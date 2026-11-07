#pragma once
#include "strategy/ITradingModel.hpp"
#include <memory>
namespace trading_app_cli {
void validateStrategyConfiguration(const StrategyConfiguration&, const Instrument&);
std::unique_ptr<ITradingModel> createStrategy(const StrategyConfiguration&, const Instrument&);
} // namespace trading_app_cli
