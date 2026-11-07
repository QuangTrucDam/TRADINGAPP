#include "BackTestingPage.hpp"
#include "MarketChart.hpp"

#include "core/FixedPoint.hpp"
#include "strategy/StrategyFactory.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrentRun>

#include <exception>
#include <stdexcept>
#include <utility>

namespace trading_app_desktop {

using namespace trading_app_cli;

namespace {

QString moneyText(Money value) {
    return QString::fromStdString(formatMoney(value));
}

QString optionalMoney(const std::optional<Money>& value) {
    return value ? moneyText(*value) : QString("—");
}

QString timeText(EventTime time) {
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(milliseconds, Qt::UTC)
        .toString("yyyy-MM-dd HH:mm:ss 'UTC'");
}

} // namespace

BackTestingPage::BackTestingPage(QWidget* parent)
    : QWidget(parent)
{
    for (const auto& instrument : instruments_) {
        settings_.insert(
            QString::fromStdString(instrument.symbol),
            Inputs{});
    }

    buildUi();
    connectUi();
    refreshInputs();
}

BackTestingPage::~BackTestingPage() {
    progressTimer_.stop();

    // A background job captures configuration and shared data,
    // not this page. Wait before destroying its watcher.
    watcher_.disconnect(this);
    watcher_.waitForFinished();
}

void BackTestingPage::buildUi() {
    setObjectName("backtestingPage");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    auto* title = new QLabel(
        "BACKTESTING   /   HISTORICAL RESEARCH", this);

    title->setObjectName("eyebrow");
    root->addWidget(title);

    // Data source.
    auto* sourceRow = new QHBoxLayout;

    directory_ = new QLineEdit(this);
    directory_->setObjectName("dataDirectory");
    directory_->setPlaceholderText(
        "Directory containing <SYMBOL>_1min.csv");

    const QStringList candidates{
        QCoreApplication::applicationDirPath() + "/../data/samples",
        QCoreApplication::applicationDirPath() + "/data/samples",
        QDir::currentPath() + "/data/samples"
    };

    for (const auto& candidate : candidates) {
        if (QDir(candidate).exists()) {
            directory_->setText(QDir(candidate).absolutePath());
            break;
        }
    }

    browse_ = new QPushButton("Choose directory…", this);

    timezone_ = new QLineEdit("America/New_York", this);
    timezone_->setMaximumWidth(190);
    timezone_->setToolTip(
        "Declared timezone of CSV timestamps. "
        "Charts and tables display UTC.");

    sourceRow->addWidget(new QLabel("DATA", this));
    sourceRow->addWidget(directory_, 1);
    sourceRow->addWidget(browse_);
    sourceRow->addWidget(new QLabel("Source timezone", this));
    sourceRow->addWidget(timezone_);

    root->addLayout(sourceRow);

    // Left: market display. Right: settings and portfolio.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter);
    auto* marketLayout = new QVBoxLayout(left);
    marketLayout->setContentsMargins(0, 0, 14, 0);

    selector_ = new QComboBox(left);
    selector_->setObjectName("instrumentSelector");

    const QStringList names{
        "SPDR S&P 500 ETF",
        "Apple",
        "Amazon",
        "Alphabet",
        "NVIDIA",
        "Tesla"
    };

    for (std::size_t i = 0; i < instruments_.size(); ++i) {
        selector_->addItem(
            QString::fromStdString(instruments_[i].symbol)
            + "  ·  "
            + names[static_cast<int>(i)]);
    }

    marketLayout->addWidget(selector_);

    price_ = new QLabel("—", left);
    price_->setObjectName("marketPrice");
    marketLayout->addWidget(price_);

    marketTime_ = new QLabel("No historical data loaded", left);
    marketLayout->addWidget(marketTime_);

    chart_ = new MarketChart(left);
    marketLayout->addWidget(chart_, 3);

    // auto* chartActions = new QHBoxLayout;

    // chartActions->addWidget(
    //     new QLabel(
    //         "Wheel to zoom · drag to pan · "
    //         "hover for exact OHLCV · UTC",
    //         left),
    //     1);

    // resetChart_ = new QPushButton("Reset chart", left);
    // chartActions->addWidget(resetChart_);

    // marketLayout->addLayout(chartActions);

    tradeLabel_ = new QLabel(
        "FILLED EXECUTIONS   /   no run yet", left);

    marketLayout->addWidget(tradeLabel_);

    trades_ = new QTableWidget(0, 7, left);
    trades_->setHorizontalHeaderLabels({
        "Time (UTC)",
        "Side",
        "Shares",
        "Price (USD)",
        "Fee (USD)",
        "Order ID",
        "Status"
    });

    trades_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);

    trades_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);

    trades_->verticalHeader()->hide();
    trades_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    trades_->setSelectionBehavior(QAbstractItemView::SelectRows);
    trades_->setMinimumHeight(100);
    trades_->setMaximumHeight(180);

    marketLayout->addWidget(trades_, 1);

    // Scrollable configuration and account column.
    auto* scroll = new QScrollArea(splitter);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(355);

    auto* right = new QWidget;
    auto* cards = new QVBoxLayout(right);
    cards->setContentsMargins(15, 8, 10, 8);

    configCard_ = new QGroupBox(
        "STRATEGY CONFIGURATION", right);

    auto* form = new QFormLayout(configCard_);
    form->setFieldGrowthPolicy(
        QFormLayout::AllNonFixedFieldsGrow);

    cash_ = new QLineEdit("10000.0000", configCard_);
    cash_->setObjectName("initialCash");

    strategy_ = new QComboBox(configCard_);
    strategy_->setObjectName("strategy");

    strategy_->addItem("VN Momentum",static_cast<int>(StrategyKind::VNMomentum));

    strategy_->addItem("Momentum",static_cast<int>(StrategyKind::Momentum));

    strategy_->addItem("DoNothing",static_cast<int>(StrategyKind::DoNothing));

    lookback_ = new QSpinBox(configCard_);
    lookback_->setRange(1, 1000000);
    lookback_->setValue(5);

    quantity_ = new QSpinBox(configCard_);
    quantity_->setRange(1, 1000000);
    quantity_->setValue(10);

    threshold_ = new QDoubleSpinBox(configCard_);
    threshold_->setRange(0, 1000000);
    threshold_->setDecimals(6);

    volFloor_ = new QDoubleSpinBox(configCard_);
    volFloor_->setDecimals(10);
    volFloor_->setRange(1e-10, 1);
    volFloor_->setValue(1e-8);
    volFloor_->setSingleStep(1e-8);

    fee_ = new QLineEdit("0.0000", configCard_);

    slippage_ = new QSpinBox(configCard_);
    slippage_->setRange(0, 1000000);

    cash_->setToolTip(
        "Exact USD text: decimal dot, up to four fractional "
        "digits; no thousands separator.");

    fee_->setToolTip(cash_->toolTip());

    slippage_->setToolTip(
        "Absolute price units: 10,000 units = 1 USD.");

    threshold_->setToolTip(
        "VN momentum score threshold θ; dimensionless.");

    volFloor_->setToolTip(
        "Minimum return volatility ε.");

    form->addRow("Initial cash (USD)", cash_);
    form->addRow("Strategy", strategy_);
    form->addRow("Lookback (bars)", lookback_);
    form->addRow("Buy quantity", quantity_);
    form->addRow("VN threshold θ", threshold_);
    form->addRow("VN volatility floor ε", volFloor_);
    form->addRow("Flat fee (USD)", fee_);
    form->addRow("Slippage (units)", slippage_);

    cards->addWidget(configCard_);

    validation_ = new QLabel(right);
    validation_->setObjectName("validation");
    validation_->setWordWrap(true);
    validation_->setMinimumHeight(38);
    cards->addWidget(validation_);

    load_ = new QPushButton("Load History", right);
    load_->setObjectName("loadHistory");
    cards->addWidget(load_);

    run_ = new QPushButton("Run Strategy", right);
    run_->setObjectName("runStrategy");
    cards->addWidget(run_);

    resetChart_ = new QPushButton("Reset chart", right);
    resetChart_->setObjectName("resetChart");
    cards->addWidget(resetChart_);

    // auto* stop = new QPushButton("Stop Strategy", right);
    // stop->setEnabled(false);
    // cards->addWidget(stop);

    // auto* stopHelp = new QLabel(
    //     "Stop is unavailable: the current engine "
    //     "has no cooperative stop API.",
    //     right);

    // stopHelp->setWordWrap(true);
    // cards->addWidget(stopHelp);

    busyBar_ = new QProgressBar(right);
    busyBar_->setRange(0, 0);
    busyBar_->setVisible(false);
    cards->addWidget(busyBar_);

    state_ = new QLabel(
        "IDLE · select a dataset directory", right);

    state_->setWordWrap(true);
    state_->setObjectName("runState");
    cards->addWidget(state_);

    auto* accountCard = new QGroupBox("PORTFOLIO   /   USD", right);
    auto* accountForm = new QFormLayout(accountCard);

    const QStringList accountNames{
        "Initial cash",
        "Cash",
        "Holdings (shares)",
        "Position value",
        "Equity",
        "Total PnL",
        "Realized PnL",
        "Unrealized PnL",
        "Total fees",
        "Bars / fills / expired"
    };

    for (std::size_t i = 0; i < accountValues_.size(); ++i) {
        accountValues_[i] = new QLabel("—", accountCard);

        accountValues_[i]->setTextInteractionFlags(
            Qt::TextSelectableByMouse);

        accountForm->addRow(
            accountNames[static_cast<int>(i)],
            accountValues_[i]);
    }

    cards->addWidget(accountCard);
    cards->addStretch();

    scroll->setWidget(right);

    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 72);
    splitter->setStretchFactor(1, 28);
    splitter->setSizes({980, 390});
}

void BackTestingPage::connectUi() {
    connect(run_, &QPushButton::clicked, this, [this] {
        startJob(true);
    });

    connect(load_, &QPushButton::clicked, this, [this] {
        startJob(false);
    });

    connect(browse_, &QPushButton::clicked, this, [this] {
        const auto chosen = QFileDialog::getExistingDirectory(
            this,
            "Select historical dataset directory",
            directory_->text());

        if (!chosen.isEmpty()) {
            directory_->setText(chosen);
        }
    });

    connect(
        resetChart_,
        &QPushButton::clicked,
        chart_,
        &MarketChart::resetView);

    connect(
        &progressTimer_,
        &QTimer::timeout,
        this,
        &BackTestingPage::onProgress);

    connect(
        &watcher_,
        &QFutureWatcher<std::shared_ptr<ViewData>>::finished,
        this,
        &BackTestingPage::onFinished);

    connect(
        selector_,
        &QComboBox::currentIndexChanged,
        this,
        &BackTestingPage::onInstrumentChanged);

    for (auto* edit : {cash_, fee_, directory_, timezone_}) {
        connect(
            edit,
            &QLineEdit::textChanged,
            this,
            &BackTestingPage::refreshInputs);
    }

    connect(
        strategy_,
        &QComboBox::currentIndexChanged,
        this,
        &BackTestingPage::refreshInputs);

    connect(
        lookback_,
        &QSpinBox::valueChanged,
        this,
        &BackTestingPage::refreshInputs);

    for (auto* edit : {directory_, timezone_}) {
        connect(edit, &QLineEdit::textChanged, this, [this] {
            clearDisplay();
            state_->setText("IDLE · data source changed");
        });
    }
}

void BackTestingPage::saveInputs() {
    settings_[selectedSymbol_] = {
        cash_->text(),
        fee_->text(),
        strategy_->currentIndex(),
        lookback_->value(),
        quantity_->value(),
        slippage_->value(),
        threshold_->value(),
        volFloor_->value()
    };
}

void BackTestingPage::clearDisplay() {
    displayed_.reset();

    chart_->setData({});
    trades_->setRowCount(0);

    tradeLabel_->setText("FILLED EXECUTIONS   /   no run yet");
    price_->setText("—");
    marketTime_->setText("No historical data loaded");

    for (auto* value : accountValues_) {
        value->setText("—");
    }
}

void BackTestingPage::readSource(RunConfiguration& config) const {
    if (directory_->text().isEmpty()
        || !QDir(directory_->text()).exists()) {
        throw std::invalid_argument(
            "Choose an existing dataset directory.");
    }

    if (timezone_->text().isEmpty()) {
        throw std::invalid_argument(
            "Declare the source timezone.");
    }

    auto& sim = config.simulation;

    sim.instrument =
        instruments_[static_cast<std::size_t>(
            selector_->currentIndex())];

    sim.dataset_path = QDir(directory_->text())
        .absoluteFilePath(selectedSymbol_ + "_1min.csv")
        .toStdString();

    sim.source_timezone = timezone_->text().toStdString();
}

RunConfiguration BackTestingPage::configuration() {
    lookback_->interpretText();
    quantity_->interpretText();
    slippage_->interpretText();
    threshold_->interpretText();
    volFloor_->interpretText();

    RunConfiguration config;
    readSource(config);

    auto& sim = config.simulation;

    sim.initial_cash = parseMoney(cash_->text().toStdString());

    sim.execution.flat_fee = parseMoney(fee_->text().toStdString());

    sim.execution.slippage_units = slippage_->value();

    if (sim.initial_cash.units <= 0) 
        throw std::invalid_argument("Initial cash must be positive.");

    if (sim.execution.flat_fee.units < 0) 
        throw std::invalid_argument("Flat fee cannot be negative.");


    auto& selectedStrategy = config.strategy;

    selectedStrategy.kind = static_cast<StrategyKind>(strategy_->currentData().toInt());

    selectedStrategy.strategy_id ="desktop:" + sim.instrument.symbol;

    selectedStrategy.lookback =
        static_cast<std::size_t>(lookback_->value());

    selectedStrategy.buy_quantity = quantity_->value();
    selectedStrategy.threshold = threshold_->value();
    selectedStrategy.volFloor = volFloor_->value();

    validateStrategyConfiguration(
        selectedStrategy,
        sim.instrument);

    return config;
}

void BackTestingPage::refreshInputs() {
    const auto kind = static_cast<StrategyKind>(
        strategy_->currentData().toInt());

    const bool momentum = kind != StrategyKind::DoNothing;
    const bool vnMomentum = kind == StrategyKind::VNMomentum;

    lookback_->setEnabled(momentum);
    quantity_->setEnabled(momentum);
    threshold_->setEnabled(vnMomentum);
    volFloor_->setEnabled(vnMomentum);

    QString error;

    try {
        if (parseMoney(cash_->text().toStdString()).units <= 0) {
            error = "Initial cash must be positive.";
        } else if (
            parseMoney(fee_->text().toStdString()).units < 0) {
            error = "Flat fee cannot be negative.";
        } else if (vnMomentum && lookback_->value() < 2) {
            error = "VN Momentum needs a lookback of at least 2.";
        }
    } catch (const std::exception& e) {
        error = QString::fromUtf8(e.what())
            + " · USD uses a dot and at most 4 decimals.";
    }

    validation_->setText(error);

    const bool sourceAvailable =
        !directory_->text().isEmpty()
        && QDir(directory_->text()).exists();

    run_->setEnabled(
        !busy_ && error.isEmpty() && sourceAvailable);

    load_->setEnabled(!busy_ && sourceAvailable);
}

void BackTestingPage::setBusy(bool busy) {
    busy_ = busy;

    configCard_->setEnabled(!busy);
    selector_->setEnabled(!busy);
    directory_->setEnabled(!busy);
    browse_->setEnabled(!busy);
    timezone_->setEnabled(!busy);

    busyBar_->setVisible(busy);
    refreshInputs();

    if (busy) {
        run_->setEnabled(false);
        load_->setEnabled(false);
    }

    emit busyChanged(busy_);
}

void BackTestingPage::present() {
    if (!displayed_ || displayed_->bars.empty()) {
        throw std::runtime_error(
            "No historical bars returned.");
    }

    chart_->setData(displayed_);

    const auto& last = displayed_->bars.back();

    price_->setText(
        QString::fromStdString(formatPrice(last.close))
        + " USD");

    const QString mode = displayed_->run
        ? "COMPLETED BACKTEST · "
        : "HISTORICAL PREVIEW · ";

    marketTime_->setText(mode + timeText(last.end));

    if (!displayed_->run) {
        return;
    }

    const auto& result = *displayed_->run;
    const auto& portfolio = result.portfolio;

    Quantity held = 0;
    std::optional<Money> positionValue;

    for (const auto& position : portfolio.positions) {
        if (position.instrument.id
            != result.configuration.simulation.instrument.id) {
            continue;
        }

        held = position.quantity;

        if (position.quantity == 0) {
            positionValue = Money{};
        } else if (position.mark) {
            positionValue = checkedNotional(
                position.quantity,
                *position.mark);
        }
    }

    if (portfolio.positions.empty()) {
        positionValue = Money{};
    }

    accountValues_[0]->setText(
        moneyText(portfolio.initial_cash));

    accountValues_[1]->setText(
        moneyText(portfolio.cash));

    accountValues_[2]->setText(
        QString::number(held));

    accountValues_[3]->setText(
        optionalMoney(positionValue));

    accountValues_[4]->setText(
        optionalMoney(portfolio.equity));

    accountValues_[5]->setText(
        portfolio.equity
            ? moneyText(checkedSubtract(
                *portfolio.equity,
                portfolio.initial_cash))
            : QString("—"));

    accountValues_[6]->setText(
        moneyText(portfolio.realized_pnl));

    accountValues_[7]->setText(
        optionalMoney(portfolio.unrealized_pnl));

    accountValues_[8]->setText(
        moneyText(portfolio.total_fees));

    accountValues_[9]->setText(
        QString("%1 / %2 / %3")
            .arg(result.statistics.completed_bars)
            .arg(result.statistics.executions_filled)
            .arg(result.statistics.orders_expired));

    constexpr std::size_t tableLimit = 5000;

    const auto begin = result.executions.size() > tableLimit
        ? result.executions.size() - tableLimit
        : 0;

    trades_->setRowCount(
        static_cast<int>(result.executions.size() - begin));

    for (std::size_t i = begin;
         i < result.executions.size();
         ++i) {
        const auto& report = result.executions[i].report;

        const QStringList cells{
            timeText(report.fill_time),
            report.side == Side::Buy ? "BUY" : "SELL",
            QString::number(report.quantity),
            QString::fromStdString(formatPrice(report.price)),
            moneyText(report.fee),
            QString::number(report.order_id),
            report.status == OrderStatus::Filled
                ? "FILLED"
                : "REJECTED"
        };

        for (int column = 0; column < cells.size(); ++column) {
            trades_->setItem(
                static_cast<int>(i - begin),
                column,
                new QTableWidgetItem(cells[column]));
        }
    }

    tradeLabel_->setText(
        QString(
            "EXECUTIONS   /   %1 fills · %2 rejected · "
            "showing last %3 records")
            .arg(result.statistics.executions_filled)
            .arg(result.statistics.executions_rejected)
            .arg(trades_->rowCount()));
}

void BackTestingPage::startJob(bool execute) {
    if (busy_) {
        return;
    }

    try {
        RunConfiguration config;

        if (execute) {
            config = configuration();
        } else {
            readSource(config);
        }

        saveInputs();
        clearDisplay();

        runningStrategy_ = execute;
        progress_ = std::make_shared<JobProgress>();

        setBusy(true);
        validation_->clear();

        state_->setText(execute? "RUNNING · fresh account and strategy": "LOADING · historical preview");

        progressTimer_.start(100);

        const auto capturedProgress = progress_;

        watcher_.setFuture(QtConcurrent::run(
            [config, execute, capturedProgress] {
                return loadOrRun(
                    config,
                    execute,
                    capturedProgress);
            }));
    } catch (const std::exception& error) {
        progressTimer_.stop();
        setBusy(false);

        validation_->setText(QString::fromUtf8(error.what()));

        state_->setText("INVALID CONFIGURATION");

        emit statusChanged(
            "Backtesting · invalid configuration");
    }
}

void BackTestingPage::onProgress() {
    if (closing_) {
        return;
    }

    const auto count = progress_
        ? progress_->bars.load(std::memory_order_relaxed)
        : 0;

    emit statusChanged(
        QString(
            "%1 · %2 bars processed · source %3 · "
            "displayed in UTC")
            .arg(runningStrategy_ ? "Backtest" : "Preview")
            .arg(count)
            .arg(timezone_->text()));
}

void BackTestingPage::onFinished() {
    progressTimer_.stop();
    setBusy(false);

    try {
        auto result = watcher_.result();

        if (!result) {
            throw std::runtime_error("No result returned.");
        }

        if (!result->error.empty()) {
            throw std::runtime_error(result->error);
        }

        if (result->run
            && result->run->status != RunStatus::Completed) {
            throw std::runtime_error(
                "Backtest did not complete.");
        }

        displayed_ = std::move(result);
        present();

        if (displayed_->run) {
            const auto& config =
                displayed_->run->configuration;

            const QString kind =
                config.strategy.kind == StrategyKind::VNMomentum
                    ? "VN Momentum"
                    : config.strategy.kind == StrategyKind::Momentum
                    ? "Momentum"
                    : "DoNothing";

            state_->setText(
                "COMPLETED · " + kind
                + " · initial "
                + moneyText(config.simulation.initial_cash)
                + " USD");
        } else {
            state_->setText(
                "PREVIEW READY · no strategy or account created");
        }

        emit statusChanged(
            QString(
                "%1 · %2 bars · %3 · source %4 · "
                "displayed in UTC")
                .arg(displayed_->run
                    ? "Completed backtest"
                    : "Historical preview")
                .arg(displayed_->bars.size())
                .arg(selectedSymbol_)
                .arg(timezone_->text()));
    } catch (const std::exception& error) {
        clearDisplay();

        validation_->setText(
            QString::fromUtf8(error.what()));

        state_->setText("FAILED · see diagnostic above");

        emit statusChanged(
            "Backtesting job failed; "
            "no successful result is displayed");
    } catch (...) {
        clearDisplay();
        validation_->setText("Unexpected worker failure");
        state_->setText("FAILED");

        emit statusChanged(
            "Backtesting · unexpected worker failure");
    }

    emit jobFinished();
}

void BackTestingPage::onInstrumentChanged(int row) {
    if (row < 0
        || row >= static_cast<int>(instruments_.size())) {
        return;
    }

    saveInputs();

    selectedSymbol_ = QString::fromStdString(
        instruments_[static_cast<std::size_t>(row)].symbol);

    const auto value = settings_.value(selectedSymbol_);

    cash_->setText(value.cash);
    fee_->setText(value.fee);
    strategy_->setCurrentIndex(value.strategy);
    lookback_->setValue(value.lookback);
    quantity_->setValue(value.quantity);
    slippage_->setValue(value.slippage);
    threshold_->setValue(value.threshold);
    volFloor_->setValue(value.volFloor);

    clearDisplay();
    state_->setText("IDLE · " + selectedSymbol_);
    refreshInputs();

    emit statusChanged(
        "Instrument changed · "
        "load history or run a fresh strategy");
}

void BackTestingPage::showClosingMessage() {
    closing_ = true;

    state_->setText(
        "CLOSING · waiting for the current "
        "historical job to finish safely");

    emit statusChanged(
        "Close requested. The window will close "
        "when this historical job finishes.");
}

} // namespace trading_app_desktop