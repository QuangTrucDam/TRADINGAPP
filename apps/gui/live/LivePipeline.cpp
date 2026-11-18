#include "LivePipeline.hpp"
#include "ProviderNormalizer.hpp"
#include "ProviderStreamClient.hpp"
#include "SessionArchive.hpp"
#include "live/SyntheticSession.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace trading_app_cli::live {
namespace {
std::int64_t elapsed(std::chrono::steady_clock::time_point origin) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - origin)
        .count();
}
} // namespace
LivePipeline::LivePipeline(Configuration c, StreamRunner stream)
    : config_(std::move(c)), stream_(std::move(stream)), input_(config_.queue_capacity),
      session_(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()),
      origin_(std::chrono::steady_clock::now()) {
    if (config_.provider == Provider::Binance && config_.symbol != "BTCUSDT")
        throw std::invalid_argument("Binance supports BTCUSDT in M3");
    if (config_.provider == Provider::Finnhub && config_.api_key.empty() && !config_.synthetic && !stream_)
        throw std::invalid_argument("Set FINNHUB_API_KEY locally in .env or environment");
    if (config_.symbol.empty() || config_.symbol.size() > 64 ||
        config_.symbol.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:._-/") != std::string::npos)
        throw std::invalid_argument("Unsupported subscription symbol");
    if (config_.bidask_poll_ms < 2000 || config_.bidask_poll_ms > 60000)
        throw std::invalid_argument("Bid/ask polling interval outside 2000..60000 ms");
    processor_ = std::thread(&LivePipeline::process, this);
    try {
        network_ = std::thread(&LivePipeline::network, this);
    } catch (...) {
        stop_ = true;
        input_.close();
        processor_.join();
        throw;
    }
}
LivePipeline::~LivePipeline() {
    requestStop();
    if (network_.joinable())
        network_.join();
    if (processor_.joinable())
        processor_.join();
}
Snapshot LivePipeline::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}
void LivePipeline::publish(Snapshot s) {
    std::lock_guard lock(mutex_);
    s.version = snapshot_.version + 1;
    snapshot_ = std::move(s);
}
std::uint64_t LivePipeline::command(Input i) {
    if (finished() || stop_)
        throw std::logic_error("Feed is disconnected/stopping");
    i.command_id = ++next_command_;
    if (!commands_.push(std::move(i)))
        throw std::runtime_error("GUI command queue full");
    return next_command_;
}
void LivePipeline::setIndicatorPeriod(std::size_t p) {
    if (p < 2 || p > 10000)
        throw std::invalid_argument("Indicator period outside 2..10000");
    Input i;
    i.kind = Input::Kind::Indicator;
    i.period = p;
    command(std::move(i));
}
std::uint64_t LivePipeline::startPaper(SessionConfiguration c) {
    validateSession(c);
    Input i;
    i.kind = Input::Kind::Start;
    i.configuration = std::move(c);
    i.session_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    return command(std::move(i));
}
std::uint64_t LivePipeline::stopPaper() {
    if (finished())
        return next_command_;
    const auto id = ++next_command_;
    if (urgent_stop_command_.exchange(id) == 0)
        input_.wake();
    return id;
}

void LivePipeline::network() {
    try {
        if (stream_)
            stream_(config_, input_, stop_, overflow_, origin_);
        else if (config_.synthetic) {
            auto inputs = syntheticSession(60);
            std::uint64_t seq = 0;
            for (const auto &i : inputs) {
                if (stop_)
                    break;
                auto bytes = QJsonDocument(inputJson(i)).toJson(QJsonDocument::Compact).toStdString();
                Envelope e{
                    ++seq,           i.receipt_ns, i.time, Envelope::Kind::Normalized, std::move(bytes),
                    elapsed(origin_)};
                if (!input_.push(std::move(e))) {
                    overflow_ = true;
                    stop_ = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            input_.close();
        } else
            runProviderStream(config_, input_, stop_, overflow_, origin_);
    } catch (...) {
        overflow_ = true;
        stop_ = true;
        input_.close();
    }
    network_done_ = true;
}
void LivePipeline::process() {
    Snapshot s;
    s.session = session_;
    s.symbol = config_.symbol;
    s.unit = config_.provider == Provider::Binance ? "USDT / BTC" : "USD / shares";
    s.entitlement = config_.entitlement;
    s.synthetic = config_.synthetic;
    s.state = "Connecting";
    s.recording = (std::filesystem::path(config_.recording_directory) / (session_ + ".jsonl")).string();
    LiveProcessor processor(config_.symbol);
    TimingSeries queue_wait, core, receipt_commit, decision_submit;
    bool ended = false, recording_complete = false;
    auto update = [&] {
        const auto &p = processor.state();
        s.state = p.feed_state;
        s.error = p.error;
        s.processed = p.processed;
        s.rejected = p.rejected;
        s.duplicates = p.duplicates;
        s.late = p.late;
        s.gaps = p.gaps;
        s.reconnects = p.reconnects;
        s.last = p.last;
        s.forming = p.forming;
        s.bars = p.bars;
        s.sma = p.sma;
        s.ema = p.ema;
        s.rsi = p.rsi;
        s.warmup = p.warmup;
        s.indicator_period = p.indicator_period;
        s.transport_healthy = p.connected && p.subscribed;
        s.command_ack = p.command_ack;
        s.quote = p.quote;
        s.heartbeat = p.heartbeat;
        s.paper = p.paper;
        s.logical_time = p.now;
        s.paper_ready = p.connected && p.subscribed && p.quote && config_.provider == Provider::Finnhub &&
                        !p.failed && freshQuote(*p.quote, p.now, p.clock_ns, std::chrono::milliseconds(5000));
        s.queue_high = input_.highWater();
        s.performance.queue_wait = queue_wait.summary();
        s.performance.core_processing = core.summary();
        s.performance.receipt_to_commit = receipt_commit.summary();
        s.performance.decision_to_submit = decision_submit.summary();
        s.performance.queue_high = input_.highWater();
        s.performance.queue_depth = input_.depth();
        s.performance.input_overflows = overflow_ ? 1 : 0;
        s.performance.duration_seconds = elapsed(origin_) / 1e9;
        s.performance.events_per_second =
            s.performance.duration_seconds > 0 ? p.processed / s.performance.duration_seconds : 0;
        s.published_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
        publish(s);
    };
    try {
        ArchiveWriter archive(s.recording,
                              {{"session", QString::fromStdString(session_)},
                               {"symbol", QString::fromStdString(config_.symbol)},
                               {"provider", config_.provider == Provider::Binance ? "binance" : "finnhub"},
                               {"entitlement", QString::fromStdString(config_.entitlement)},
                               {"source_mode", config_.synthetic ? "synthetic_demo"
                                               : stream_         ? "injected_fixture"
                                                                 : "provider_stream"},
                               {"input_queue_capacity", QString::number(config_.queue_capacity)},
                               {"bidask_poll_ms", config_.bidask_poll_ms}});
        auto apply = [&](Input i) {
            const auto &prior = processor.state();
            i.sequence = prior.processed + 1;
            i.clock_ns = std::max(i.clock_ns, prior.clock_ns);
            i.time = std::max(i.time, prior.now);
            const auto before = std::chrono::steady_clock::now();
            const auto result = processor.apply(i);
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - before)
                                .count();
            core.add(ns);
            for (const auto &t : result.transitions)
                if (t.decision_to_submit_ns)
                    decision_submit.add(*t.decision_to_submit_ns);
            try {
                archive.step(i, result, processor.state());
            } catch (...) {
                s.performance.recorder_overflows = archive.overflows();
                throw;
            }
        };
        auto normalized = [&](Input::Kind kind, const Envelope &e) {
            Input i;
            i.kind = kind;
            i.receipt_ns = e.receipt_ns;
            i.clock_ns = std::max(e.receipt_ns, elapsed(origin_));
            i.time = std::max(e.receipt_utc, utcNow());
            return i;
        };
        auto last_publish = std::chrono::steady_clock::now();
        Envelope e;
        Input cmd;
        while (!input_.drained()) {
            while (commands_.pop(cmd, std::chrono::milliseconds(0))) {
                if (config_.synthetic) {
                    cmd.clock_ns = processor.state().clock_ns;
                    cmd.time = processor.state().now;
                } else {
                    cmd.clock_ns = elapsed(origin_);
                    cmd.time = utcNow();
                }
                cmd.receipt_ns = cmd.clock_ns;
                apply(cmd);
            }
            if (auto id = urgent_stop_command_.exchange(0)) {
                Input stop;
                stop.kind = Input::Kind::Stop;
                stop.command_id = id;
                stop.reason = "user stop";
                stop.clock_ns = config_.synthetic ? processor.state().clock_ns : elapsed(origin_);
                stop.time = config_.synthetic ? processor.state().now : utcNow();
                stop.receipt_ns = stop.clock_ns;
                apply(stop);
            }
            s.performance.recorder_depth = archive.depth();
            s.performance.recorder_high = archive.highWater();
            s.performance.recorder_overflows = archive.overflows();
            if (archive.failed())
                throw std::runtime_error("Audit recording failed; paper processing stopped");
            if (overflow_) {
                auto i = normalized(Input::Kind::Failure,
                                    Envelope{0, elapsed(origin_), utcNow(), Envelope::Kind::Rejected, {}});
                i.reason = "input queue overflow or transport exception";
                apply(i);
                stop_ = true;
                break;
            }
            if (!input_.pop(e))
                continue;
            auto before = elapsed(origin_);
            if (e.enqueued_ns > 0)
                queue_wait.add(before - e.enqueued_ns);
            if (e.kind == Envelope::Kind::Normalized) {
                auto i = inputFromJson(QJsonDocument::fromJson(QByteArray::fromStdString(e.text)).object());
                if (i.kind == Input::Kind::End)
                    ended = true;
                apply(i);
            } else if (e.kind == Envelope::Kind::Raw) {
                ++s.received;
                auto decoded = decode(config_.provider, config_.symbol, e.text, e.receipt_utc);
                if (decoded.acknowledgement)
                    apply(normalized(Input::Kind::Subscribed, e));
                if (decoded.heartbeat)
                    apply(normalized(Input::Kind::Heartbeat, e));
                if (decoded.quote_unavailable) {
                    auto i = normalized(Input::Kind::Quote, e);
                    i.quote = LiveQuote{};
                    i.reason = "Finnhub bid/ask unavailable: check entitlement, permissions and rate limit";
                    apply(i);
                }
                for (const auto &reason : decoded.reasons) {
                    auto i = normalized(Input::Kind::Reject, e);
                    i.reason = reason;
                    apply(i);
                }
                for (const auto &trade : decoded.trades) {
                    auto i = normalized(Input::Kind::Trade, e);
                    i.trade = trade;
                    apply(i);
                }
                for (auto quote : decoded.quotes) {
                    quote.receipt_ns = e.receipt_ns;
                    auto i = normalized(Input::Kind::Quote, e);
                    i.quote = quote;
                    apply(i);
                }
                if (decoded.fatal) {
                    auto i = normalized(Input::Kind::Failure, e);
                    i.reason = "provider rejected subscription";
                    apply(i);
                    stop_ = true;
                    break;
                }
            } else if (e.kind == Envelope::Kind::Reference) {
                auto o = QJsonDocument::fromJson(QByteArray::fromStdString(e.text)).object();
                auto price = o.value("c").toDouble();
                auto seconds = o.value("t").toInteger();
                if (std::isfinite(price) && price > 0 && price < 1e10 && seconds > 0 &&
                    seconds < 9000000000LL) {
                    auto units = price * usd_scale;
                    if (std::abs(units - std::round(units)) < 1e-5) {
                        s.reference_price = Price{static_cast<std::int64_t>(std::round(units))};
                        s.reference_time = EventTime{std::chrono::seconds(seconds)};
                    }
                }
            } else {
                Input::Kind kind = Input::Kind::Timer;
                switch (e.kind) {
                case Envelope::Kind::Connected:
                    kind = Input::Kind::Connected;
                    break;
                case Envelope::Kind::Gap:
                    kind = Input::Kind::Gap;
                    break;
                case Envelope::Kind::Heartbeat:
                    kind = Input::Kind::Heartbeat;
                    break;
                case Envelope::Kind::Rejected:
                    kind = Input::Kind::Failure;
                    break;
                case Envelope::Kind::End:
                    kind = Input::Kind::End;
                    ended = true;
                    break;
                default:
                    break;
                }
                auto i = normalized(kind, e);
                i.reason = e.text;
                apply(i);
                if (kind == Input::Kind::Failure) {
                    stop_ = true;
                    break;
                }
            }
            if (e.enqueued_ns > 0)
                receipt_commit.add(elapsed(origin_) - e.enqueued_ns);
            if (std::chrono::steady_clock::now() - last_publish > std::chrono::milliseconds(100)) {
                s.recording_ok = !archive.failed();
                s.performance.recorder_high = archive.highWater();
                update();
                last_publish = std::chrono::steady_clock::now();
            }
        }
        if (overflow_ && !processor.state().failed) {
            Input i;
            i.kind = Input::Kind::Failure;
            i.time = processor.state().now;
            i.clock_ns = processor.state().clock_ns;
            i.reason = "input queue overflow";
            apply(i);
            stop_ = true;
        }
        if (!ended && !processor.state().failed) {
            Input end;
            end.kind = Input::Kind::End;
            end.time = config_.synthetic ? processor.state().now : utcNow();
            end.clock_ns = config_.synthetic ? processor.state().clock_ns : elapsed(origin_);
            end.reason = "feed ended or disconnected";
            apply(end);
        }
        archive.finish(processor.state());
        while (!archive.done())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (archive.failed())
            throw std::runtime_error("Recording flush failed; recording incomplete");
        recording_complete = true;
    } catch (const std::exception &ex) {
        stop_ = true;
        if (!processor.state().failed) {
            try {
                Input fail;
                fail.kind = Input::Kind::Failure;
                fail.sequence = processor.state().processed + 1;
                fail.time = processor.state().now;
                fail.clock_ns = processor.state().clock_ns;
                fail.reason = ex.what();
                processor.apply(fail);
            } catch (...) {
            }
        }
        s.error = ex.what();
    } catch (...) {
        stop_ = true;
        s.error = "Live processing exception; recording incomplete";
    }
    s.recording_ok = recording_complete;
    update();
    if (!recording_complete) {
        s.state = "Failed";
        s.recording_ok = false;
        publish(s);
    }
    processing_done_ = true;
}
} // namespace trading_app_cli::live
