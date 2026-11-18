#pragma once
#include "LiveTypes.hpp"
#include <map>
#include <tuple>

namespace trading_app_cli::live {
class LiveBarBuilder {
public:
    explicit LiveBarBuilder(std::chrono::milliseconds grace = std::chrono::milliseconds(2000));
    bool add(const Trade& trade, std::uint64_t ingest);
    std::vector<Bar> advance(EventTime now);
    void invalidate(EventTime now); // discard partial bars and skip current minute
    std::optional<Bar> forming() const;
    std::uint64_t late() const { return late_; }
private:
    struct Pending {
        Bar bar;
        std::tuple<EventTime,std::uint64_t> first, last;
        std::size_t count{};
    };
    std::map<EventTime,Pending> pending_;
    std::chrono::milliseconds grace_;
    EventTime watermark_{}, skip_until_{};
    std::uint64_t sequence_{}, late_{};
};
}
