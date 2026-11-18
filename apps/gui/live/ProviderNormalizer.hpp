#pragma once
#include "marketdata/live/LiveTypes.hpp"
#include <QString>
namespace trading_app_cli::live {
struct DecodeResult {
    std::vector<Trade> trades;
    std::vector<LiveQuote> quotes;
    bool quote_unavailable{};
    std::vector<std::string> reasons;
    bool heartbeat{}, acknowledgement{};
    bool fatal{};
};
// Parse prices/volumes without silently rounding unsupported precision.
std::int64_t decimalAtoms(const QString &text, int places);
DecodeResult decode(Provider provider, const std::string &symbol, const std::string &raw, EventTime receipt);
} // namespace trading_app_cli::live
