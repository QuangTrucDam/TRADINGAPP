#pragma once

#include "engine/TradingEngine.hpp"
#include "marketdata/HistoricalFeed.hpp"
#include "simulation/SimulationFactory.hpp"
#include "strategy/StrategyFactory.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace trading_app_desktop {
using namespace trading_app_cli;
// Immutable completed job, transferred once from background work to the GUI.
struct ViewData {
    std::vector<BarEvent> bars;
    std::vector<CloseObservation> accounts;
    std::optional<RunResult> run;
    std::string error;
};
struct JobProgress { std::atomic<std::uint64_t> bars{0}; };
inline constexpr std::size_t maximum_display_bars = 100000;
std::shared_ptr<ViewData> loadOrRun(RunConfiguration config,
    bool execute, const std::shared_ptr<JobProgress>& progress);

class RecordingFeed final : public IMarketDataFeed {
public:
    RecordingFeed(IMarketDataFeed& source, ViewData& data)
        : source_(source), data_(data) {}
    MarketDataReadResult nextEvent() override {
        auto read = source_.nextEvent();
        if (read.status == MarketDataReadStatus::Event && read.event) {
            const auto* bar = std::get_if<BarEvent>(&*read.event);
            if (!bar) throw std::runtime_error("Desktop replay requires historical bars");
            if (data_.bars.size() >= maximum_display_bars)
                throw std::runtime_error("Dataset exceeds the 100,000-bar desktop limit. Choose a smaller dataset.");
            data_.bars.push_back(*bar);
        }
        return read;
    }
private:
    IMarketDataFeed& source_;
    ViewData& data_;
};
}// namespace trading_app_desktop
