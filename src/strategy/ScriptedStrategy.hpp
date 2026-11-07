#pragma once
#include "strategy/ITradingModel.hpp"
namespace trading_app_cli {
class ScriptedStrategy final : public ITradingModel {
public:
    ScriptedStrategy(Instrument instrument, StrategyId id,
        std::vector<ScriptedDecision> decisions);
    std::vector<OrderRequest> onBar(const BarEvent&, const PortfolioSnapshot&,
        const PendingOrderView&) override;
    void reset() override;
private:
    Instrument instrument_;
    StrategyId id_;
    std::vector<ScriptedDecision> decisions_;
    std::uint64_t completed_bars_{};
    std::size_t cursor_{};
    OrderRequest makeRequest(const ScriptedDecision&) const;
};
} // namespace trading_app_cli
