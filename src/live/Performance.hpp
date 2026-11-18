#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace trading_app_cli::live {
struct TimingSummary {
    std::uint64_t count{}, retained{};
    double p50_us{}, p95_us{}, maximum_us{};
    std::optional<double> p99_us, p999_us;
};
class TimingSeries {
  public:
    void add(std::int64_t ns) {
        if (ns < 0)
            return;
        if (values_.size() < 100000)
            values_.push_back(ns);
        else
            values_[count_ % values_.size()] = ns;
        ++count_;
        maximum_ = std::max(maximum_, ns);
    }
    TimingSummary summary() const {
        TimingSummary s;
        s.count = count_;
        s.retained = values_.size();
        s.maximum_us = maximum_ / 1000.;
        if (values_.empty())
            return s;
        auto sorted = values_;
        std::sort(sorted.begin(), sorted.end());
        auto percentile = [&](double p) {
            return sorted[static_cast<std::size_t>((sorted.size() - 1) * p)] / 1000.;
        };
        s.p50_us = percentile(.5);
        s.p95_us = percentile(.95);
        if (sorted.size() >= 1000)
            s.p99_us = percentile(.99);
        if (sorted.size() >= 10000)
            s.p999_us = percentile(.999);
        return s;
    }

  private:
    std::vector<std::int64_t> values_;
    std::uint64_t count_{};
    std::int64_t maximum_{};
};
struct PerformanceSnapshot {
    TimingSummary queue_wait, core_processing, receipt_to_commit, snapshot_age, decision_to_submit;
    double duration_seconds{}, events_per_second{};
    std::uint64_t queue_high{}, recorder_high{}, queue_depth{}, recorder_depth{}, input_overflows{},
        recorder_overflows{};
};
} // namespace trading_app_cli::live
