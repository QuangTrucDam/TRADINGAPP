#pragma once
#include "strategy/ITradingModel.hpp"
#include <deque>
namespace trading_app_cli {
class VNMomentumStrategy final : public ITradingModel { //volatility-normalized momentum
public:
    VNMomentumStrategy(Instrument instrument, StrategyId id,
        std::size_t lookback, Quantity buyQuantity, double threshold, double volFloor);
    std::vector<OrderRequest> onBar(const BarEvent&, const PortfolioSnapshot&,
        const PendingOrderView&) override;
    void reset() override;
private:
    Instrument instrument_;
    StrategyId id_;
    std::size_t lookback_;
    Quantity buy_quantity_;
    double  threshold_;         // θ
    double  vol_floor_;         // ε
    std::deque<Price> closes_;
    OrderRequest makeRequest(Side side, Quantity quantity) const;
    // double computeScore(std::deque<Price>& closes, std::size_t lookback, double volFloor) const;     // returns S_t = R_t / σ_t
};
} // namespace trading_app_cli