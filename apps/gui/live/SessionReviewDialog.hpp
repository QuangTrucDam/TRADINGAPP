#pragma once
#include "EngineGate.hpp"
#include "SessionArchive.hpp"
#include <QDialog>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QCloseEvent;
namespace trading_app_desktop {
class SessionReviewDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit SessionReviewDialog(SharedEngineGate gate, QWidget *parent = nullptr);
    ~SessionReviewDialog() override;
    bool isBusy() const { return watcher_.isRunning(); }
    void requestCancel();
    void setRecordingPath(const QString &path);

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    void run(bool audit);
    void finished();
    void runFaults();
    SharedEngineGate gate_;
    bool closing_{}, claimed_{};
    std::shared_ptr<std::atomic<bool>> stop_;
    QFutureWatcher<trading_app_cli::live::ReviewResult> watcher_;
    QString path_, report_;
    QPlainTextEdit *output_{};
    QLabel *status_{};
    QDoubleSpinBox *speed_{};
    QPushButton *open_{}, *replay_{}, *audit_{}, *cancel_{}, *export_{}, *faults_{};
};
} // namespace trading_app_desktop
