#include "LiveSession.hpp"
#include "core/FixedPoint.hpp"
#include "core/Validation.hpp"
#include "strategy/StrategyFactory.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
namespace trading_app_cli::live {
static_assert(std::is_nothrow_move_constructible_v<PaperOrder> &&
              std::is_nothrow_move_assignable_v<PaperOrder>);
static_assert(std::is_nothrow_move_constructible_v<ExecutionReport> &&
              std::is_nothrow_move_constructible_v<SessionTransition>);
namespace {
std::size_t warmup(const StrategyConfiguration &c) {
    return c.kind == StrategyKind::Momentum || c.kind == StrategyKind::VNMomentum ? c.lookback + 1 : 1;
}
Price protectedPrice(Price reference, int bps, Side side) {
    auto scaled = checkedNotional(side == Side::Buy ? 10000 + bps : 10000 - bps, reference).units;
    // Maximum buy cost rounds up; sell protection rounds down, in fixed-point units.
    return Price{scaled / 10000 + (side == Side::Buy && scaled % 10000 ? 1 : 0)};
}
} // namespace
void validateSession(const SessionConfiguration &c) {
    validateInstrument(c.instrument);
    if (c.instrument.symbol == "BTCUSDT")
        throw std::invalid_argument("BTC is observation-only: M3 paper accounts use whole USD shares");
    validateStrategyConfiguration(c.strategy, c.instrument);
    if (c.initial_cash.units < 0 || c.execution.flat_fee.units < 0 || c.execution.slippage_units < 0 ||
        c.maximum_order_quantity <= 0 || c.maximum_order_quantity > 1000000 || c.maximum_position <= 0 ||
        c.maximum_position > 10000000 || c.maximum_order_notional.units <= 0 ||
        c.maximum_order_notional.units > 10000000000000000LL || c.initial_cash.units > 10000000000000000LL ||
        c.execution.flat_fee.units > 1000000000000LL || c.execution.slippage_units > 1000000000000LL ||
        !c.maximum_pending || c.maximum_pending > 64 || !c.maximum_records || c.maximum_records > 20000 ||
        c.protection_bps < 1 || c.protection_bps >= 10000 || c.order_timeout.count() < 100 ||
        c.order_timeout.count() > 3600000 || c.quote_max_age.count() < 100 ||
        c.quote_max_age.count() > 60000 || c.strategy.lookback > 10000 || c.strategy.decisions.size() > 256 ||
        !c.realtime_confirmed)
        throw std::invalid_argument("Invalid risk/timing configuration or real-time entitlement unconfirmed");
}
bool freshQuote(const LiveQuote &q, EventTime now, std::int64_t ns, std::chrono::milliseconds age) {
    auto limit = std::chrono::duration_cast<std::chrono::nanoseconds>(age).count();
    return q.bid.units > 0 && q.ask.units >= q.bid.units && q.source_time &&
           *q.source_time <= now + std::chrono::seconds(1) && *q.source_time >= now - age &&
           ns >= q.receipt_ns && ns - q.receipt_ns <= limit;
}
LiveSession::LiveSession(SessionConfiguration c, std::string id)
    : config_(std::move(c)), id_(std::move(id)), account_(config_.initial_cash),
      strategy_(createStrategy(config_.strategy, config_.instrument)) {
    validateSession(config_);
    if (id_.empty())
        throw std::invalid_argument("Empty session ID");
    orders_.reserve(config_.maximum_records);
    executions_.reserve(config_.maximum_records);
    transitions_.reserve(config_.maximum_pending + 8);
    transitions_.push_back({"start", "fresh account; warm-up uses new bars", {}, {}});
}
std::vector<Order> LiveSession::pendingOrders() const {
    std::vector<Order> out;
    for (const auto &p : orders_)
        if (p.order.status == OrderStatus::Accepted)
            out.push_back(p.order);
    return out;
}
bool LiveSession::ready(EventTime now, std::int64_t ns) const {
    return running() && healthy_ && warmup_bars_ >= warmup(config_.strategy) && quote_ &&
           freshQuote(*quote_, now, ns, config_.quote_max_age);
}
void LiveSession::expire(PaperOrder &p, std::string reason) {
    if (p.order.status != OrderStatus::Accepted)
        return;
    auto next = p;
    next.reserved_cash = {};
    next.reserved_shares = 0;
    next.order.status = OrderStatus::Expired;
    next.order.reason = reason;
    SessionTransition transition{"expire", std::move(reason), next, {}};
    transitions_.reserve(transitions_.size() + 1);
    const auto cash = checkedSubtract(reserved_cash_, p.reserved_cash);
    const auto shares = reserved_shares_ - p.reserved_shares;
    transitions_.push_back(std::move(transition));
    p = std::move(next);
    reserved_cash_ = cash;
    reserved_shares_ = shares;
    ++expiries_;
}
void LiveSession::expireAll(const std::string &reason) {
    for (auto &p : orders_)
        expire(p, reason);
}
void LiveSession::expireDue(std::int64_t ns) {
    for (auto &p : orders_)
        if (ns >= p.expiry_ns)
            expire(p, "order timeout");
}
void LiveSession::setHealth(bool healthy, EventTime now, std::int64_t ns, std::uint64_t) {
    latest_time_ = now;
    latest_ns_ = ns;
    if (!healthy) {
        expireAll("feed integrity lost");
        quote_.reset();
        strategy_->reset();
        warmup_bars_ = 0;
    }
    healthy_ = healthy;
}
void LiveSession::onTrade(Price price, EventTime time) {
    if (!running() || price.units <= 0 || (mark_time_ && time < *mark_time_))
        return;
    account_.updateMark(config_.instrument.id, price);
    mark_ = price;
    mark_time_ = time;
}
void LiveSession::onQuote(const LiveQuote &q, EventTime now, std::int64_t ns, std::uint64_t seq) {
    latest_time_ = now;
    latest_ns_ = ns;
    expireDue(ns);
    // Never turn a repeatedly polled cached quote into a new execution opportunity.
    bool duplicate = quote_ && q.bid == quote_->bid && q.ask == quote_->ask &&
                     q.source_time == quote_->source_time && q.update_id == quote_->update_id;
    if (quote_ && q.source_time && quote_->source_time && *q.source_time < *quote_->source_time)
        return;
    quote_ = q;
    if (duplicate || !running() || !healthy_ || !freshQuote(q, now, ns, config_.quote_max_age))
        return;
    for (auto &p : orders_) {
        if (p.order.status != OrderStatus::Accepted || seq <= p.order.submission_dispatch_sequence ||
            q.receipt_ns < p.submitted_ns)
            continue;
        Price fill;
        if (p.order.request.side == Side::Buy)
            fill.units = checkedAdd(Money{q.ask.units}, Money{config_.execution.slippage_units}).units;
        else
            fill.units = checkedSubtract(Money{q.bid.units}, Money{config_.execution.slippage_units}).units;
        if (fill.units <= 0 || (p.order.request.side == Side::Buy ? fill.units > p.protection.units
                                                                  : fill.units < p.protection.units)) {
            expire(p, "quote outside price protection");
            continue;
        }
        ExecutionReport execution{p.order.id,
                                  next_execution_,
                                  config_.instrument,
                                  now,
                                  p.order.request.side,
                                  p.order.request.quantity,
                                  fill,
                                  config_.execution.flat_fee,
                                  OrderStatus::Filled,
                                  {}};
        // Prepare allocations and arithmetic before the financial commit. Following
        // applyFill's strong commit, all remaining state transitions are no-throw moves.
        auto next = p;
        next.reserved_cash = {};
        next.reserved_shares = 0;
        next.order.status = OrderStatus::Filled;
        SessionTransition transition{"fill", {}, next, execution};
        executions_.reserve(executions_.size() + 1);
        transitions_.reserve(transitions_.size() + 1);
        const auto cash = checkedSubtract(reserved_cash_, p.reserved_cash);
        const auto shares = reserved_shares_ - p.reserved_shares;
        const auto applied = account_.applyFill(execution);
        if (applied.status != FillApplyStatus::Applied)
            throw std::logic_error("Live fill/account commit rejected");
        executions_.push_back(std::move(execution));
        transitions_.push_back(std::move(transition));
        p = std::move(next);
        reserved_cash_ = cash;
        reserved_shares_ = shares;
        ++next_execution_;
    }
}
void LiveSession::submit(const OrderRequest &request, EventTime now, std::int64_t ns, std::uint64_t seq) {
    if (orders_.size() >= config_.maximum_records)
        throw std::runtime_error("session order record limit reached");
    PaperOrder p;
    p.order.id = orders_.size() + 1;
    p.order.request = request;
    p.order.submission_time = now;
    p.order.submission_dispatch_sequence = seq;
    p.order.submission_sequence = seq;
    p.order.status = OrderStatus::Rejected;
    p.submitted_ns = ns;
    auto timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(config_.order_timeout).count();
    if (ns > std::numeric_limits<std::int64_t>::max() - timeout)
        throw std::overflow_error("order expiry overflows");
    p.expiry_ns = ns + timeout;
    std::string reason;
    try {
        if (!ready(now, ns))
            reason = "feed, quote or strategy warm-up not ready";
        else if (request.instrument != config_.instrument ||
                 request.strategy_id != config_.strategy.strategy_id || request.type != OrderType::Market ||
                 (request.side != Side::Buy && request.side != Side::Sell))
            reason = "invalid instrument/strategy/order";
        else if (request.quantity <= 0 || request.quantity > config_.maximum_order_quantity)
            reason = "order quantity limit";
        else if (pendingOrders().size() >= config_.maximum_pending)
            reason = "pending order limit";
        else {
            // A second same-side request at this decision position is rejected as duplicate.
            bool duplicate = false;
            Quantity pending_buys = 0;
            for (const auto &old : orders_)
                if (old.order.status == OrderStatus::Accepted) {
                    if (old.order.submission_dispatch_sequence == seq &&
                        old.order.request.side == request.side)
                        duplicate = true;
                    if (old.order.request.side == Side::Buy)
                        pending_buys += old.order.request.quantity;
                }
            if (duplicate)
                reason = "duplicate submission at decision position";
            p.protection = protectedPrice(request.side == Side::Buy ? quote_->ask : quote_->bid,
                                          config_.protection_bps, request.side);
            const auto protected_notional = checkedNotional(request.quantity, p.protection);
            if (reason.empty() && protected_notional.units > config_.maximum_order_notional.units)
                reason = "order notional limit";
            const auto held = account_.quantity(config_.instrument.id);
            if (reason.empty() && request.side == Side::Buy) {
                if (request.quantity > config_.maximum_position - held - pending_buys)
                    reason = "total position limit";
                else {
                    p.reserved_cash = checkedAdd(protected_notional, config_.execution.flat_fee);
                    if (p.reserved_cash.units > checkedSubtract(account_.cash(), reserved_cash_).units)
                        reason = "insufficient available cash including fee";
                }
            } else if (reason.empty()) {
                if (request.quantity > held - reserved_shares_)
                    reason = "insufficient available holdings";
                else if (protected_notional.units < config_.execution.flat_fee.units)
                    reason = "sell proceeds cannot cover fee";
                else
                    p.reserved_shares = request.quantity;
            }
        }
    } catch (const std::exception &) {
        reason = "checked risk arithmetic rejected order";
    }
    Money cash = reserved_cash_;
    auto shares = reserved_shares_;
    if (reason.empty()) {
        p.order.status = OrderStatus::Accepted;
        cash = checkedAdd(cash, p.reserved_cash);
        shares += p.reserved_shares;
    } else {
        p.reserved_cash = {};
        p.reserved_shares = 0;
        p.order.reason = reason;
    }
    SessionTransition transition{"order", reason, p, {}};
    orders_.reserve(orders_.size() + 1);
    transitions_.reserve(transitions_.size() + 1);
    orders_.push_back(std::move(p));
    transitions_.push_back(std::move(transition));
    reserved_cash_ = cash;
    reserved_shares_ = shares;
    if (!reason.empty())
        ++rejections_;
}
void LiveSession::onBar(const BarEvent &bar, EventTime now, std::int64_t ns, std::uint64_t seq) {
    latest_time_ = now;
    latest_ns_ = ns;
    if (!running() || !healthy_)
        return;
    ++bars_;
    ++warmup_bars_;
    expireDue(ns);
    auto requests = strategy_->onBar(bar, account_.snapshot(), PendingOrderView(pendingOrders()));
    transitions_.push_back({"decision", std::to_string(requests.size()) + " request(s)", {}, {}});
    const auto decision = std::chrono::steady_clock::now();
    for (const auto &request : requests) {
        submit(request, now, ns, seq);
        transitions_.back().decision_to_submit_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - decision)
                .count();
    }
}
void LiveSession::onTimer(EventTime now, std::int64_t ns, std::uint64_t seq) {
    latest_time_ = now;
    latest_ns_ = ns;
    if (!running())
        return;
    expireDue(ns);
    if (quote_ && !freshQuote(*quote_, now, ns, config_.quote_max_age))
        expireAll("quote stale");
    (void)seq;
}
void LiveSession::stop(std::string reason, bool failed) {
    if (!running())
        return;
    expireAll(reason);
    state_ = failed ? "Failed" : "Stopped";
    transitions_.push_back({failed ? "failure" : "stop", std::move(reason), {}, {}});
}
PaperSnapshot LiveSession::snapshot() const {
    PaperSnapshot s;
    s.id = id_;
    s.state = state_;
    s.account = account_.snapshot();
    s.reserved_cash = reserved_cash_;
    s.reserved_shares = reserved_shares_;
    s.bars = bars_;
    s.orders = orders_.size();
    s.fills = executions_.size();
    s.rejected = rejections_;
    s.expired = expiries_;
    s.mark_time = mark_time_;
    s.pending = pendingOrders().size();
    s.warmup_required = warmup(config_.strategy);
    s.warmup_completed = warmup_bars_;
    s.blocked_reason = !running()                         ? state_
                       : !healthy_                        ? "feed continuity unavailable"
                       : warmup_bars_ < s.warmup_required ? "strategy warming up from new bars"
                       : !quote_                          ? "waiting for fresh timestamped quote"
                       : !freshQuote(*quote_, latest_time_, latest_ns_, config_.quote_max_age)
                           ? "quote stale; new orders blocked"
                           : "ready for completed-bar decisions";
    const auto count = std::min<std::size_t>(200, orders_.size());
    s.recent_orders.assign(orders_.end() - static_cast<std::ptrdiff_t>(count), orders_.end());
    const auto fills = std::min<std::size_t>(200, executions_.size());
    s.recent_executions.assign(executions_.end() - static_cast<std::ptrdiff_t>(fills), executions_.end());
    return s;
}
std::vector<SessionTransition> LiveSession::takeTransitions() {
    auto out = std::move(transitions_);
    transitions_.clear();
    return out;
}
} // namespace trading_app_cli::live
