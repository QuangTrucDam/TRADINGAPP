#pragma once
#include "LiveSession.hpp"
#include "indicators/IndicatorSeries.hpp"
#include "marketdata/live/LiveBarBuilder.hpp"
#include <deque>
#include <set>
namespace trading_app_cli::live {
struct Input {
    enum class Kind {
        Connected,
        Subscribed,
        Heartbeat,
        Trade,
        Quote,
        Timer,
        Gap,
        Reject,
        Start,
        Stop,
        Indicator,
        End,
        Failure
    } kind{Kind::Timer};
    std::uint64_t sequence{}, command_id{};
    EventTime time{};
    std::int64_t clock_ns{}, receipt_ns{};
    std::string reason, session_id;
    std::optional<Trade> trade;
    std::optional<LiveQuote> quote;
    std::optional<SessionConfiguration> configuration;
    std::size_t period{14};
};
struct ProcessingResult {
    std::vector<Bar> bars;
    std::vector<SessionTransition> transitions;
    std::string quality;
};
struct ProcessorState {
    std::string feed_state{"Disconnected"}, error;
    bool connected{}, subscribed{}, failed{};
    std::uint64_t processed{}, trades{}, quotes{}, rejected{}, duplicates{}, gaps{}, reconnects{}, late{},
        command_ack{};
    EventTime now{};
    std::int64_t clock_ns{};
    std::optional<Trade> last;
    std::optional<LiveQuote> quote;
    std::optional<EventTime> heartbeat;
    std::optional<Bar> forming;
    std::vector<Bar> bars;
    std::size_t indicator_period{14}, warmup{};
    std::optional<double> sma, ema, rsi;
    PaperSnapshot paper;
};
// The same deterministic object is used for live processing, decision replay,
// synthetic fixtures, fault injection and measured replay. No Qt or wall clock.
class LiveProcessor {
  public:
    explicit LiveProcessor(std::string symbol);
    ProcessingResult apply(const Input &);
    const ProcessorState &state() const { return state_; }

  private:
    void invalidate(EventTime now);
    void collect(ProcessingResult &);
    std::string symbol_;
    ProcessorState state_;
    LiveBarBuilder builder_;
    IndicatorSeries indicators_;
    std::unique_ptr<LiveSession> session_;
    std::set<std::uint64_t> ids_;
    std::deque<std::uint64_t> id_order_;
    bool seen_connection_{};
};
} // namespace trading_app_cli::live
