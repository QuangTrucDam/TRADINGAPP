#include "TradingAPI.hpp"

#include <stdexcept>
#include <utility>


namespace trading_app_desktop {

std::shared_ptr<ViewData> loadOrRun(RunConfiguration config, bool execute,
    const std::shared_ptr<JobProgress>& progress) {
    auto data = std::make_shared<ViewData>();
    try {
        if (execute) {
            // Fresh family, strategy and account for every press of Run.
            auto components = makeSimulationFactory(config.simulation)->create();
            RecordingFeed feed(*components.feed, *data);
            auto strategy = createStrategy(config.strategy, config.simulation.instrument);
            config.close_observation_capacity = 0; // avoid duplicating retained snapshots
            TradingEngine engine(config, std::move(components.fill_model), std::move(strategy),
                [&](const CloseObservation& observation) {
                    data->accounts.push_back(observation);
                    progress->bars.store(data->accounts.size(), std::memory_order_relaxed);
                });
            data->run = engine.run(feed);
        } else {
            HistoricalFeedConfiguration feedConfig;
            feedConfig.instrument = config.simulation.instrument;
            feedConfig.source_timezone = config.simulation.source_timezone;
            HistoricalFeed feed(config.simulation.dataset_path, feedConfig);
            for (;;) {
                const auto read = feed.nextBar();
                if (read.status == BarReadStatus::EndOfData) break;
                if (!read.bar) throw std::runtime_error("Feed returned no bar payload");
                if (data->bars.size() >= maximum_display_bars)
                    throw std::runtime_error("Dataset exceeds the 100,000-bar desktop limit. Choose a smaller dataset.");
                data->bars.push_back(*read.bar);
                progress->bars.store(data->bars.size(), std::memory_order_relaxed);
            }
            if (data->bars.empty()) throw std::runtime_error("Dataset is empty");
        }
    } catch (const std::exception& error) {
        data->error = error.what();
        data->bars.clear(); data->accounts.clear(); data->run.reset();
    } catch (...) {
        data->error = "Unknown historical job failure";
        data->bars.clear(); data->accounts.clear(); data->run.reset();
    }
    return data;
}
}
