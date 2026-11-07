#pragma once
#include "core/Types.hpp"
#include "execution/IFillModel.hpp"
#include "marketdata/IMarketDataFeed.hpp"
#include <memory>

namespace trading_app_cli {
struct SimulationComponents {
    std::unique_ptr<IMarketDataFeed> feed;
    std::unique_ptr<IFillModel> fill_model;
};

// Abstract Factory:
class ISimulationFactory {
public:
    virtual ~ISimulationFactory() = default;
    virtual SimulationComponents create() const = 0;
};
} // namespace trading_app_cli
