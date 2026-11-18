#include "LiveProcessor.hpp"
#include <algorithm>
#include <stdexcept>
namespace trading_app_cli::live {
LiveProcessor::LiveProcessor(std::string symbol) : symbol_(std::move(symbol)) {}
void LiveProcessor::invalidate(EventTime now) {
    builder_.invalidate(now);
    state_.bars.clear();
    indicators_ = IndicatorSeries(state_.indicator_period);
    state_.forming.reset();
    state_.sma.reset();
    state_.ema.reset();
    state_.rsi.reset();
    state_.warmup = 0;
    state_.quote.reset();
    if (session_)
        session_->setHealth(false, now, state_.clock_ns, state_.processed);
}
void LiveProcessor::collect(ProcessingResult &out) {
    if (session_) {
        auto events = session_->takeTransitions();
        out.transitions.insert(out.transitions.end(), events.begin(), events.end());
        state_.paper = session_->snapshot();
    }
    state_.forming = builder_.forming();
    state_.late = builder_.late();
    state_.sma = indicators_.sma;
    state_.ema = indicators_.ema;
    state_.rsi = indicators_.rsi;
    state_.warmup = indicators_.count();
}
ProcessingResult LiveProcessor::apply(const Input &in) {
    if (in.sequence != state_.processed + 1 || in.clock_ns < state_.clock_ns || in.time < state_.now)
        throw std::invalid_argument("Non-monotonic processing position/clock");
    state_.processed = in.sequence;
    state_.clock_ns = in.clock_ns;
    state_.now = in.time;
    if (in.command_id)
        state_.command_ack = std::max(state_.command_ack, in.command_id);
    ProcessingResult out;
    if (state_.failed && in.kind != Input::Kind::End)
        throw std::logic_error("Input after terminal failure");
    switch (in.kind) {
    case Input::Kind::Connected:
        if (seen_connection_)
            ++state_.reconnects;
        seen_connection_ = true;
        state_.connected = true;
        state_.subscribed = false;
        state_.feed_state = "Awaiting subscription";
        invalidate(in.time);
        break;
    case Input::Kind::Subscribed:
        if (state_.connected) {
            state_.subscribed = true;
            state_.feed_state = "Ready / warming up";
            if (session_)
                session_->setHealth(true, in.time, in.clock_ns, in.sequence);
        }
        break;
    case Input::Kind::Heartbeat:
        state_.heartbeat = in.time;
        break;
    case Input::Kind::Trade: {
        if (!in.trade)
            throw std::invalid_argument("Trade payload missing");
        ++state_.trades;
        const auto &t = *in.trade;
        if (t.id && ids_.contains(*t.id)) {
            ++state_.duplicates;
            out.quality = "duplicate";
            break;
        }
        if (t.id) {
            ids_.insert(*t.id);
            id_order_.push_back(*t.id);
            if (id_order_.size() > 65536) {
                ids_.erase(id_order_.front());
                id_order_.pop_front();
            }
        }
        if (!state_.connected) {
            out.quality = "disconnected";
            break;
        }
        if (!state_.subscribed) {
            state_.subscribed = true;
            state_.feed_state = "Ready / subscription evidenced by trade";
            if (session_)
                session_->setHealth(true, in.time, in.clock_ns, in.sequence);
        }
        if (!state_.last || t.time >= state_.last->time) {
            state_.last = t;
            if (session_)
                session_->onTrade(t.price, t.time);
        }
        out.quality = builder_.add(t, in.sequence) ? "accepted" : "late_or_partial_start";
        break;
    }
    case Input::Kind::Quote: {
        if (!in.quote)
            throw std::invalid_argument("Quote payload missing");
        ++state_.quotes;
        const auto &q = *in.quote;
        if (q.bid.units <= 0 || q.ask.units < q.bid.units) {
            ++state_.rejected;
            state_.error = in.reason.empty() ? "Crossed or invalid quote" : in.reason;
            out.quality = "crossed_or_invalid_quote";
            if (session_)
                session_->setHealth(false, in.time, in.clock_ns, in.sequence);
            state_.quote.reset();
            break;
        }
        if (state_.quote && q.source_time && state_.quote->source_time &&
            *q.source_time < *state_.quote->source_time) {
            ++state_.rejected;
            out.quality = "out_of_order_quote";
            break;
        }
        state_.quote = q;
        if (state_.connected && state_.subscribed && session_) {
            session_->setHealth(true, in.time, in.clock_ns, in.sequence);
            session_->onQuote(q, in.time, in.clock_ns, in.sequence);
        }
        break;
    }
    case Input::Kind::Timer:
        if (session_)
            session_->onTimer(in.time, in.clock_ns, in.sequence);
        if (state_.connected && state_.subscribed)
            for (auto bar : builder_.advance(in.time)) {
                out.bars.push_back(bar);
                state_.bars.push_back(bar);
                if (state_.bars.size() > 2000)
                    state_.bars.erase(state_.bars.begin());
                indicators_.add(static_cast<double>(bar.close.units) / usd_scale);
                if (session_ && session_->running()) {
                    const auto converted = asWholeShareBar(bar, session_->configuration().instrument);
                    if (converted)
                        session_->onBar(*converted, in.time, in.clock_ns, in.sequence);
                    else {
                        ++state_.gaps;
                        session_->setHealth(false, in.time, in.clock_ns, in.sequence);
                        out.quality = "fractional share bar unsupported for whole-share strategy";
                    }
                }
            }
        break;
    case Input::Kind::Gap:
    case Input::Kind::Reject:
        if (in.kind == Input::Kind::Reject)
            ++state_.rejected;
        ++state_.gaps;
        state_.error = in.reason;
        state_.feed_state = "Recovering / warm-up reset";
        if (in.kind == Input::Kind::Gap) {
            state_.connected = false;
            state_.subscribed = false;
        }
        invalidate(in.time);
        break;
    case Input::Kind::Start:
        try {
            if (session_ && session_->running())
                throw std::invalid_argument("Session already running");
            if (!in.configuration || in.session_id.empty())
                throw std::invalid_argument("Start configuration missing");
            if (in.configuration->instrument.symbol != symbol_)
                throw std::invalid_argument("Start instrument differs from subscription");
            validateSession(*in.configuration);
            if (!state_.connected || !state_.subscribed || !state_.quote ||
                !freshQuote(*state_.quote, in.time, in.clock_ns, in.configuration->quote_max_age))
                throw std::invalid_argument(
                    "Start needs a healthy subscription and fresh timestamped bid/ask");
            session_ = std::make_unique<LiveSession>(*in.configuration, in.session_id);
            session_->setHealth(true, in.time, in.clock_ns, in.sequence);
            if (state_.last)
                session_->onTrade(state_.last->price, state_.last->time);
            // Cached quote establishes protection only; it cannot fill anything.
            session_->onQuote(*state_.quote, in.time, in.clock_ns, in.sequence);
        } catch (const std::invalid_argument &e) {
            out.transitions.push_back({"start_rejected", e.what(), {}, {}});
            state_.error = e.what();
        }
        break;
    case Input::Kind::Stop:
        if (session_)
            session_->stop(in.reason.empty() ? "user stop" : in.reason);
        break;
    case Input::Kind::Indicator:
        state_.indicator_period = in.period;
        indicators_ = IndicatorSeries(in.period);
        for (const auto &b : state_.bars)
            indicators_.add(static_cast<double>(b.close.units) / usd_scale);
        break;
    case Input::Kind::Failure:
        state_.failed = true;
        state_.error = in.reason;
        state_.feed_state = "Failed";
        invalidate(in.time);
        if (session_)
            session_->stop(in.reason, true);
        state_.connected = false;
        state_.subscribed = false;
        break;
    case Input::Kind::End:
        if (session_)
            session_->stop(in.reason.empty() ? "feed disconnected" : in.reason);
        state_.connected = false;
        state_.subscribed = false;
        if (!state_.failed)
            state_.feed_state = "Disconnected";
        break;
    }
    collect(out);
    if (in.kind == Input::Kind::End || in.kind == Input::Kind::Failure)
        state_.forming.reset();
    return out;
}
} // namespace trading_app_cli::live
