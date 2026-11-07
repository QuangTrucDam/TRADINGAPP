#include "simulation/HistoricalSimulationFactory.hpp"
#include "execution/NextBarOpenFillModel.hpp"
#include "marketdata/HistoricalFeed.hpp"
#include <stdexcept>
#include <utility>

namespace trading_app_cli {
HistoricalSimulationFactory::HistoricalSimulationFactory(
    SimulationConfiguration configuration)
    : configuration_(std::move(configuration))
{
    if (configuration_.mode != SimulationMode::HistoricalReplay)
        throw std::invalid_argument("Historical factory requires HistoricalReplay mode");
    if (configuration_.initial_cash.units <= 0)
        throw std::invalid_argument("Initial simulation cash must be positive");
    
    const NextBarOpenFillModel validation(configuration_.execution);
}

SimulationComponents HistoricalSimulationFactory::create() const
{
    HistoricalFeedConfiguration feedConfiguration;
    feedConfiguration.instrument = configuration_.instrument;
    feedConfiguration.source_timezone = configuration_.source_timezone;
    feedConfiguration.session_coverage = configuration_.session_coverage;
    feedConfiguration.price_adjustment_assumption =
        configuration_.price_adjustment_assumption;

    SimulationComponents components;
    components.fill_model =
        std::make_unique<NextBarOpenFillModel>(configuration_.execution);
    components.feed = std::make_unique<HistoricalFeed>(
        configuration_.dataset_path, std::move(feedConfiguration));
    return components;
}
} // namespace trading_app_cli
