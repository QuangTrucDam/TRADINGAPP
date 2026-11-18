#include "LiveTradingPage.hpp"
#include "LiveFeedController.hpp"
#include "LiveMarketChart.hpp"
#include "SessionReviewDialog.hpp"
#include "core/FixedPoint.hpp"
#include "marketdata/live/LiveConfiguration.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QSysInfo>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <ctime>
#ifdef __linux__
#include <sys/resource.h>
#endif
namespace trading_app_desktop {
using namespace trading_app_cli;
using namespace trading_app_cli::live;
namespace {
QString stamp(EventTime time) {
    return QDateTime::fromMSecsSinceEpoch(
               std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count(),
               Qt::UTC)
        .toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'");
}
QString money(Money value) { return QString::fromStdString(formatMoney(value)); }
QString price(Price value) { return QString::fromStdString(formatPrice(value)); }
QString orderStatus(OrderStatus s) {
    switch (s) {
    case OrderStatus::Submitted:
        return "Submitted";
    case OrderStatus::Accepted:
        return "Accepted";
    case OrderStatus::Filled:
        return "Filled";
    case OrderStatus::Rejected:
        return "Rejected";
    case OrderStatus::Expired:
        return "Expired";
    }
    return "Unknown";
}
QSpinBox *spin(QWidget *p, int low, int high, int value) {
    auto *s = new QSpinBox(p);
    s->setRange(low, high);
    s->setValue(value);
    return s;
}
} // namespace
struct LiveTradingPage::Ui {
    QComboBox *instrument{}, *indicator{}, *strategy{};
    QLabel *source{}, *trade{}, *quote{}, *reference{}, *indicator_value{}, *connection{}, *diagnostics{},
        *message{}, *paper_state{};
    QPushButton *connect{}, *disconnect{}, *start{}, *stop{}, *demo{}, *review{}, *export_recording{},
        *performance{};
    QCheckBox *bidask{}, *realtime{};
    QSpinBox *period{}, *lookback{}, *quantity{}, *slippage{}, *protection{}, *timeout{}, *quote_age{},
        *max_quantity{}, *max_position{}, *max_pending{};
    QDoubleSpinBox *threshold{};
    QLineEdit *cash{}, *fee{}, *max_notional{};
    QGroupBox *configuration{};
    LiveMarketChart *chart{};
    QTableWidget *orders{};
    std::array<QLabel *, 12> account{};
    Snapshot snapshot;
    TimingSeries gui_observation_age;
    std::uint64_t observed_version{};
};
LiveTradingPage::LiveTradingPage(QWidget *parent, SharedEngineGate gate)
    : QWidget(parent), ui_(std::make_unique<Ui>()),
      gate_(gate ? std::move(gate) : std::make_shared<EngineGate>()) {
    auto &u = *ui_;
    controller_ = new LiveFeedController(this);
    window()->installEventFilter(this);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *heading = new QLabel("LIVE DATA / PAPER TRADING / SESSION REVIEW", this);
    heading->setObjectName("eyebrow");
    root->addWidget(heading);
    auto *split = new QSplitter(this);
    root->addWidget(split, 1);
    auto *market = new QWidget(split);
    auto *left = new QVBoxLayout(market);
    left->setContentsMargins(0, 0, 12, 0);
    u.instrument = new QComboBox(market);
    u.instrument->setObjectName("liveInstrument");
    u.instrument->addItems({"BTCUSDT", "SPY", "AAPL", "AMZN", "GOOGL", "NVDA", "TSLA"});
    left->addWidget(u.instrument);
    u.source = new QLabel("Binance public spot observations · USDT / BTC", market);
    u.source->setWordWrap(true);
    left->addWidget(u.source);
    u.trade = new QLabel("Last trade: —", market);
    u.trade->setObjectName("liveMarketPrice");
    left->addWidget(u.trade);
    u.quote = new QLabel("Bid/ask: waiting for observations", market);
    u.quote->setWordWrap(true);
    left->addWidget(u.quote);
    u.reference = new QLabel(market);
    u.reference->setWordWrap(true);
    left->addWidget(u.reference);
    u.chart = new LiveMarketChart(market);
    u.chart->setObjectName("liveMarketChart");
    left->addWidget(u.chart, 3);
    auto *indicator_row = new QHBoxLayout;
    u.indicator = new QComboBox(market);
    u.indicator->addItems({"RSI", "SMA", "EMA"});
    u.indicator->setObjectName("liveIndicatorKind");
    u.period = spin(market, 2, 10000, 14);
    u.period->setObjectName("liveIndicatorPeriod");
    u.indicator_value = new QLabel("Warming up on completed bars", market);
    u.indicator_value->setObjectName("liveIndicatorValue");
    indicator_row->addWidget(u.indicator);
    indicator_row->addWidget(u.period);
    indicator_row->addWidget(u.indicator_value, 1);
    left->addLayout(indicator_row);
    u.orders = new QTableWidget(0, 6, market);
    u.orders->setHorizontalHeaderLabels({"Order", "Side", "Quantity", "Status", "Protection", "Reason"});
    u.orders->setEditTriggers(QAbstractItemView::NoEditTriggers);
    u.orders->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    u.orders->horizontalHeader()->setStretchLastSection(true);
    u.orders->setMaximumHeight(200);
    left->addWidget(u.orders, 1);
    auto *scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(355);
    auto *panel = new QWidget;
    scroll->setWidget(panel);
    auto *right = new QVBoxLayout(panel);
    auto *connection = new QGroupBox("CONNECTION / RECORDING", panel);
    auto *connection_layout = new QVBoxLayout(connection);
    u.connection = new QLabel("Disconnected", connection);
    u.connection->setObjectName("liveConnectionState");
    connection_layout->addWidget(u.connection);
    u.bidask = new QCheckBox("Enable Finnhub bid/ask", connection);
    u.bidask->setToolTip("Poll Finnhub /stock/bidask only if your account permits it.");
    connection_layout->addWidget(u.bidask);
    u.connect = new QPushButton("Connect feed", connection);
    u.connect->setObjectName("connectLiveFeed");
    connection_layout->addWidget(u.connect);
    u.disconnect = new QPushButton("Disconnect feed", connection);
    u.disconnect->setObjectName("disconnectLiveFeed");
    u.disconnect->setEnabled(false);
    connection_layout->addWidget(u.disconnect);
    u.demo = new QPushButton("Run synthetic paper demo", connection);
    u.demo->setObjectName("runSyntheticDemo");
    connection_layout->addWidget(u.demo);
    u.diagnostics = new QLabel(connection);
    u.diagnostics->setWordWrap(true);
    connection_layout->addWidget(u.diagnostics);
    right->addWidget(connection);
    u.configuration = new QGroupBox("NEXT PAPER SESSION · USD SHARES", panel);
    auto *form = new QFormLayout(u.configuration);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    u.cash = new QLineEdit("10000.0000", u.configuration);
    u.fee = new QLineEdit("0.1000", u.configuration);
    u.strategy = new QComboBox(u.configuration);
    u.strategy->addItems({"DoNothing", "Momentum", "VNMomentum", "Scripted buy/sell"});
    u.strategy->setCurrentIndex(1);
    u.quantity = spin(u.configuration, 1, 100000, 10);
    u.lookback = spin(u.configuration, 2, 10000, 5);
    u.slippage = spin(u.configuration, 0, 100000, 0);
    u.protection = spin(u.configuration, 1, 9999, 100);
    u.timeout = spin(u.configuration, 1, 3600, 10);
    u.quote_age = spin(u.configuration, 1, 60, 5);
    u.max_quantity = spin(u.configuration, 1, 100000, 1000);
    u.max_position = spin(u.configuration, 1, 1000000, 10000);
    u.max_pending = spin(u.configuration, 1, 64, 8);
    u.max_notional = new QLineEdit("100000.0000", u.configuration);
    u.threshold = new QDoubleSpinBox(u.configuration);
    u.threshold->setRange(0, 100000);
    u.threshold->setDecimals(4);
    u.threshold->setValue(.5);
    u.realtime = new QCheckBox("Real-time entitlement confirmed", u.configuration);
    form->addRow("Initial cash (USD)", u.cash);
    form->addRow("Strategy", u.strategy);
    form->addRow("Quantity", u.quantity);
    form->addRow("Lookback", u.lookback);
    form->addRow("VN threshold", u.threshold);
    form->addRow("Flat fee (USD)", u.fee);
    form->addRow("Slippage (1/10000 USD)", u.slippage);
    form->addRow("Protection (bps)", u.protection);
    form->addRow("Order timeout (seconds)", u.timeout);
    form->addRow("Quote max age (seconds)", u.quote_age);
    form->addRow("Max quantity / order", u.max_quantity);
    form->addRow("Max position", u.max_position);
    form->addRow("Max order notional", u.max_notional);
    form->addRow("Max pending", u.max_pending);
    form->addRow(u.realtime);
    right->addWidget(u.configuration);
    u.start = new QPushButton("Start live paper session", panel);
    u.start->setObjectName("startLivePaper");
    right->addWidget(u.start);
    u.stop = new QPushButton("Stop paper session (retain holdings)", panel);
    u.stop->setObjectName("stopLivePaper");
    right->addWidget(u.stop);
    u.paper_state = new QLabel("Idle · waiting for healthy timestamped quotes", panel);
    u.paper_state->setWordWrap(true);
    right->addWidget(u.paper_state);
    auto *account = new QGroupBox("PAPER ACCOUNT · USD", panel);
    auto *account_form = new QFormLayout(account);
    const std::array<const char *, 12> labels{"Cash",
                                              "Available cash",
                                              "Reserved cash",
                                              "Holdings (shares)",
                                              "Reserved shares",
                                              "Equity",
                                              "Realized PnL",
                                              "Unrealized PnL",
                                              "Fees",
                                              "Orders / fills",
                                              "Mark UTC",
                                              "Strategy bars"};
    for (std::size_t i = 0; i < labels.size(); ++i) {
        u.account[i] = new QLabel("—", account);
        u.account[i]->setWordWrap(true);
        account_form->addRow(labels[i], u.account[i]);
    }
    right->addWidget(account);
    u.review = new QPushButton("Review / replay / audit recording", panel);
    u.review->setObjectName("reviewSession");
    right->addWidget(u.review);
    u.export_recording = new QPushButton("Export sealed session", panel);
    right->addWidget(u.export_recording);
    u.performance = new QPushButton("Export live diagnostics report", panel);
    right->addWidget(u.performance);
    u.message = new QLabel("BTC is observation-only. Paper trading uses whole USD shares, new-bar warm-up "
                           "and subsequent fresh quotes. No real-money orders are sent.",
                           panel);
    u.message->setWordWrap(true);
    right->addWidget(u.message);
    right->addStretch();
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    try {
        auto env = readDotEnv(".env");
        u.bidask->setChecked(configuredValue(env, "FINNHUB_BIDASK_ENABLED") == "true");
        u.realtime->setChecked(configuredValue(env, "FINNHUB_REALTIME_CONFIRMED") == "true");
    } catch (const std::exception &e) {
        u.message->setText(QString::fromUtf8(e.what()));
    }
    connect(u.connect, &QPushButton::clicked, this, [this] {
        try {
            demo_ = false;
            controller_->connectFeed(ui_->instrument->currentText(), ui_->bidask->isChecked());
            ui_->message->clear();
            poll();
        } catch (const std::exception &e) {
            ui_->message->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(u.disconnect, &QPushButton::clicked, this, [this] {
        controller_->disconnectFeed();
        ui_->disconnect->setEnabled(false);
    });
    connect(u.demo, &QPushButton::clicked, this, &LiveTradingPage::startDemo);
    connect(u.start, &QPushButton::clicked, this, &LiveTradingPage::startPaper);
    connect(u.stop, &QPushButton::clicked, this, [this] {
        try {
            controller_->stopPaper();
        } catch (const std::exception &e) {
            ui_->message->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(u.period, &QSpinBox::valueChanged, this, [this](int n) {
        try {
            controller_->setIndicatorPeriod(n);
        } catch (const std::exception &e) {
            ui_->message->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(u.review, &QPushButton::clicked, this, [this] {
        if (!review_)
            review_ = new SessionReviewDialog(gate_, this);
        if (ui_->snapshot.recording_ok && !controller_->busy())
            review_->setRecordingPath(QString::fromStdString(ui_->snapshot.recording));
        review_->show();
        review_->raise();
    });
    connect(u.export_recording, &QPushButton::clicked, this, &LiveTradingPage::exportRecording);
    connect(u.performance, &QPushButton::clicked, this, &LiveTradingPage::exportPerformance);
    connect(u.instrument, &QComboBox::currentTextChanged, this, [this](const QString &symbol) {
        ui_->source->setText(symbol == "BTCUSDT" ? "Binance public spot · USDT / BTC"
                                                 : "Finnhub · confirm account coverage and delay");
        ui_->trade->setText("Last trade: —");
        ui_->quote->setText("Bid/ask: —");
        ui_->reference->clear();
        ui_->chart->setSnapshot({});
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &LiveTradingPage::poll);
    timer->start(200);
    u.start->setEnabled(false);
    u.stop->setEnabled(false);
    u.export_recording->setEnabled(false);
}
LiveTradingPage::~LiveTradingPage() {
    requestShutdown();
    if (claimed_)
        gate_->release("live");
}
bool LiveTradingPage::isBusy() const { return controller_->busy() || (review_ && review_->isBusy()); }
void LiveTradingPage::requestShutdown() {
    controller_->disconnectFeed();
    if (review_)
        review_->requestCancel();
}
bool LiveTradingPage::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Close && isBusy()) {
        closing_ = true;
        requestShutdown();
        event->ignore();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
void LiveTradingPage::startDemo() {
    if (!gate_->acquire("live")) {
        ui_->message->setText("Another engine job is active: " + QString::fromStdString(gate_->owner()));
        return;
    }
    claimed_ = true;
    try {
        demo_ = true;
        ui_->instrument->setCurrentText("AAPL");
        controller_->connectDemo();
        ui_->message->setText("SYNTHETIC DEMO: accelerated generated inputs, scripted buy/sell, USD paper "
                              "account. This is not market data.");
        poll();
    } catch (const std::exception &e) {
        gate_->release("live");
        claimed_ = false;
        demo_ = false;
        ui_->message->setText(QString::fromUtf8(e.what()));
    }
}
void LiveTradingPage::startPaper() {
    if (!gate_->acquire("live")) {
        ui_->message->setText("Another engine job is active: " + QString::fromStdString(gate_->owner()));
        return;
    }
    claimed_ = true;
    try {
        auto &u = *ui_;
        SessionConfiguration c;
        c.instrument = {1, u.instrument->currentText().toStdString()};
        c.initial_cash = parseMoney(u.cash->text().toStdString());
        c.execution.flat_fee = parseMoney(u.fee->text().toStdString());
        c.execution.slippage_units = u.slippage->value();
        c.strategy.kind = u.strategy->currentIndex() == 0   ? StrategyKind::DoNothing
                          : u.strategy->currentIndex() == 1 ? StrategyKind::Momentum
                          : u.strategy->currentIndex() == 2 ? StrategyKind::VNMomentum
                                                            : StrategyKind::Scripted;
        c.strategy.strategy_id = "live-" + u.instrument->currentText().toStdString();
        c.strategy.lookback = u.lookback->value();
        c.strategy.buy_quantity = u.quantity->value();
        c.strategy.threshold = u.threshold->value();
        if (c.strategy.kind == StrategyKind::Scripted)
            c.strategy.decisions = {{1, Side::Buy, u.quantity->value()},
                                    {2, Side::Sell, u.quantity->value()}};
        c.protection_bps = u.protection->value();
        c.order_timeout = std::chrono::seconds(u.timeout->value());
        c.quote_max_age = std::chrono::seconds(u.quote_age->value());
        c.maximum_order_quantity = u.max_quantity->value();
        c.maximum_position = u.max_position->value();
        c.maximum_pending = u.max_pending->value();
        c.maximum_order_notional = parseMoney(u.max_notional->text().toStdString());
        c.realtime_confirmed = u.realtime->isChecked();
        controller_->startPaper(std::move(c));
        u.message->setText("Start requested. Warm-up uses only new completed bars.");
        poll();
    } catch (const std::exception &e) {
        gate_->release("live");
        claimed_ = false;
        ui_->message->setText(QString::fromUtf8(e.what()));
    }
}
void LiveTradingPage::poll() {
    auto &u = *ui_;
    auto s = controller_->poll();
    u.snapshot = s;
    if (s.published_ns && s.version != u.observed_version) {
        u.observed_version = s.version;
        auto observed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        u.gui_observation_age.add(observed - s.published_ns);
    }
    const bool busy = controller_->busy();
    const bool running = s.paper.state == "Running";
    const bool pending = controller_->pendingCommand();
    if (claimed_ && ((!demo_ && !running && !pending) || (demo_ && !busy))) {
        gate_->release("live");
        claimed_ = false;
    }
    if (demo_ && !busy)
        demo_ = false;
    u.instrument->setEnabled(!busy);
    u.connect->setEnabled(!busy && !closing_);
    u.disconnect->setEnabled(busy && !closing_);
    u.demo->setEnabled(!busy && !gate_->busy() && !closing_);
    u.bidask->setEnabled(!busy);
    u.configuration->setEnabled(!running && !pending && !demo_);
    u.start->setEnabled(busy && s.paper_ready && !running && !pending && !gate_->busy() && !demo_ &&
                        u.realtime->isChecked());
    u.stop->setEnabled(busy && (running || pending));
    u.review->setEnabled(!gate_->busy() && !closing_);
    u.export_recording->setEnabled(!busy && s.recording_ok && !s.recording.empty());
    u.performance->setEnabled(!s.session.empty());
    u.connection->setText(QString::fromStdString(s.state));
    if (!s.session.empty())
        u.source->setText(QString::fromStdString(s.symbol + " · " + s.entitlement + " · " + s.unit));
    const auto now = s.synthetic ? s.logical_time : utcNow();
    if (!s.last)
        u.trade->setText("Last trade: —");
    if (s.last) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - s.last->time).count();
        u.trade->setText(QString("Last trade %1 · %2 · age %3 s%4")
                             .arg(price(s.last->price))
                             .arg(stamp(s.last->time))
                             .arg(age)
                             .arg(age > 30 ? " / STALE" : ""));
    }
    if (s.quote) {
        u.quote->setText(QString("Bid %1 / Ask %2 · %3")
                             .arg(price(s.quote->bid), price(s.quote->ask),
                                  s.quote->source_time ? stamp(*s.quote->source_time)
                                                       : "no source timestamp; observation only"));
    } else if (!s.session.empty())
        u.quote->setText("Bid/ask unavailable: paper Start waits for a fresh timestamped quote");
    if (!s.reference_price)
        u.reference->clear();
    if (s.reference_price)
        u.reference->setText(QString("REST reference last %1 · %2 (not bid/ask)")
                                 .arg(price(*s.reference_price), stamp(*s.reference_time)));
    u.chart->setSnapshot(s);
    auto value = u.indicator->currentText() == "RSI"   ? s.rsi
                 : u.indicator->currentText() == "SMA" ? s.sma
                                                       : s.ema;
    u.indicator_value->setText(QString("%1 (%2): %3 · %4 completed closes")
                                   .arg(u.indicator->currentText())
                                   .arg(s.indicator_period)
                                   .arg(value ? QString::number(*value, 'f', 4) : "warming up")
                                   .arg(s.warmup));
    u.paper_state->setText(QString::fromStdString(s.paper.state + " · " + s.paper.blocked_reason));
    u.diagnostics->setText(
        QString("Input %1 · rejected %2 · duplicates %3\nLate/partial %4 · gaps %5 · reconnects %6\nQueue "
                "high %7 · recorder high %8\nRecording %9\nSession %10\n%11\n%12")
            .arg(s.processed)
            .arg(s.rejected)
            .arg(s.duplicates)
            .arg(s.late)
            .arg(s.gaps)
            .arg(s.reconnects)
            .arg(s.queue_high)
            .arg(s.performance.recorder_high)
            .arg(s.recording_ok ? "writing/sealed" : "pending or incomplete")
            .arg(QString::fromStdString(s.session), QString::fromStdString(s.recording),
                 QString::fromStdString(s.error)));
    u.diagnostics->setText(u.diagnostics->text() +
                           QString("\nQueue depth %1 · recorder depth %2\nHeartbeat %3")
                               .arg(s.performance.queue_depth)
                               .arg(s.performance.recorder_depth)
                               .arg(s.heartbeat ? stamp(*s.heartbeat) : "not yet observed"));
    if (s.paper.account) {
        const auto &a = *s.paper.account;
        Quantity held = 0;
        for (const auto &p : a.positions)
            held += p.quantity;
        const std::array<QString, 12> values{
            money(a.cash),
            money(checkedSubtract(a.cash, s.paper.reserved_cash)),
            money(s.paper.reserved_cash),
            QString::number(held),
            QString::number(s.paper.reserved_shares),
            a.equity ? money(*a.equity) : "unpriced",
            money(a.realized_pnl),
            a.unrealized_pnl ? money(*a.unrealized_pnl) : "unpriced",
            money(a.total_fees),
            QString("%1 / %2").arg(s.paper.orders).arg(s.paper.fills),
            s.paper.mark_time ? stamp(*s.paper.mark_time) +
                                    (now - *s.paper.mark_time > std::chrono::seconds(30) ? " / STALE" : "")
                              : "—",
            QString("%1 total; warm-up %2 / %3")
                .arg(s.paper.bars)
                .arg(s.paper.warmup_completed)
                .arg(s.paper.warmup_required)};
        for (std::size_t n = 0; n < values.size(); ++n)
            u.account[n]->setText(values[n]);
    } else
        for (auto *value : u.account)
            value->setText("—");
    u.orders->setRowCount(static_cast<int>(s.paper.recent_orders.size()));
    for (std::size_t n = 0; n < s.paper.recent_orders.size(); ++n) {
        const auto &p = s.paper.recent_orders[n];
        const std::array<QString, 6> values{QString::number(p.order.id),
                                            p.order.request.side == Side::Buy ? "Buy" : "Sell",
                                            QString::number(p.order.request.quantity),
                                            orderStatus(p.order.status),
                                            price(p.protection),
                                            QString::fromStdString(p.order.reason)};
        for (int col = 0; col < 6; ++col)
            u.orders->setItem(static_cast<int>(n), col, new QTableWidgetItem(values[col]));
    }
    emit statusChanged(
        QString(s.synthetic ? "Synthetic demo: %1 · paper %2 · %3 fills" : "Live: %1 · paper %2 · %3 fills")
            .arg(QString::fromStdString(s.state), QString::fromStdString(s.paper.state))
            .arg(s.paper.fills));
    if (closing_ && !isBusy()) {
        closing_ = false;
        QTimer::singleShot(0, window(), &QWidget::close);
    }
}
void LiveTradingPage::exportRecording() {
    auto &s = ui_->snapshot;
    if (controller_->busy() || !s.recording_ok)
        return;
    auto path = QFileDialog::getSaveFileName(this, "Export sealed session",
                                             QString::fromStdString(s.session) + ".jsonl", "JSONL (*.jsonl)");
    if (path.isEmpty())
        return;
    QFile source(QString::fromStdString(s.recording));
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(path).absoluteFilePath())
        return;
    QSaveFile destination(path);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        ui_->message->setText("Could not open export files");
        return;
    }
    while (!source.atEnd()) {
        auto bytes = source.read(65536);
        if (bytes.isEmpty() || destination.write(bytes) != bytes.size()) {
            ui_->message->setText("Session export failed");
            return;
        }
    }
    ui_->message->setText(destination.commit() ? "Session exported" : "Session export failed");
}
void LiveTradingPage::exportPerformance() {
    const auto &s = ui_->snapshot;
    auto report = QString("# M3 live diagnostics\n\nSession %1. Source: %2. OS: %3. Runtime %4 s. Throughput "
                          "%5 normalized inputs/s.\n\n| Measurement | Count | p50 us | p95 us | p99 us | "
                          "p99.9 us | Max us |\n|---|---:|---:|---:|---:|---:|---:|\n")
                      .arg(QString::fromStdString(s.session), QString::fromStdString(s.entitlement),
                           QSysInfo::prettyProductName())
                      .arg(s.performance.duration_seconds)
                      .arg(s.performance.events_per_second);
    auto row = [&](const QString &name, const TimingSummary &t) {
        report += QString("| %1 | %2 | %3 | %4 | %5 | %6 | %7 |\n")
                      .arg(name)
                      .arg(t.count)
                      .arg(t.p50_us)
                      .arg(t.p95_us)
                      .arg(t.p99_us ? QString::number(*t.p99_us) : "insufficient samples")
                      .arg(t.p999_us ? QString::number(*t.p999_us) : "insufficient samples")
                      .arg(t.maximum_us);
    };
    row("Queue wait", s.performance.queue_wait);
    row("Core apply", s.performance.core_processing);
    row("Local receipt/enqueue to audit enqueue", s.performance.receipt_to_commit);
    row("Decision to order transition", s.performance.decision_to_submit);
    row("Snapshot publication to GUI observation", ui_->gui_observation_age.summary());
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    report += QString("\nCurrent publication-to-GUI-observation age: %1 us (single sample, not paint "
                      "completion). Queue high %2; recorder high %3; input overflow %4. CPU process clock %5 "
                      "s (cumulative, not utilization).\n")
                  .arg(s.published_ns ? (now - s.published_ns) / 1000. : 0)
                  .arg(s.queue_high)
                  .arg(s.performance.recorder_high)
                  .arg(s.performance.input_overflows)
                  .arg(double(std::clock()) / CLOCKS_PER_SEC);
#ifdef __linux__
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        report += QString("\nProcess peak RSS: %1 KiB.\n").arg(usage.ru_maxrss);
#endif
    report += "\nBounded timing samples: up to 100,000 each; percentiles use retained samples. Allocation "
              "counts and actual paint-completion latency are unmeasured. Source timestamps are not one-way "
              "network latency. Synthetic accelerated inputs do not establish provider throughput. Paper "
              "fills ignore liquidity, queue position and impact.\n";
    auto path = QFileDialog::getSaveFileName(this, "Export diagnostics report", "M3_live_diagnostics.md",
                                             "Markdown (*.md)");
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    auto bytes = report.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        ui_->message->setText("Could not write diagnostics report");
    else
        ui_->message->setText("Diagnostics report exported");
}
} // namespace trading_app_desktop
