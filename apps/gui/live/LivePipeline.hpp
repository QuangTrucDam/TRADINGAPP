#pragma once
#include "live/LiveProcessor.hpp"
#include "marketdata/live/LiveEventQueue.hpp"
#include "marketdata/live/LiveTypes.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
namespace trading_app_cli::live {
class LivePipeline {
  public:
    using StreamRunner =
        std::function<void(const Configuration &, LiveEventQueue<Envelope> &, std::atomic<bool> &,
                           std::atomic<bool> &, std::chrono::steady_clock::time_point)>;
    explicit LivePipeline(Configuration configuration, StreamRunner stream = {});
    ~LivePipeline();
    LivePipeline(const LivePipeline &) = delete;
    LivePipeline &operator=(const LivePipeline &) = delete;
    void requestStop() { stop_ = true; }
    bool finished() const { return network_done_ && processing_done_; }
    Snapshot snapshot() const;
    void setIndicatorPeriod(std::size_t);
    std::uint64_t startPaper(SessionConfiguration);
    std::uint64_t stopPaper();

  private:
    std::uint64_t command(Input);
    void network();
    void process();
    void publish(Snapshot);
    Configuration config_;
    StreamRunner stream_;
    LiveEventQueue<Envelope> input_;
    LiveEventQueue<Input> commands_{32}; // GUI is its sole producer
    std::uint64_t next_command_{};
    std::atomic<std::uint64_t> urgent_stop_command_{0};
    std::atomic<bool> stop_{false}, network_done_{false}, processing_done_{false}, overflow_{false};
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::thread network_, processor_;
    std::string session_;
    std::chrono::steady_clock::time_point origin_;
};
} // namespace trading_app_cli::live
