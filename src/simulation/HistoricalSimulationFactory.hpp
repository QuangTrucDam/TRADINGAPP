#pragma once
#include "simulation/ISimulationFactory.hpp"

namespace trading_app_cli {
class HistoricalSimulationFactory final : public ISimulationFactory {
public:
    explicit HistoricalSimulationFactory(SimulationConfiguration configuration);
    SimulationComponents create() const override;
private:
    SimulationConfiguration configuration_;
};
} // namespace trading_app_cli
