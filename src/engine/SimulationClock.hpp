#pragma once
#include "engine/IClock.hpp"
namespace trading_app_cli {
class SimulationClock final : public IClock {
public:
    void initialize(EventTime time);
    void advanceTo(EventTime time);
    EventTime now() const override;
    bool initialized() const noexcept;
private:
    std::optional<EventTime> time_;
};
} // namespace trading_app_cli