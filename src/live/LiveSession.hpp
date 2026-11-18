#pragma once
#include "core/Types.hpp"
#include "portfolio/PortfolioAccounting.hpp"
#include "strategy/ITradingModel.hpp"
#include <memory>
#include <optional>
#include <vector>
namespace trading_app_cli::live {
struct SessionConfiguration {
    Instrument instrument{1, "AAPL"};
    Money initial_cash{10000 * usd_scale};
    StrategyConfiguration strategy;
    ExecutionConfiguration execution;
    Quantity maximum_order_quantity{1000}, maximum_position{10000};
    Money maximum_order_notional{100000 * usd_scale};
    std::size_t maximum_pending{8}, maximum_records{10000};
    int protection_bps{100};
    std::chrono::milliseconds order_timeout{10000}, quote_max_age{5000};
    bool realtime_confirmed{false};
};
struct LiveQuote {
    Price bid{}, ask{};
    std::optional<EventTime> source_time;
    std::int64_t receipt_ns{};
    std::optional<std::uint64_t> update_id;
    bool operator==(const LiveQuote &) const = default;
};
struct PaperOrder {
    Order order;
    Price protection{};
    Money reserved_cash{};
    Quantity reserved_shares{};
    std::int64_t submitted_ns{}, expiry_ns{};
};
struct SessionTransition {
    std::string kind, reason;
    std::optional<PaperOrder> order;
    std::optional<ExecutionReport> execution;
    std::optional<std::int64_t> decision_to_submit_ns{};
};
struct PaperSnapshot {
    std::string id, state{"Idle"}, blocked_reason;
    std::optional<PortfolioSnapshot> account;
    Money reserved_cash{};
    Quantity reserved_shares{};
    std::uint64_t bars{}, orders{}, fills{}, rejected{}, expired{};
    std::optional<EventTime> mark_time;
    std::vector<PaperOrder> recent_orders;
    std::vector<ExecutionReport> recent_executions;
    std::size_t pending{}, warmup_required{}, warmup_completed{};
};
void validateSession(const SessionConfiguration &);
bool freshQuote(const LiveQuote &, EventTime now, std::int64_t dispatch_ns,
                std::chrono::milliseconds max_age);
class LiveSession {
  public:
    LiveSession(SessionConfiguration configuration, std::string id);
    void setHealth(bool healthy, EventTime now, std::int64_t ns, std::uint64_t sequence);
    void onTrade(Price price, EventTime time);
    void onQuote(const LiveQuote &, EventTime now, std::int64_t ns, std::uint64_t sequence);
    void onBar(const BarEvent &, EventTime now, std::int64_t ns, std::uint64_t sequence);
    void onTimer(EventTime now, std::int64_t ns, std::uint64_t sequence);
    void stop(std::string reason, bool failed = false);
    PaperSnapshot snapshot() const;
    std::vector<SessionTransition> takeTransitions();
    const SessionConfiguration &configuration() const { return config_; }
    bool running() const { return state_ == "Running"; }

  private:
    void submit(const OrderRequest &, EventTime, std::int64_t, std::uint64_t);
    void expire(PaperOrder &, std::string);
    void expireAll(const std::string &);
    void expireDue(std::int64_t);
    bool ready(EventTime, std::int64_t) const;
    std::vector<Order> pendingOrders() const;
    SessionConfiguration config_;
    std::string id_, state_{"Running"};
    PortfolioAccounting account_;
    std::unique_ptr<ITradingModel> strategy_;
    std::optional<LiveQuote> quote_;
    std::optional<EventTime> mark_time_;
    std::optional<Price> mark_;
    EventTime latest_time_{};
    std::int64_t latest_ns_{};
    bool healthy_{};
    std::vector<PaperOrder> orders_;
    std::vector<ExecutionReport> executions_;
    std::vector<SessionTransition> transitions_;
    Money reserved_cash_{};
    Quantity reserved_shares_{};
    std::uint64_t bars_{}, warmup_bars_{}, rejections_{}, expiries_{}, next_execution_{1};
};
} // namespace trading_app_cli::live
