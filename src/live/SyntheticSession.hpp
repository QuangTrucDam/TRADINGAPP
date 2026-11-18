#pragma once
#include "LiveProcessor.hpp"
namespace trading_app_cli::live {
std::vector<Input> syntheticSession(std::size_t minutes = 60);
struct FaultResult {
    std::string scenario, expected;
    bool passed{};
};
std::vector<FaultResult> runFaultScenarios();
} // namespace trading_app_cli::live
