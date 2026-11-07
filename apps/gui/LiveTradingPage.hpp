#pragma once
#include <QWidget>

namespace trading_app_desktop {
// Independent presentation workspace. A future controller will supply live
// observations and indicators; this class does not create an engine or feed.
class LiveTradingPage final : public QWidget {
public:
    explicit LiveTradingPage(QWidget* parent = nullptr);
};
}
