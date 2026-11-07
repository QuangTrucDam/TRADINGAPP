#include "strategy/MomentumStrategy.hpp"
#include "core/Validation.hpp"
#include <limits>
#include <stdexcept>
#include <utility>
namespace trading_app_cli {
MomentumStrategy::MomentumStrategy(Instrument instrument, StrategyId id,
    std::size_t lookback, Quantity buyQuantity)
    : instrument_(std::move(instrument)), id_(std::move(id)),
      lookback_(lookback), buy_quantity_(buyQuantity) {
    validateInstrument(instrument_);
    if (id_.empty() || lookback_ == 0 || buy_quantity_ <= 0
        || lookback_ == std::numeric_limits<std::size_t>::max()
        || lookback_ + 1 > closes_.max_size())
        throw std::invalid_argument("Invalid momentum configuration");
}
OrderRequest MomentumStrategy::makeRequest(Side side, Quantity quantity) const {
    return {instrument_, id_, side, quantity, OrderType::Market};
}
std::vector<OrderRequest> MomentumStrategy::onBar(const BarEvent& bar,
    const PortfolioSnapshot& portfolio, const PendingOrderView& pending) {
    validateHistoricalBar(bar, instrument_);
    closes_.push_back(bar.close);
    if (closes_.size() > lookback_ + 1) closes_.pop_front();
    if (closes_.size() < lookback_ + 1 || pending.hasPending(instrument_.id)) return {};
    Quantity held = 0;
    for (const auto& position : portfolio.positions) {
        if (position.instrument.id == instrument_.id) {
            if (position.instrument != instrument_ || position.quantity < 0)
                throw std::logic_error("Invalid strategy portfolio observation");
            held = position.quantity;
            break;
        }
    }
    if (closes_.back().units > closes_.front().units && held == 0)
        return {makeRequest(Side::Buy, buy_quantity_)};
    if (closes_.back().units < closes_.front().units && held > 0)
        return {makeRequest(Side::Sell, held)};
    return {};
}
void MomentumStrategy::reset() { closes_.clear(); }
} // namespace trading_app_cli
