#pragma once
#include "core/Types.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace trading_app_cli {
inline void validateInstrument(const Instrument& instrument) {
    if (instrument.id == 0 || instrument.symbol.empty()
        || instrument.currency != Currency::USD
        || instrument.price_scale != usd_scale || instrument.whole_share_step != 1)
        throw std::invalid_argument("Unsupported instrument");
}
inline void validateHistoricalBar(const BarEvent& bar, const Instrument& instrument) {
    if (bar.instrument != instrument || bar.source_sequence == 0
        || bar.open.units <= 0 || bar.high.units <= 0 || bar.low.units <= 0
        || bar.close.units <= 0 || bar.volume < 0
        || bar.low.units > std::min(bar.open.units, bar.close.units)
        || std::max(bar.open.units, bar.close.units) > bar.high.units)
        throw std::invalid_argument("Invalid normalized historical bar");
    constexpr auto interval = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::seconds{60}).count();
    const auto start = bar.start.time_since_epoch().count();
    if (start > std::numeric_limits<std::int64_t>::max() - interval)
        throw std::overflow_error("Historical bar end overflows EventTime");
    if (bar.end != bar.start + std::chrono::nanoseconds{interval})
        throw std::invalid_argument("requires one-minute bar intervals");
}
inline std::uint64_t checkedNext(std::uint64_t value) {
    if (value == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("Run counter exhausted");
    return value + 1;
}

inline bool validInstrument(const Instrument& instrument) {
        return instrument.id != 0 && !instrument.symbol.empty()
            && instrument.currency == Currency::USD
            && instrument.price_scale == usd_scale
            && instrument.whole_share_step == 1;
    }

inline bool supportedInstrument(const Instrument& instrument){
    return instrument.id != 0 && !instrument.symbol.empty()
        && instrument.currency ==Currency::USD
        && instrument.price_scale == usd_scale
        && instrument.whole_share_step ==1;
}
} // namespace trading_app_cli
