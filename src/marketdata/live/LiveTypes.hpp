#pragma once
#include "core/Types.hpp"
#include "live/LiveSession.hpp"
#include "live/Performance.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace trading_app_cli::live {
// Observation quantities use 1e-8 base units. Historical Quantity remains whole shares.
inline constexpr std::int64_t quantity_scale = 100'000'000;
struct Trade {
    EventTime time{};
    Price price{};
    std::int64_t quantity_atoms{};
    std::optional<std::uint64_t> id;
};
struct Bar {
    EventTime start{}, end{};
    Price open{}, high{}, low{}, close{};
    std::int64_t volume_atoms{};
    std::uint64_t sequence{};
};
// Explicit bridge for Stage 2: fractional volume is rejected, never truncated.
inline std::optional<BarEvent> asWholeShareBar(const Bar& b, const Instrument& instrument) {
    if (b.volume_atoms % quantity_scale) return std::nullopt;
    return BarEvent{instrument,b.start,b.end,b.open,b.high,b.low,b.close,
                    b.volume_atoms / quantity_scale,b.sequence};
}
enum class Provider { Binance, Finnhub };
struct Configuration {
    Provider provider{Provider::Binance};
    std::string symbol{"BTCUSDT"};
    std::string entitlement{"Public spot trades / USDT"};
    std::string recording_directory{"recordings"};
    std::string api_key; // memory only; never serialized
    std::size_t queue_capacity{8192};
    bool bidask_enabled{},synthetic{};
    int bidask_poll_ms{2000};
};
struct Envelope {
    std::uint64_t sequence{};
    std::int64_t receipt_ns{};
    EventTime receipt_utc{};
    enum class Kind { Raw, Connected, Gap, Timer, Rejected, Reference, Heartbeat, Normalized, End } kind{Kind::Raw};
    std::string text;
    std::int64_t enqueued_ns{};
};
struct Snapshot {
    std::string session, state{"Disconnected"}, error, recording, symbol, unit, entitlement;
    std::uint64_t received{}, processed{}, rejected{}, duplicates{}, late{}, gaps{}, reconnects{}, queue_high{};
    std::uint64_t version{};
    std::optional<Trade> last;
    std::optional<Price> reference_price;
    std::optional<EventTime> reference_time;
    std::optional<Bar> forming;
    std::vector<Bar> bars;
    std::optional<double> sma, ema, rsi;
    std::size_t indicator_period{14}, warmup{};
    bool transport_healthy{}, recording_ok{};
    std::int64_t last_receipt_ns{};
    std::uint64_t command_ack{};
    std::optional<LiveQuote> quote;
    std::optional<EventTime> heartbeat;
    PaperSnapshot paper;
    PerformanceSnapshot performance;
    bool synthetic{},paper_ready{};
    EventTime logical_time{};
    std::int64_t published_ns{};

};
inline EventTime utcNow() { return std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()); }
}
