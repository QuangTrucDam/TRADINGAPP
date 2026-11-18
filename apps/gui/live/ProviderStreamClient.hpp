#pragma once
#include "marketdata/live/LiveEventQueue.hpp"
#include "marketdata/live/LiveTypes.hpp"
#include <atomic>
namespace trading_app_cli::live {
void runProviderStream(const Configuration &, LiveEventQueue<Envelope> &, std::atomic<bool> &stop,
                       std::atomic<bool> &overflow, std::chrono::steady_clock::time_point origin,
                       const std::string &loopback_test_endpoint = {});
}
