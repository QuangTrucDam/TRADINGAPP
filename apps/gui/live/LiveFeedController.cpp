#include "LiveFeedController.hpp"
#include "marketdata/live/LiveConfiguration.hpp"
#include <stdexcept>
namespace trading_app_desktop {
using namespace trading_app_cli::live;
void LiveFeedController::connectFeed(const QString &symbol, bool enable_bidask) {
    if (busy())
        throw std::logic_error("Previous connection is still active/cleaning up");
    auto env = readDotEnv(".env");
    Configuration c;
    c.symbol = symbol.toStdString();
    if (c.symbol != "BTCUSDT") {
        c.provider = Provider::Finnhub;
        c.api_key = configuredValue(env, "FINNHUB_API_KEY");
        c.entitlement = configuredValue(env, "FINNHUB_ENTITLEMENT");
        if (c.entitlement.empty())
            c.entitlement = "Entitlement/delay unverified; confirm locally before paper trading";
        c.bidask_enabled = enable_bidask || configuredValue(env, "FINNHUB_BIDASK_ENABLED") == "true";
        auto polling = configuredValue(env, "FINNHUB_BIDASK_POLL_MS");
        if (!polling.empty()) {
            try {
                c.bidask_poll_ms = std::stoi(polling);
            } catch (...) {
                throw std::invalid_argument("Invalid FINNHUB_BIDASK_POLL_MS");
            }
        }
    }
    auto dir = configuredValue(env, "LIVE_RECORDING_DIR");
    if (!dir.empty())
        c.recording_directory = dir;
    pending_command_ = 0;
    last_ = {};
    pipeline_ = std::make_unique<LivePipeline>(std::move(c));
    if (period_ != 14)
        pipeline_->setIndicatorPeriod(period_);
}
void LiveFeedController::connectDemo() {
    if (busy())
        throw std::logic_error("Disconnect current feed before starting demo");
    Configuration c;
    c.provider = Provider::Finnhub;
    c.symbol = "AAPL";
    c.synthetic = true;
    c.entitlement = "SYNTHETIC DEMO — generated trades/quotes, accelerated clock, scripted paper orders";
    auto env = readDotEnv(".env");
    auto dir = configuredValue(env, "LIVE_RECORDING_DIR");
    if (!dir.empty())
        c.recording_directory = dir;
    pending_command_ = 0;
    last_ = {};
    pipeline_ = std::make_unique<LivePipeline>(std::move(c));
}
void LiveFeedController::disconnectFeed() {
    if (pipeline_)
        pipeline_->requestStop();
}
void LiveFeedController::setIndicatorPeriod(std::size_t n) {
    period_ = n;
    if (pipeline_)
        pipeline_->setIndicatorPeriod(n);
}
void LiveFeedController::startPaper(SessionConfiguration c) {
    if (!pipeline_)
        throw std::logic_error("Connect a feed first");
    if (pendingCommand())
        throw std::logic_error("Previous session command pending");
    pending_command_ = pipeline_->startPaper(std::move(c));
}
void LiveFeedController::stopPaper() {
    if (pipeline_)
        pending_command_ = pipeline_->stopPaper();
}
Snapshot LiveFeedController::poll() {
    if (pipeline_) {
        last_ = pipeline_->snapshot();
        if (pipeline_->finished()) {
            pipeline_.reset();
            pending_command_ = 0;
        }
    }
    return last_;
}
} // namespace trading_app_desktop
