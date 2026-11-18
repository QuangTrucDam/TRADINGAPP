#pragma once
#include "marketdata/live/LiveEventQueue.hpp"
#include <atomic>
#include <filesystem>
#include <thread>
namespace trading_app_cli::live {
// One consumer worker owns this producer API. No raw/authentication frames logged.
class SessionRecorder {
public:
    explicit SessionRecorder(const std::filesystem::path& path,std::size_t limit=64*1024*1024);
    ~SessionRecorder();
    bool append(std::string line);
    void close();
    bool failed() const { return failed_.load(); }
    bool done() const { return done_.load(); }
    std::size_t highWater() const { return queue_.highWater(); }
    std::size_t depth() const { return queue_.depth(); }
    std::uint64_t overflows() const { return overflows_.load(); }
private:
    LiveEventQueue<std::string> queue_{4096};
    std::atomic<bool> failed_{false},done_{false};
    std::thread writer_;
    std::atomic<std::uint64_t> overflows_{0};
};
}
