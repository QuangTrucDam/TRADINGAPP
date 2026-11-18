#pragma once
#include "LivePipeline.hpp"
#include <QObject>
#include <memory>
namespace trading_app_desktop {
class LiveFeedController final : public QObject {
  public:
    explicit LiveFeedController(QObject *parent = nullptr) : QObject(parent) {}
    bool busy() const { return bool(pipeline_); }
    bool pendingCommand() const { return pending_command_ > last_.command_ack; }
    void connectFeed(const QString &symbol, bool enable_bidask = false);
    void connectDemo();
    void disconnectFeed();
    void setIndicatorPeriod(std::size_t);
    void startPaper(trading_app_cli::live::SessionConfiguration);
    void stopPaper();
    trading_app_cli::live::Snapshot poll();

  private:
    std::unique_ptr<trading_app_cli::live::LivePipeline> pipeline_;
    trading_app_cli::live::Snapshot last_;
    std::uint64_t pending_command_{};
    std::size_t period_{14};
};
} // namespace trading_app_desktop
