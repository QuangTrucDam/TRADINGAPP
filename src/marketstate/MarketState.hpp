#pragma once
#include "core/Types.hpp"
#include <deque>
namespace trading_app_cli {
class MarketState final {
public:
    MarketState(Instrument instrument, std::size_t capacity);
    void onCompletedBar(const BarEvent& bar, EventTime publishedAt);
    std::optional<Price> lastClose(InstrumentId instrument) const;
    const std::deque<BarEvent>& history() const noexcept;
private:
    Instrument instrument_;
    std::size_t capacity_;
    std::deque<BarEvent> history_;
    std::optional<EventTime> last_published_at_;
};
} // namespace trading_app_cli
