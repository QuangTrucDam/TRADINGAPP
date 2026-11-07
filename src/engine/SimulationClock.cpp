#include "engine/SimulationClock.hpp"
#include <stdexcept>
namespace trading_app_cli {
void SimulationClock::initialize(EventTime time) {
    if (time_) throw std::logic_error("Simulation clock already initialized");
    time_ = time;
}
void SimulationClock::advanceTo(EventTime time) {
    if (!time_) throw std::logic_error("Simulation clock not initialized");
    if (time < *time_) throw std::invalid_argument("Simulation time cannot move backward");
    time_ = time;
}
EventTime SimulationClock::now() const {
    if (!time_) throw std::logic_error("Simulation clock not initialized");
    return *time_;
}
bool SimulationClock::initialized() const noexcept { return time_.has_value(); }
} // namespace trading_app_cli
