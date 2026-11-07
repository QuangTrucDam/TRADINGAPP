#include "MarketChart.hpp"
#include "core/FixedPoint.hpp"
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

namespace trading_app_desktop {
using namespace trading_app_cli;
namespace {
double seconds(EventTime time) {
    return std::chrono::duration<double>(time.time_since_epoch()).count();
}
QString stamp(double value) {
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(value), Qt::UTC).toString("MM-dd HH:mm");
}
}
void MarketChart::setEmptyMessage(const QString& message) {
    emptyMessage_ = message;
    update();
}
MarketChart::MarketChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(450, 280); setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip("Wheel: zoom · drag: pan · hover: exact OHLCV · all chart times UTC");
}
void MarketChart::setData(std::shared_ptr<const ViewData> data) {
    data_ = std::move(data); resetView();
}
void MarketChart::resetView() {
    if (data_ && !data_->bars.empty()) {
        start_ = seconds(data_->bars.front().start);
        end_ = seconds(data_->bars.back().end);
    } else { start_ = 0; end_ = 1; }
    hoverX_ = -1; update();
}
void MarketChart::paintEvent(QPaintEvent*) {
    QPainter p(this); p.fillRect(rect(), QColor("#101922"));
    const QRectF area(72, 35, std::max(1, width() - 96), std::max(1, height() - 74));
    const QRectF price(area.left(), area.top(), area.width(), area.height() * .73);
    const QRectF volume(area.left(), price.bottom() + 26, area.width(), area.height() * .27 - 26);
    p.setPen(QColor("#2c3b4b")); p.drawRect(price); p.drawRect(volume);
    p.setPen(QColor("#a9b8ca"));
    p.drawText(10, 20, "CLOSE · USD"); p.drawText(10, static_cast<int>(volume.top() - 3), "VOLUME · shares");
    if (!data_ || data_->bars.empty()) {
        // p.drawText(price, Qt::AlignCenter, "Choose a data directory, then Load History or Run Strategy");
        p.drawText(
            price.adjusted(12, 12, -12, -12),
            Qt::AlignCenter | Qt::TextWordWrap,
            emptyMessage_);
        return;
    }
    const auto& bars = data_->bars;
    const auto first = std::lower_bound(bars.begin(), bars.end(), start_,
        [](const BarEvent& bar, double t) { return seconds(bar.end) < t; });
    const auto last = std::upper_bound(first, bars.end(), end_,
        [](double t, const BarEvent& bar) { return t < seconds(bar.end); });
    if (first == last) return;
    double low = static_cast<double>(first->close.units), high = low;
    for (auto i = first; i != last; ++i) {
        low = std::min(low, static_cast<double>(i->close.units));
        high = std::max(high, static_cast<double>(i->close.units));
    }
    const double padding = std::max((high - low) * .08, 1.0);
    low -= padding; high += padding;
    auto x = [&](double t) { return area.left() + (t - start_) / (end_ - start_) * area.width(); };
    auto y = [&](double units) { return price.bottom() - (units - low) / (high - low) * price.height(); };
    for (int i = 0; i <= 4; ++i) {
        const double fraction = i / 4.0;
        const double yy = price.bottom() - fraction * price.height();
        p.setPen(QColor("#243241")); p.drawLine(QPointF(price.left(), yy), QPointF(price.right(), yy));
        p.setPen(QColor("#a9b8ca"));
        p.drawText(QRectF(0, yy - 9, 65, 18), Qt::AlignRight | Qt::AlignVCenter,
            QString::number((low + fraction * (high - low)) / usd_scale, 'f', 2));
    }
    // Dense views sum minute volumes per horizontal pixel, without omitting bars.
    struct Bucket { Quantity volume{}; const BarEvent* first{}; const BarEvent* last{}; };
    std::vector<Bucket> buckets(static_cast<std::size_t>(std::ceil(area.width())) + 1);
    Quantity maxVolume = 0; bool overflow = false;
    for (auto i = first; i != last; ++i) {
        const auto pixel = static_cast<std::size_t>(std::clamp(x(seconds(i->end)) - area.left(), 0.0, area.width()));
        auto& bucket = buckets[pixel];
        if (!bucket.first) bucket.first = &*i;
        bucket.last = &*i;
        if (i->volume > std::numeric_limits<Quantity>::max() - bucket.volume) overflow = true;
        else bucket.volume += i->volume;
        maxVolume = std::max(maxVolume, bucket.volume);
    }
    p.save(); p.setClipRect(price); p.setRenderHint(QPainter::Antialiasing);
    QPainterPath line;
    auto lineFirst = first == bars.begin() ? first : first - 1;
    line.moveTo(x(seconds(lineFirst->end)), y(static_cast<double>(lineFirst->close.units)));
    for (auto i = lineFirst + 1; i != last; ++i)
        line.lineTo(x(seconds(i->end)), y(static_cast<double>(i->close.units)));
    p.setPen(QPen(QColor("#f5ce57"), 1.5)); p.drawPath(line);
    p.setClipRect(volume);
    if (!overflow) {
        p.setRenderHint(QPainter::Antialiasing, false);
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            const auto& b = buckets[i]; if (!b.first) continue;
            const auto color = b.last->close.units > b.first->open.units ? QColor("#45bc9c") :
                b.last->close.units < b.first->open.units ? QColor("#df7183") : QColor("#9aabbe");
            const double h = maxVolume == 0 ? 0 : static_cast<double>(b.volume) / static_cast<double>(maxVolume) * volume.height();
            const double w = std::clamp(area.width() * 40.0 / (end_ - start_), 1.0, 12.0);
            p.fillRect(QRectF(area.left() + static_cast<double>(i) - w / 2, volume.bottom() - h, w, h), color);
        }
    }
    p.setClipRect(area);
    if (hoverX_ >= area.left() && hoverX_ <= area.right()) {
        p.setPen(QPen(QColor("#91a3b8"), 1, Qt::DashLine));
        p.drawLine(QPointF(hoverX_, area.top()), QPointF(hoverX_, area.bottom()));
    }
    p.restore(); p.setPen(QColor("#a9b8ca"));
    p.drawText(QRectF(0, volume.top(), 65, 18), Qt::AlignRight, QString::number(maxVolume));
    p.drawText(QRectF(0, volume.bottom() - 18, 65, 18), Qt::AlignRight, "0");
    if (overflow) p.drawText(volume, Qt::AlignCenter, "Volume sum exceeds range; zoom in");
    p.drawText(QRectF(area.left(), area.bottom() + 6, area.width() / 2, 20), stamp(start_) + " UTC");
    p.drawText(QRectF(area.center().x(), area.bottom() + 6, area.width() / 2, 20), Qt::AlignRight, stamp(end_) + " UTC");
}
void MarketChart::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) { dragging_ = true; previousX_ = e->position().x(); }
}
void MarketChart::mouseReleaseEvent(QMouseEvent*) { dragging_ = false; }
void MarketChart::mouseMoveEvent(QMouseEvent* e) {
    if (!data_ || data_->bars.empty()) return;
    hoverX_ = static_cast<int>(e->position().x());
    if (dragging_) {
        const double shift = (previousX_ - e->position().x()) / std::max(1, width() - 96) * (end_ - start_);
        const double span = end_ - start_;
        start_ = std::clamp(start_ + shift, seconds(data_->bars.front().start), seconds(data_->bars.back().end) - span);
        end_ = start_ + span; previousX_ = e->position().x();
        QToolTip::hideText();
    } else {
        const double t = start_ + (e->position().x() - 72) / std::max(1, width() - 96) * (end_ - start_);
        auto i = std::lower_bound(data_->bars.begin(), data_->bars.end(), t,
            [](const BarEvent& bar, double time) { return seconds(bar.end) < time; });
        if (i == data_->bars.end()) --i;
        if (i != data_->bars.begin() && std::abs(seconds((i-1)->end)-t) < std::abs(seconds(i->end)-t)) --i;
        const auto text = QString("%1 UTC (bar close)\\nO %2   H %3\\nL %4   C %5\\nVolume: %6 shares")
            .arg(stamp(seconds(i->end)), QString::fromStdString(formatPrice(i->open)),
                QString::fromStdString(formatPrice(i->high)), QString::fromStdString(formatPrice(i->low)),
                QString::fromStdString(formatPrice(i->close)), QString::number(i->volume));
        QToolTip::showText(e->globalPosition().toPoint(), QString(text).replace("\\n", "\n"), this);
    }
    update();
}
void MarketChart::wheelEvent(QWheelEvent* e) {
    if (!data_ || data_->bars.empty()) return;
    const double min = seconds(data_->bars.front().start), max = seconds(data_->bars.back().end);
    const double span = std::clamp((end_ - start_) * (e->angleDelta().y() > 0 ? .8 : 1.25), std::min(300.0, max - min), max - min);
    const double anchor = std::clamp((e->position().x() - 72) / std::max(1, width() - 96), 0.0, 1.0);
    const double target = start_ + (end_ - start_) * anchor;
    start_ = std::clamp(target - span * anchor, min, max - span); end_ = start_ + span;
    e->accept(); update();
}
void MarketChart::leaveEvent(QEvent*) { hoverX_ = -1; update(); }
}
