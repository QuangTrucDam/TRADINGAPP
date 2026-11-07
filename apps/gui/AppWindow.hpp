#pragma once
#include <QCloseEvent>
#include <QMainWindow>
#include <functional>

namespace trading_app_desktop {
// Keeps the event loop alive during a core run: the current core has no stop API.
class AppWindow final : public QMainWindow {
public:
    bool busy{false};
    bool closeWhenFinished{false};
    std::function<void()> onDeferredClose;
protected:
    void closeEvent(QCloseEvent* event) override {
        if (busy) {
            closeWhenFinished = true;
            if (onDeferredClose) onDeferredClose();
            event->ignore();
        } else event->accept();
    }
};
}
