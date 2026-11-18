#include "AppWindow.hpp"
#include "backtest/BackTestingPage.hpp"
#include "live/LiveTradingPage.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <utility>

using namespace trading_app_desktop;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Trading App");

    AppWindow window;
    window.setWindowTitle(
        "Trading App | Historical and live market data");

    window.resize(1400, 900);
    window.setMinimumSize(1100, 780);

    auto* shell = new QWidget(&window);
    auto* layout = new QVBoxLayout(shell);
    layout->setContentsMargins(18, 12, 18, 12);

    auto* navigation = new QHBoxLayout;

    auto* backtestingButton =
        new QPushButton("Backtesting", shell);

    auto* liveButton =
        new QPushButton("Live Trading", shell);

    auto* group = new QButtonGroup(shell);
    group->setExclusive(true);

    for (auto* button : {backtestingButton, liveButton}) {
        button->setCheckable(true);
        button->setProperty("workspaceNavigation", true);
        button->setMinimumWidth(160);

        group->addButton(button);
        navigation->addWidget(button);
    }

    backtestingButton->setChecked(true);

    auto* workspaceLabel =
        new QLabel("Backtesting workspace", shell);

    navigation->addStretch();
    navigation->addWidget(workspaceLabel);
    layout->addLayout(navigation);

    auto* workspaces = new QStackedWidget(shell);

    auto engineGate = std::make_shared<EngineGate>();
    auto* backtestingPage =
        new BackTestingPage(workspaces, engineGate);

    auto* livePage =
        new LiveTradingPage(workspaces, engineGate);

    workspaces->addWidget(backtestingPage);
    workspaces->addWidget(livePage);

    layout->addWidget(workspaces, 1);
    window.setCentralWidget(shell);

    
    QObject::connect(
        backtestingButton,
        &QPushButton::clicked,
        workspaces,
        [workspaces, backtestingPage, workspaceLabel] {
            workspaces->setCurrentWidget(backtestingPage);
            workspaceLabel->setText("Backtesting workspace");
        });

    QObject::connect(
        liveButton,
        &QPushButton::clicked,
        workspaces,
        [workspaces, livePage, workspaceLabel] {
            workspaces->setCurrentWidget(livePage);
            workspaceLabel->setText(
                "Live Trading · market data and paper sessions");
        });

    auto status = std::make_shared<std::pair<QString, QString>>("Backtest: Idle", "Live: Disconnected");
    QObject::connect(livePage, &LiveTradingPage::statusChanged, &window,
        [&window, status](const QString& message) {
            status->second = message;
            window.statusBar()->showMessage(status->first + "  |  " + status->second);
        });
    QObject::connect(backtestingPage, &BackTestingPage::statusChanged, &window,
        [&window, status](const QString& message) {
            status->first = message;
            window.statusBar()->showMessage(status->first + "  |  " + status->second);
        });

    QObject::connect(
        backtestingPage,
        &BackTestingPage::busyChanged,
        &window,
        [&window](bool busy) {
            window.busy = busy;
        });

    QObject::connect(
        backtestingPage,
        &BackTestingPage::jobFinished,
        &window,
        [&window] {
            if (window.closeWhenFinished) {
                window.close();
            }
        });

    window.onDeferredClose = [backtestingPage] {
        backtestingPage->showClosingMessage();
    };


    window.setStyleSheet(R"(
        QWidget {
            background: #101922;
            color: #dfe8f2;
            font-size: 13px;
        }

        QLabel#eyebrow {
            color: #93a8bf;
            font-size: 12px;
            padding-bottom: 8px;
        }

        QLabel#marketPrice, QLabel#liveMarketPrice {
            font-size: 34px;
            color: #f5ce57;
            font-weight: bold;
        }

        QLabel#validation { color: #ffb2ac; }
        QLabel#runState { color: #f5ce57; }

        QGroupBox {
            border: 1px solid #334356;
            border-radius: 6px;
            margin-top: 16px;
            padding: 14px 8px 8px;
        }

        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }

        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
            background: #1c2937;
            border: 1px solid #50657c;
            border-radius: 4px;
            padding: 5px;
            min-height: 20px;
        }

        QLineEdit:focus, QComboBox:focus,
        QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: #f5ce57;
        }

        QPushButton {
            background: #243649;
            border: 1px solid #50657c;
            border-radius: 4px;
            padding: 8px;
        }

        QPushButton:hover { background: #354d66; }

        QPushButton[workspaceNavigation="true"] {
            background: #1c2937;
            color: #c7d5e5;
        }

        QPushButton[workspaceNavigation="true"]:checked {
            background: #f5ce57;
            color: #18212b;
            font-weight: bold;
            border-color: #f5ce57;
        }

        QPushButton[workspaceNavigation="true"]:focus {
            border: 2px solid #f5ce57;
        }

        QPushButton#runStrategy {
            background: #f5ce57;
            color: #18212b;
            font-weight: bold;
        }

        QPushButton:disabled,
        QPushButton#runStrategy:disabled {
            background: #202c39;
            color: #8798aa;
        }

        QTableWidget {
            gridline-color: #2c3b4b;
            border: 1px solid #334356;
        }

        QHeaderView::section {
            background: #1c2937;
            color: #c7d5e5;
            padding: 5px;
            border: 0;
        }

        QScrollArea { border: 1px solid #334356; }
    )");

    window.statusBar()->showMessage(
        "Backtesting and live market data · no live strategy running");

    window.show();
    return app.exec();
}