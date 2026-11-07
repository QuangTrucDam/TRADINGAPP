#pragma once
#include "strategy/ITradingModel.hpp"
#include <deque>
namespace trading_app_cli {
class MomentumStrategy final : public ITradingModel {
public:
    MomentumStrategy(Instrument instrument, StrategyId id,
        std::size_t lookback, Quantity buyQuantity);
    std::vector<OrderRequest> onBar(const BarEvent&, const PortfolioSnapshot&,
        const PendingOrderView&) override;
    void reset() override;
private:
    Instrument instrument_;
    StrategyId id_;
    std::size_t lookback_;
    Quantity buy_quantity_;
    std::deque<Price> closes_;
    OrderRequest makeRequest(Side side, Quantity quantity) const;
};
} // namespace trading_app_cli
