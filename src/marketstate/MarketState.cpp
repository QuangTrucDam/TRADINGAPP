#include "marketstate/MarketState.hpp"
#include "core/Validation.hpp"
#include <stdexcept>
#include <utility>
namespace trading_app_cli {
MarketState::MarketState(Instrument instrument, std::size_t capacity)
    : instrument_(std::move(instrument)), capacity_(capacity) {
    validateInstrument(instrument_);
    if (capacity_ == 0 || capacity_ > history_.max_size())
        throw std::invalid_argument("Invalid market-history capacity");
}
void MarketState::onCompletedBar(const BarEvent& bar, EventTime publishedAt) {
    validateHistoricalBar(bar, instrument_);
    if (publishedAt < bar.end) throw std::invalid_argument("Cannot publish an incomplete bar");
    if (last_published_at_ && publishedAt < *last_published_at_)
        throw std::invalid_argument("Publication time cannot move backward");
    if (!history_.empty() && (bar.start < history_.back().end
        || bar.source_sequence <= history_.back().source_sequence))
        throw std::invalid_argument("Duplicate/backward market publication");
    history_.push_back(bar);
    if (history_.size() > capacity_) history_.pop_front();
    last_published_at_ = publishedAt;
}
std::optional<Price> MarketState::lastClose(InstrumentId instrument) const {
    if (instrument != instrument_.id || history_.empty()) return std::nullopt;
    return history_.back().close;
}
const std::deque<BarEvent>& MarketState::history() const noexcept { return history_; }
} // namespace trading_app_cli
