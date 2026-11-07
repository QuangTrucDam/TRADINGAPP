
flowchart TD
    subgraph Inputs["Market-data inputs"]
        Live["Live provider adapter"]
        Historical["Historical reader"]
        Recorded["Recorded-session reader"]
        Synthetic["Synthetic feed"]
    end

    Live --> Normalize["Normalization and validation"]
    Historical --> Normalize
    Recorded --> Normalize
    Synthetic --> Normalize

    Normalize --> Queue["Bounded input queue"]
    Queue --> Engine["TradingEngine: clock and event dispatch"]

    subgraph Core["Trading core — single state owner"]
        Engine --> Market["MarketState and BarService"]
        Market --> Strategy["Trading strategy"]
        Strategy --> Risk["Risk checks and reservations"]
        Risk --> OMS["Order management"]
        OMS --> Broker["PaperBroker and FillModel"]
        Broker --> Accounting["Fill accounting and Portfolio"]

        Accounting --> Strategy
        Accounting --> Risk
        OMS --> Risk
        Market --> Broker
    end

    Engine --> Storage["Journal, checkpoints, and run records"]
    Engine --> Snapshots["Immutable UI snapshots"]
    Snapshots --> UI["Qt dashboard"]
    UI --> Commands["Start, stop, and configuration commands"]
    Commands --> Engine


TradingSimulation/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
│
├── apps/
│   ├── cli/
│   │   └── main.cpp
│   └── desktop/
│       └── main.cpp
│
├── src/
│   ├── core/
│   │   ├── Types.hpp
│   │   ├── Instrument.hpp
│   │   ├── MarketEvent.hpp
│   │   ├── Order.hpp
│   │   ├── ExecutionReport.hpp
│   │   ├── ApplicationCommand.hpp
│   │   └── RunConfiguration.hpp
│   │
│   ├── utils/
│   │   ├── Logger.hpp
│   │   ├── ConfigLoader.hpp
│   │   ├── ConfigLoader.cpp
│   │   └── ScopedTimer.hpp
│   │
│   ├── marketdata/
│   │   ├── IMarketDataFeed.hpp
│   │   ├── LiveProviderFeed.hpp
│   │   ├── LiveProviderFeed.cpp
│   │   ├── HistoricalFeed.hpp
│   │   ├── HistoricalFeed.cpp
│   │   ├── RecordedSessionFeed.hpp
│   │   ├── RecordedSessionFeed.cpp
│   │   ├── SyntheticFeed.hpp
│   │   ├── SyntheticFeed.cpp
│   │   ├── MarketDataNormalizer.hpp
│   │   ├── MarketDataValidator.hpp
│   │   ├── RateLimiter.hpp
│   │   └── RequestScheduler.hpp
│   │
│   ├── marketstate/
│   │   ├── MarketState.hpp
│   │   ├── MarketState.cpp
│   │   ├── BarService.hpp
│   │   ├── BarService.cpp
│   │   └── MarketCalendar.hpp
│   │
│   ├── pricing/
│   │   ├── IMarkPricePolicy.hpp
│   │   ├── LastTradeMarkPolicy.hpp
│   │   ├── MidQuoteMarkPolicy.hpp
│   │   └── BarCloseMarkPolicy.hpp
│   │
│   ├── strategy/
│   │   ├── ITradingModel.hpp
│   │   ├── DoNothingStrategy.hpp
│   │   ├── EqualWeightStrategy.hpp
│   │   ├── EqualWeightStrategy.cpp
│   │   ├── MomentumStrategy.hpp
│   │   └── MomentumStrategy.cpp
│   │
│   ├── risk/
│   │   ├── IRiskManager.hpp
│   │   ├── RiskDecision.hpp
│   │   ├── RiskManager.hpp
│   │   └── RiskManager.cpp
│   │
│   ├── oms/
│   │   ├── OrderManager.hpp
│   │   ├── OrderManager.cpp
│   │   └── ReservationBook.hpp
│   │
│   ├── execution/
│   │   ├── IExecutionVenue.hpp
│   │   ├── IFillModel.hpp
│   │   ├── PaperBroker.hpp
│   │   ├── PaperBroker.cpp
│   │   ├── NextBarOpenFillModel.hpp
│   │   └── SubsequentQuoteFillModel.hpp
│   │
│   ├── portfolio/
│   │   ├── Position.hpp
│   │   ├── PortfolioSnapshot.hpp
│   │   ├── PortfolioAccounting.hpp
│   │   └── PortfolioAccounting.cpp
│   │
│   ├── engine/
│   │   ├── TradingEngine.hpp
│   │   ├── TradingEngine.cpp
│   │   ├── EngineEvent.hpp
│   │   ├── BoundedEventQueue.hpp
│   │   ├── IClock.hpp
│   │   ├── LiveClock.hpp
│   │   └── SimulationClock.hpp
│   │
│   ├── storage/
│   │   ├── IMarketDataStore.hpp
│   │   ├── IRunRecorder.hpp
│   │   ├── CsvReader.hpp
│   │   ├── CsvReader.cpp
│   │   ├── SqliteStore.hpp
│   │   ├── SqliteStore.cpp
│   │   ├── RunRecorder.hpp
│   │   └── RunRecorder.cpp
│   │
│   ├── analytics/
│   │   ├── PerformanceAnalytics.hpp
│   │   └── PerformanceAnalytics.cpp
│   │
│   ├── observability/
│   │   ├── LatencyMetrics.hpp
│   │   └── EngineHealth.hpp
│   │
│   └── hmi/
│       ├── MainWindow.hpp
│       ├── MainWindow.cpp
│       ├── ChartWidget.hpp
│       ├── ChartWidget.cpp
│       ├── PresentationBridge.hpp
│       ├── PresentationBridge.cpp
│       ├── PortfolioModel.hpp
│       ├── PositionsModel.hpp
│       ├── OrdersModel.hpp
│       └── resources/
│           └── dark.qss
│
├── tests/
│   ├── unit/
│   ├── integration/
│   └── fixtures/
│
├── benchmarks/
│   ├── ReplayBenchmark.cpp
│   └── EventProcessingBenchmark.cpp
│
├── configs/
│   ├── replay.json
│   └── live.example.json
│
├── data/
│   ├── samples/
│   └── README.md
│
└── docs/
    ├── architecture.md
    ├── execution-assumptions.md
    └── benchmark-results.md
    