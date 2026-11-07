#pragma once

#include "marketdata/IMarketDataFeed.hpp"
#include "storage/CsvReader.hpp"

#include "date/tz.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace trading_app_cli {

class HistoricalFeed final : public IMarketDataFeed {
public:
    HistoricalFeed(const std::filesystem::path& path,
                   HistoricalFeedConfiguration configuration);

    HistoricalFeed(const HistoricalFeed&) = delete;
    HistoricalFeed& operator=(const HistoricalFeed&) = delete;
    HistoricalFeed(HistoricalFeed&&) = delete;
    HistoricalFeed& operator=(HistoricalFeed&&) = delete;

    BarReadResult nextBar() override;
    MarketDataReadResult nextEvent() override;
    const FeedStatistics& statistics() const noexcept;
    const HistoricalFeedConfiguration& configuration() const noexcept;

private:
    EventTime toUtc(SourceLocalTime localStart) const;
    BarEvent normalize(const RawBarRow& row) const;
    std::string errorContext(std::size_t lineNumber) const;

    // Declaration order ensures configuration/zone checks precede file opening.
    std::filesystem::path path_;
    HistoricalFeedConfiguration configuration_;
    const date::time_zone* zone_; // borrowed from the process tzdb
    CsvReader reader_;
    FeedStatistics statistics_{};
};

} // namespace trading_app_cli
