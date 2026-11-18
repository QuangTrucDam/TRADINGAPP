#pragma once
#include "EngineGate.hpp"
#include <QPointer>
#include <QWidget>
#include <memory>
namespace trading_app_desktop {
class LiveFeedController;
class SessionReviewDialog;
class LiveTradingPage final : public QWidget {
    Q_OBJECT
  public:
    explicit LiveTradingPage(QWidget *parent = nullptr, SharedEngineGate gate = {});
    ~LiveTradingPage() override;
    bool isBusy() const;
    void requestShutdown();
  signals:
    void statusChanged(const QString &);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    void poll();
    void startPaper();
    void startDemo();
    void exportRecording();
    void exportPerformance();
    struct Ui;
    std::unique_ptr<Ui> ui_;
    SharedEngineGate gate_;
    LiveFeedController *controller_{};
    QPointer<SessionReviewDialog> review_;
    bool closing_{}, claimed_{}, demo_{};
};
} // namespace trading_app_desktop
