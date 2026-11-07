#include "LiveTradingPage.hpp"
#include "MarketChart.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

namespace trading_app_desktop {
LiveTradingPage::LiveTradingPage(QWidget* parent) : QWidget(parent) {
    setObjectName("liveTradingPage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* heading = new QLabel("LIVE TRADING   /   PAPER SESSION", this);
    heading->setObjectName("eyebrow"); 
    layout->addWidget(heading);
    auto* description = new QLabel(
        "Live market data and paper execution will have their own session here. No feed is connected yet.", this);
    description->setWordWrap(true); 
    layout->addWidget(description);

    auto* split = new QSplitter(Qt::Horizontal, this); 
    layout->addWidget(split, 1);
    auto* market = new QWidget(split); auto* left = new QVBoxLayout(market);
    left->setContentsMargins(0, 0, 14, 0);
    auto* instrument = new QComboBox(market); 
    instrument->setObjectName("liveInstrument");
    instrument->addItems({"SPY", "AAPL", "AMZN", "GOOGL", "NVDA", "TSLA"});
    instrument->setAccessibleName("Live session instrument"); 
    left->addWidget(instrument);
    auto* header = new QLabel("SPY · waiting for live market data", market);
    header->setObjectName("liveMarketHeader"); 
    left->addWidget(header);
    auto* price = new QLabel("— USD", market); 
    price->setObjectName("liveMarketPrice"); 
    left->addWidget(price);
    left->addWidget(new QLabel("No quote or exchange timestamp received", market));
    auto* chart = new MarketChart(market); 
    chart->setObjectName("liveMarketChart");
    chart->setEmptyMessage("Live price and share volume\nWaiting for a streaming feed");
    left->addWidget(chart, 3);
    auto* indicatorCard = new QGroupBox("INDICATOR PANEL", market);
    auto* indicatorLayout = new QVBoxLayout(indicatorCard);
    auto* indicatorValue = new QLabel("RSI (14) · —", indicatorCard);
    indicatorValue->setObjectName("liveIndicatorValue"); 
    indicatorLayout->addWidget(indicatorValue);
    auto* indicatorNote = new QLabel(
        "Reserved for indicator calculations on completed live bars. No indicator values have been calculated.", indicatorCard);
    indicatorNote->setWordWrap(true); 
    indicatorLayout->addWidget(indicatorNote);
    indicatorCard->setMinimumHeight(100); 
    left->addWidget(indicatorCard, 1);

    auto* scroll = new QScrollArea(split); scroll->setWidgetResizable(true); 
    scroll->setMinimumWidth(355);
    auto* panel = new QWidget; auto* right = new QVBoxLayout(panel);
    auto* connection = new QGroupBox("CONNECTION", panel); auto* connectionLayout = new QVBoxLayout(connection);
    connectionLayout->addWidget(new QLabel("NOT CONNECTED", connection));
    auto* connectionNote = new QLabel("Choose and implement a streaming provider in the next stage.", connection);
    connectionNote->setWordWrap(true); connectionLayout->addWidget(connectionNote);
    auto* connectButton = new QPushButton("Connect feed", connection);
    connectButton->setObjectName("connectLiveFeed"); connectButton->setEnabled(false);
    connectButton->setToolTip("A live feed adapter has not been implemented yet."); 
    connectionLayout->addWidget(connectButton);
    right->addWidget(connection);

    auto* indicators = new QGroupBox("INDICATOR SETTINGS", panel); 
    auto* form = new QFormLayout(indicators);
    auto* kind = new QComboBox(indicators); 
    kind->setObjectName("liveIndicatorKind");
    kind->addItems({"RSI", "SMA", "EMA"});
    auto* period = new QSpinBox(indicators); 
    period->setObjectName("liveIndicatorPeriod"); 
    period->setRange(2, 10000); period->setValue(14);
    form->addRow("Indicator", kind); form->addRow("Period (bars)", period);
    auto* setupNote = new QLabel("Selection only. Calculation and bar construction will be connected later.", indicators);
    setupNote->setWordWrap(true); form->addRow(setupNote); 
    right->addWidget(indicators);

    auto* session = new QGroupBox("PAPER SESSION", panel); 
    auto* sessionLayout = new QVBoxLayout(session);
    auto* start = new QPushButton("Start live paper session", session); 
    start->setEnabled(false);
    auto* stop = new QPushButton("Stop live paper session", session); 
    stop->setEnabled(false);
    start->setToolTip("Live session lifecycle is not connected yet."); 
    stop->setToolTip(start->toolTip());
    sessionLayout->addWidget(start); 
    sessionLayout->addWidget(stop);
    auto* values = new QFormLayout;
    for (const auto* name : {"Cash (USD)", "Holdings", "Equity (USD)", "PnL (USD)", "Events received"})
        values->addRow(QString::fromUtf8(name), new QLabel("—", session));
    sessionLayout->addLayout(values);
    auto* sessionNote = new QLabel("No account or orders exist until a live paper session is implemented and started.", session);
    sessionNote->setWordWrap(true); 
    sessionLayout->addWidget(sessionNote); 
    right->addWidget(session);
    right->addStretch(); 
    scroll->setWidget(panel);
    split->setChildrenCollapsible(false); 
    split->setStretchFactor(0, 72); 
    split->setStretchFactor(1, 28);
    split->setSizes({980, 390});

    connect(instrument, &QComboBox::currentTextChanged, this, [header](const QString& symbol) {
        header->setText(symbol + " · waiting for live market data");
    });
    const auto updateIndicator = [kind, period, indicatorValue] {
        indicatorValue->setText(QString("%1 (%2) · —").arg(kind->currentText()).arg(period->value()));
    };
    connect(kind, &QComboBox::currentTextChanged, this, updateIndicator);
    connect(period, &QSpinBox::valueChanged, this, updateIndicator);
}
}
