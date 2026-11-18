#include "LiveBarBuilder.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace trading_app_cli::live {
LiveBarBuilder::LiveBarBuilder(std::chrono::milliseconds grace) : grace_(grace) {
    if (grace.count() < 0) throw std::invalid_argument("negative bar grace");
}
bool LiveBarBuilder::add(const Trade& t, std::uint64_t ingest) {
    if (t.time < watermark_ || t.time < skip_until_) { ++late_; return false; }
    if (t.price.units <= 0 || t.quantity_atoms <= 0) throw std::invalid_argument("invalid trade");
    auto start = std::chrono::floor<std::chrono::minutes>(t.time);
    if (!pending_.contains(start) && pending_.size() >= 4) throw std::runtime_error("bar window exceeded");
    auto& p = pending_[start];
    auto key = std::tuple{t.time,ingest};
    if (!p.count) {
        p.bar = Bar{start,start+std::chrono::minutes(1),t.price,t.price,t.price,t.price,0,0};
        p.first = p.last = key;
    }
    if (p.count >= 1'000'000) throw std::runtime_error("bar trade count limit");
    if (t.quantity_atoms > std::numeric_limits<std::int64_t>::max() - p.bar.volume_atoms)
        throw std::overflow_error("bar volume overflow");
    p.bar.volume_atoms += t.quantity_atoms;
    p.bar.high.units = std::max(p.bar.high.units,t.price.units);
    p.bar.low.units = std::min(p.bar.low.units,t.price.units);
    if (key < p.first) { p.first=key; p.bar.open=t.price; }
    if (key >= p.last) { p.last=key; p.bar.close=t.price; }
    ++p.count; return true;
}
std::vector<Bar> LiveBarBuilder::advance(EventTime now) {
    // Advancing to complete UTC minute boundaries also seals empty intervals.
    watermark_ = std::max(watermark_,EventTime{std::chrono::floor<std::chrono::minutes>(now-grace_)});
    std::vector<Bar> out;
    while (!pending_.empty() && pending_.begin()->second.bar.end <= watermark_) {
        auto b=pending_.begin()->second.bar; b.sequence=++sequence_;
        out.push_back(b); pending_.erase(pending_.begin());
    }
    return out;
}
void LiveBarBuilder::invalidate(EventTime now) {
    pending_.clear(); skip_until_=std::chrono::floor<std::chrono::minutes>(now)+std::chrono::minutes(1);
    watermark_=std::max(watermark_,skip_until_);
}
std::optional<Bar> LiveBarBuilder::forming() const {
    if (pending_.empty()) return std::nullopt;
    return pending_.rbegin()->second.bar;
}
}
