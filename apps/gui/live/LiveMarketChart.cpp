#include "LiveMarketChart.hpp"
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
namespace trading_app_desktop {
using namespace trading_app_cli;
void LiveMarketChart::setSnapshot(const live::Snapshot &s) {
    // Bounded display batch; this copy never feeds strategy calculations.
    bars_.assign(s.bars.begin() + static_cast<std::ptrdiff_t>(s.bars.size() > 120 ? s.bars.size() - 120 : 0),
                 s.bars.end());
    forming_ = s.forming;
    unit_ = QString::fromStdString(s.unit);
    update();
}
void LiveMarketChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#101922"));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#a9b8ca"));
    p.drawText(12, 22, "LIVE CLOSE / VOLUME · " + unit_ + " · UTC · last 120 completed bars");
    auto display = bars_;
    if (forming_)
        display.push_back(*forming_);
    if (display.empty()) {
        p.drawText(rect(), Qt::AlignCenter, "Waiting for observed trades\nFirst partial minute is excluded");
        return;
    }
    double low = display.front().low.units, high = display.front().high.units, vol = 1;
    for (const auto &b : display) {
        low = std::min(low, double(b.low.units));
        high = std::max(high, double(b.high.units));
        vol = std::max(vol, double(b.volume_atoms));
    }
    const double pad = std::max(1., (high - low) * .08);
    low -= pad;
    high += pad;
    const QRectF area(85, 45, width() - 110, height() * .60),
        volume(85, height() * .74, width() - 110, height() * .16);
    const auto begin = display.front().start, end = display.back().end;
    double duration = std::chrono::duration<double>(end - begin).count();
    auto x = [&](EventTime t) {
        return area.left() + std::chrono::duration<double>(t - begin).count() / duration * area.width();
    };
    auto y = [&](double price) { return area.bottom() - (price - low) / (high - low) * area.height(); };
    p.setPen(QColor("#2c3b4b"));
    p.drawRect(area);
    p.drawRect(volume);
    for (int i = 0; i < 5; ++i) {
        double value = low + (high - low) * i / 4.;
        p.setPen(QColor("#a9b8ca"));
        p.drawText(QRectF(0, y(value) - 9, 78, 18), Qt::AlignRight,
                   QString::number(value / usd_scale, 'f', 2));
    }
    QPainterPath line;
    bool first = true;
    for (std::size_t i = 0; i < display.size(); ++i) {
        const auto &b = display[i];
        bool forming = forming_ && i == display.size() - 1;
        double px = x(b.start + (b.end - b.start) / 2);
        if (forming) {
            p.fillRect(QRectF(x(b.start), area.top(), x(b.end) - x(b.start), area.height()),
                       QColor(220, 170, 50, 30));
            p.setPen(QColor("#e5b94b"));
            p.drawText(QPointF(std::max(area.left(), px - 55), area.top() + 16), "FORMING");
        }
        if (first || (i > 0 && display[i - 1].end < b.start)) {
            line.moveTo(px, y(b.close.units));
            first = false;
        } else
            line.lineTo(px, y(b.close.units));
        p.fillRect(QRectF(x(b.start) + 1, volume.bottom() - double(b.volume_atoms) / vol * volume.height(),
                          std::max(1., x(b.end) - x(b.start) - 2),
                          double(b.volume_atoms) / vol * volume.height()),
                   QColor(forming ? "#8a7136" : "#2d7e8c"));
    }
    p.setPen(QPen(QColor("#67d5d1"), 2));
    p.drawPath(line);
    p.setPen(QColor("#a9b8ca"));
    auto stamp = [](EventTime t) {
        return QDateTime::fromMSecsSinceEpoch(
                   std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count(),
                   Qt::UTC)
            .toString("MM-dd HH:mm");
    };
    p.drawText(QPointF(area.left(), height() - 12), stamp(begin));
    p.drawText(QRectF(area.left(), height() - 30, area.width(), 25), Qt::AlignRight, stamp(end));
    p.drawText(12, static_cast<int>(volume.top() + 14), QString::number(vol / live::quantity_scale, 'g', 6));
}
} // namespace trading_app_desktop
