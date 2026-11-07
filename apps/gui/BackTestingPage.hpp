#pragma once

#include "TradingAPI.hpp"

#include <QFutureWatcher>
#include <QMap>
#include <QTimer>
#include <QWidget>

#include <array>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace trading_app_desktop {

class MarketChart;

class BackTestingPage final : public QWidget {
    Q_OBJECT

public:
    explicit BackTestingPage(QWidget* parent = nullptr);
    ~BackTestingPage() override;

    bool isBusy() const noexcept {
        return busy_;
    }

    void showClosingMessage();

signals:
    void statusChanged(const QString& message);
    void busyChanged(bool busy);
    void jobFinished();

private:
    struct Inputs {
        QString cash{"10000.0000"};
        QString fee{"0.0000"};

        int strategy{0};
        int lookback{5};
        int quantity{10};
        int slippage{0};

        double threshold{0};
        double volFloor{1e-8};
    };

    void buildUi();
    void connectUi();

    void saveInputs();
    void clearDisplay();

    void readSource(
        trading_app_cli::RunConfiguration& config) const;

    trading_app_cli::RunConfiguration configuration();

    void refreshInputs();
    void setBusy(bool busy);
    void present();

    void startJob(bool execute);
    void onFinished();
    void onProgress();
    void onInstrumentChanged(int row);

    const std::array<trading_app_cli::Instrument, 6> instruments_{{
        {1, "SPY"},
        {2, "AAPL"},
        {3, "AMZN"},
        {4, "GOOGL"},
        {5, "NVDA"},
        {6, "TSLA"}
    }};

    QMap<QString, Inputs> settings_;
    QString selectedSymbol_{"SPY"};

    std::shared_ptr<const ViewData> displayed_;
    std::shared_ptr<JobProgress> progress_;

    QFutureWatcher<std::shared_ptr<ViewData>> watcher_;
    QTimer progressTimer_;

    bool busy_{false};
    bool runningStrategy_{false};
    bool closing_{false};

    QLineEdit* directory_{};
    QLineEdit* timezone_{};
    QLineEdit* cash_{};
    QLineEdit* fee_{};

    QComboBox* selector_{};
    QComboBox* strategy_{};

    QSpinBox* lookback_{};
    QSpinBox* quantity_{};
    QSpinBox* slippage_{};

    QDoubleSpinBox* threshold_{};
    QDoubleSpinBox* volFloor_{};

    QPushButton* browse_{};
    QPushButton* resetChart_{};
    QPushButton* load_{};
    QPushButton* run_{};

    QLabel* price_{};
    QLabel* marketTime_{};
    QLabel* tradeLabel_{};
    QLabel* validation_{};
    QLabel* state_{};

    QGroupBox* configCard_{};
    QProgressBar* busyBar_{};
    QTableWidget* trades_{};
    MarketChart* chart_{};

    std::array<QLabel*, 10> accountValues_{};
};

} // namespace trading_app_desktop