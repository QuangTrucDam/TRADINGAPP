#include "simulation/SimulationFactory.hpp"
#include "simulation/HistoricalSimulationFactory.hpp"
#include <stdexcept>

namespace trading_app_cli {
std::unique_ptr<ISimulationFactory> makeSimulationFactory(
    const SimulationConfiguration& configuration)
{
    switch (configuration.mode) {
    case SimulationMode::HistoricalReplay:
        return std::make_unique<HistoricalSimulationFactory>(configuration);
    case SimulationMode::LivePaper:
        throw std::invalid_argument("LivePaper components are scheduled for M3");
    default:
        throw std::invalid_argument("Unknown simulation mode");
    }
}
} // namespace trading_app_cli
