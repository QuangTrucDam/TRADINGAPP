#pragma once
#include "simulation/ISimulationFactory.hpp"

namespace trading_app_cli {

std::unique_ptr<ISimulationFactory> makeSimulationFactory(
    const SimulationConfiguration& configuration);
} // namespace trading_app_cli
