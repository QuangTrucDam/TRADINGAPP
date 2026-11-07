#include "marketdata/HistoricalFeed.hpp"

#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace trading_app_cli {
namespace {

std::string inferSymbol(const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    constexpr std::string_view suffix = "_1min.csv";
    if (!filename.ends_with(suffix) || filename.size() <= suffix.size())
        throw std::invalid_argument("filename must be <symbol>_1min.csv");
    return filename.substr(0, filename.size() - suffix.size());
}

HistoricalFeedConfiguration validateConfiguration(
    const std::filesystem::path& path, HistoricalFeedConfiguration config) {
    try {
        const auto& instrument = config.instrument;
        if (instrument.id == 0 || instrument.symbol.empty()
            || instrument.currency != Currency::USD
            || instrument.price_scale != usd_scale || instrument.whole_share_step != 1)
            throw std::invalid_argument("instrument requires nonzero ID, symbol, USD, scale 10000 and share step 1");
        if (config.bar_interval != std::chrono::seconds{60})
            throw std::invalid_argument("bar interval must be exactly 60 seconds");
        if (config.source_timezone.empty())
            throw std::invalid_argument("source timezone is required");
        if (inferSymbol(path) != instrument.symbol)
            throw std::invalid_argument("filename symbol does not match configured instrument");
        if (config.session_coverage.empty()) config.session_coverage = "unknown";
        if (config.price_adjustment_assumption.empty()) config.price_adjustment_assumption = "unknown";
        return config;
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument("Historical CSV '" + path.string() + "': " + error.what());
    }
}

const date::time_zone* resolveZone(const std::filesystem::path& path,
                                         const std::string& name) {
    try {
        // Resolve once. Do not reload the tzdb or consult the machine's current zone.
        return date::locate_zone(name);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error("Historical CSV '" + path.string()
            + "': cannot resolve source timezone '" + name + "': " + error.what());
    }
}

EventTime checkedBarEnd(EventTime start) {
    constexpr auto duration = std::chrono::duration_cast<EventTime::duration>(
        std::chrono::seconds{60});
    if (start.time_since_epoch().count()
        > std::numeric_limits<EventTime::duration::rep>::max() - duration.count())
        throw std::overflow_error("bar end exceeds EventTime range");
    return start + duration;
}

bool validateInterval(const BarEvent& bar, std::optional<EventTime> previousEnd) {
    if (!previousEnd) return false;
    if (bar.start < *previousEnd)
        throw std::invalid_argument("duplicate, overlapping or out-of-order UTC interval");
    return bar.start > *previousEnd;
}

FeedStatistics recordBar(const FeedStatistics& current, const BarEvent& bar, bool gap) {
    FeedStatistics next = current;
    const auto limit = std::numeric_limits<std::uint64_t>::max();
    if (next.bars_read == limit)
        throw std::overflow_error("returned bar counter overflow");
    if (gap && next.gaps == limit)
        throw std::overflow_error("gap counter overflow");
    ++next.bars_read;
    if (bar.source_sequence != next.bars_read)
        throw std::logic_error("unexpected raw source sequence");
    if (gap) ++next.gaps;
    if (!next.first_start) next.first_start = bar.start;
    next.last_end = bar.end;
    return next;
}

} // namespace

HistoricalFeed::HistoricalFeed(const std::filesystem::path& path,
                               HistoricalFeedConfiguration configuration)
    : path_(path),
      configuration_(validateConfiguration(path, std::move(configuration))),
      zone_(resolveZone(path, configuration_.source_timezone)), reader_(path) {}

std::string HistoricalFeed::errorContext(std::size_t lineNumber) const {
    return "Historical CSV '" + path_.string() + "', line "
        + std::to_string(lineNumber) + ": ";
}

EventTime HistoricalFeed::toUtc(SourceLocalTime localStart) const {
    // std::chrono and date use distinct local_clock tags.
    // Preserve civil seconds while bridging only at the timezone boundary.
    const date::local_seconds local{localStart.time_since_epoch()};
    const auto info = zone_->get_info(local);
    if (info.result == date::local_info::ambiguous)
        throw std::invalid_argument("DateTime: ambiguous local time in '"
            + configuration_.source_timezone + "'");
    if (info.result == date::local_info::nonexistent)
        throw std::invalid_argument("DateTime: nonexistent local time in '"
            + configuration_.source_timezone + "'");

    const auto seconds = zone_->to_sys(local).time_since_epoch().count();
    constexpr std::int64_t nanoseconds_per_second = 1'000'000'000;
    using Rep = EventTime::duration::rep;
    constexpr auto minimum = std::numeric_limits<Rep>::min();
    constexpr auto maximum = std::numeric_limits<Rep>::max();
    // Division truncates toward zero: these are the safe whole-second endpoints.
    if (seconds < minimum / nanoseconds_per_second
        || seconds > maximum / nanoseconds_per_second)
        throw std::overflow_error("UTC start exceeds EventTime nanosecond range");
    return EventTime{EventTime::duration{seconds * nanoseconds_per_second}};
}

BarEvent HistoricalFeed::normalize(const RawBarRow& row) const {
    BarEvent bar;
    bar.instrument = configuration_.instrument;
    bar.start = toUtc(row.local_start);
    bar.end = checkedBarEnd(bar.start);
    bar.open = row.open;
    bar.high = row.high;
    bar.low = row.low;
    bar.close = row.close;
    bar.volume = row.volume;
    bar.source_sequence = row.source_sequence;
    return bar;
}


MarketDataReadResult HistoricalFeed::nextEvent() {
    if (statistics_.failed)
        throw std::runtime_error(errorContext(reader_.lineNumber())
            + "feed is failed; construct a fresh feed");
    if (statistics_.end_of_data) return {};

    // Reader exceptions already contain path and physical line information.
    RawBarReadResult raw;
    try {
        raw = reader_.next();
    } catch (...) {
        statistics_.failed = true;
        throw;
    }
    if (raw.status == BarReadStatus::EndOfData) {
        statistics_.end_of_data = true;
        return {};
    }

    try {
        if (raw.status != BarReadStatus::Bar || !raw.row)
            throw std::logic_error("invalid raw-reader result");
        MarketDataReadResult result{
            MarketDataReadStatus::Event, MarketDataEvent{normalize(*raw.row)}};
        const auto& bar = std::get<BarEvent>(*result.event);
        const bool gap = validateInterval(bar, statistics_.last_end);
        const FeedStatistics next = recordBar(statistics_, bar, gap);
        // Prepare the complete result before committing prefix statistics.
        // Returning the value uses only noexcept moves of these standard members.
        statistics_ = next;
        return result;
    } catch (const std::invalid_argument& error) {
        statistics_.failed = true;
        throw std::invalid_argument(errorContext(reader_.lineNumber()) + error.what());
    } catch (const std::overflow_error& error) {
        statistics_.failed = true;
        throw std::overflow_error(errorContext(reader_.lineNumber()) + error.what());
    } catch (const std::logic_error& error) {
        statistics_.failed = true;
        throw std::logic_error(errorContext(reader_.lineNumber()) + error.what());
    } catch (const std::runtime_error& error) {
        statistics_.failed = true;
        throw std::runtime_error(errorContext(reader_.lineNumber()) + error.what());
    } catch (...) {
        statistics_.failed = true;
        throw;
    }
}
BarReadResult HistoricalFeed::nextBar() {
    auto result = nextEvent();
    if (result.status == MarketDataReadStatus::EndOfData) {
        if (result.event)
            throw std::logic_error("HistoricalFeed: EOF contains an event");
        return {};
    }
    if (result.status != MarketDataReadStatus::Event || !result.event)
        throw std::logic_error("HistoricalFeed: invalid event result");
    auto* bar = std::get_if<BarEvent>(&*result.event);
    if (!bar)
        throw std::logic_error("HistoricalFeed: expected a BarEvent");
    return {BarReadStatus::Bar, std::move(*bar)};
}

const FeedStatistics& HistoricalFeed::statistics() const noexcept {
    return statistics_;
}

const HistoricalFeedConfiguration& HistoricalFeed::configuration() const noexcept {
    return configuration_;
}

} // namespace trading_app_cli
