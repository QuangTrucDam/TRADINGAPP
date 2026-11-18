#pragma once
#include "marketdata/live/LiveTypes.hpp"
#include <QWidget>
namespace trading_app_desktop {
class LiveMarketChart final : public QWidget {
  public:
    explicit LiveMarketChart(QWidget *parent = nullptr) : QWidget(parent) { setMinimumSize(450, 280); }
    void setSnapshot(const trading_app_cli::live::Snapshot &snapshot);

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    std::vector<trading_app_cli::live::Bar> bars_;
    std::optional<trading_app_cli::live::Bar> forming_;
    QString unit_;
};
} // namespace trading_app_desktop
