#include "strategy/StrategyFactory.hpp"
#include "strategy/DoNothingStrategy.hpp"
#include "strategy/ScriptedStrategy.hpp"
#include "strategy/MomentumStrategy.hpp"
#include "strategy/VNMomentumStrategy.hpp"
#include "core/Validation.hpp"
#include <limits>
#include <cmath>
#include <deque>
#include <stdexcept>
namespace trading_app_cli {
void validateStrategyConfiguration(const StrategyConfiguration& config, const Instrument& instrument) {
    validateInstrument(instrument);
    if (config.strategy_id.empty()) throw std::invalid_argument("Strategy ID must not be empty");
    switch (config.kind) {
    case StrategyKind::DoNothing: break;
    case StrategyKind::Scripted:
        for (const auto& decision : config.decisions)
            if (decision.completed_bar_index == 0 || decision.quantity <= 0
                || (decision.side != Side::Buy && decision.side != Side::Sell))
                throw std::invalid_argument("Invalid scripted decision");
        break;
    case StrategyKind::Momentum:
        if (config.lookback == 0 || config.buy_quantity <= 0
            || config.lookback == std::numeric_limits<std::size_t>::max()
            || config.lookback + 1 > std::deque<Price>{}.max_size())
            throw std::invalid_argument("Invalid momentum configuration");
        break;
    case StrategyKind::VNMomentum:
        if (config.lookback < 2 || config.buy_quantity <= 0
            || config.lookback == std::numeric_limits<std::size_t>::max()
            || config.lookback + 1 > std::deque<Price>{}.max_size()
            || !std::isfinite(config.threshold) || config.threshold < 0.0
            || !std::isfinite(config.volFloor) || config.volFloor <= 0.0)
            throw std::invalid_argument("Invalid risk-adjusted momentum configuration");
        break;
    default: throw std::invalid_argument("Unknown strategy kind");
    }
}
std::unique_ptr<ITradingModel> createStrategy(const StrategyConfiguration& config, const Instrument& instrument) {
    validateStrategyConfiguration(config, instrument);
    switch (config.kind) {
    case StrategyKind::DoNothing: 
        return std::make_unique<DoNothingStrategy>();
    case StrategyKind::Scripted:
        return std::make_unique<ScriptedStrategy>(instrument, config.strategy_id, config.decisions);
    case StrategyKind::Momentum:
        return std::make_unique<MomentumStrategy>(instrument, config.strategy_id, config.lookback, config.buy_quantity);
    case StrategyKind::VNMomentum:
        return std::make_unique<VNMomentumStrategy>(instrument, config.strategy_id, config.lookback, config.buy_quantity,config.threshold, config.volFloor);
    default: throw std::invalid_argument("Unknown strategy kind");
    }
}
} // namespace trading_app_cli
