#pragma once
#include "TradingAPI.hpp"
#include <QWidget>
#include <QString>
class QMouseEvent;
class QWheelEvent;
namespace trading_app_desktop {
class MarketChart final : public QWidget {
public:
    explicit MarketChart(QWidget* parent = nullptr);
    void setData(std::shared_ptr<const ViewData> data);
    void resetView();
    void setEmptyMessage(const QString& message);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    std::shared_ptr<const ViewData> data_;
    double start_{0};
    double end_{1};
    int hoverX_{-1};
    bool dragging_{false};
    double previousX_{0};
    QString emptyMessage_{"Choose a data directory, then Load History or Run Strategy"};
};
}
