#pragma once
#include <chrono>
#include <string>
#include <cstdint>
#include <string_view>
#include <optional>
#include <variant>
#include <vector>

namespace trading_app_cli{
    inline constexpr std::int64_t usd_scale = 10'000;

    struct Price
    {
        std::int64_t units{};
        bool operator==(const Price&) const = default;
    };

    struct Money
    {
        std::int64_t units{};
        bool operator==(const Money&) const = default;
    };

    using Quantity = std::int64_t;
    using InstrumentId = std::uint64_t;
    using StrategyId = std::string;
    using OrderId = std::uint64_t;
    using ExecutionId = std::uint64_t;
    using EventTime = std::chrono::sys_time<std::chrono::nanoseconds>;

    using SourceLocalTime = std::chrono::local_time<std::chrono::seconds>;

    enum class Currency { USD };
    enum class Side { Buy, Sell };
    enum class OrderType { Market };
    enum class OrderLifetime { UntilEndOfRun };
    enum class OrderStatus { Submitted, Accepted, Filled, Rejected, Expired };

    struct Instrument
    {
        InstrumentId id{};
        std::string symbol;
        Currency currency{Currency::USD};
        std::int64_t price_scale{usd_scale};
        Quantity whole_share_step{1};
        bool operator==(const Instrument&) const=default;
    };

    struct BarEvent
    {
        Instrument instrument;
        EventTime start{};
        EventTime end{};
        Price open{};
        Price high{};
        Price low{};
        Price close{};
        Quantity volume{};
        std::uint64_t source_sequence{};
        bool operator==(const BarEvent&) const = default;
    };
    
    struct OrderRequest {
        Instrument instrument;
        StrategyId strategy_id;
        Side side{Side::Buy};
        Quantity quantity{};
        OrderType type{OrderType::Market};
        bool operator==(const OrderRequest&) const = default;
    };

    struct Order {
        OrderId id{};
        OrderRequest request;
        EventTime submission_time{};
        std::uint64_t submission_sequence{};
        OrderStatus status{OrderStatus::Submitted};
        OrderLifetime lifetime{OrderLifetime::UntilEndOfRun};
        std::uint64_t submission_dispatch_sequence{};
        std::string reason;

        bool operator==(const Order&) const = default;
    };

    struct ExecutionReport {
        OrderId order_id{};
        ExecutionId execution_id{};
        Instrument instrument;
        EventTime fill_time{};
        Side side{Side::Buy};
        Quantity quantity{};
        Price price{};
        Money fee{};
        OrderStatus status{OrderStatus::Filled};
        std::string reason;
        bool operator==(const ExecutionReport&) const = default;
    };
    struct Position {
        Instrument instrument;
        Quantity quantity{}; 
        Money remaining_acquisition_cost{}; 
        std::optional<Price> mark;
        bool operator==(const Position&) const = default;
    };


    enum class BarReadStatus {Bar, EndOfData};

    struct RawBarRow
    {
        SourceLocalTime local_start{};
        Price open{};
        Price high{};
        Price low{};
        Price close{};
        Quantity volume{};
        std::size_t line_number{};
        std::uint64_t source_sequence{};
        bool operator==(const RawBarRow&) const = default;
    };
    struct RawBarReadResult
    {
        BarReadStatus status{BarReadStatus::EndOfData};
        std::optional<RawBarRow> row;
    };

    struct BarReadResult {
        BarReadStatus status{BarReadStatus::EndOfData};
        std::optional<BarEvent> bar;
    };

    struct HistoricalFeedConfiguration
    {
        Instrument instrument;
        std::string source_timezone;
        std::chrono::seconds bar_interval{60};
        std::string session_coverage{"unknown"};
        std::string price_adjustment_assumption{"unknown"};
    };

    struct FeedStatistics {
        std::uint64_t bars_read{}; 
        std::uint64_t gaps{};     
        std::optional<EventTime> first_start;
        std::optional<EventTime> last_end;
        bool end_of_data{};        
        bool failed{};       
    };
    
    struct ExecutionConfiguration
    {
        Money flat_fee{};
        std::int64_t slippage_units{};
    };

    struct FillProposal
    {
        OrderId order_id{};
        Instrument instrument;
        EventTime fill_time{};
        std::uint64_t source_sequence{};

        Side side{Side::Buy};
        Quantity quantity{};
        Price price{};
        Money fee{};

        OrderStatus status{OrderStatus::Filled};
        std::string reason;
        std::uint64_t dispatch_sequence{};

        bool operator ==(const FillProposal&) const = default;

    };

    struct TradeEvent
    {
        Instrument instrument;
        EventTime source_time{};
        Price price{};
        Quantity quantity{};
        std::optional<std::uint64_t> source_sequence;
        bool operator ==(const TradeEvent&) const = default;
    };

    struct QuoteEvent
    {
        Instrument instrument;
        EventTime source_time{};
        Price bid{};
        Price ask{};
        std::optional<Quantity> bde_size;
        std::optional<Quantity> ask_size;
        std::optional<std::uint64_t> source_sequence;
        bool operator ==(const QuoteEvent&) const = default;
    };
    
    using MarketDataEvent = std::variant<BarEvent, TradeEvent, QuoteEvent>;
    enum class MarketDataReadStatus { Event, NoDataAvailable, EndOfData };
    struct MarketDataReadResult {
        MarketDataReadStatus status{MarketDataReadStatus::EndOfData};
        std::optional<MarketDataEvent> event;
    };

    struct EventContext {
        EventTime processing_time{};
        std::uint64_t dispatch_sequence{};
        bool operator==(const EventContext&) const = default;
    };
    struct BarOpenPrice {
        Price open{};
        bool operator==(const BarOpenPrice&) const = default;
    };
    struct TradePrice {
        Price price{};
        bool operator==(const TradePrice&) const = default;
    };
    struct QuotePrices {
        Price bid{};
        Price ask{};
        bool operator==(const QuotePrices&) const = default;
    };
    struct ExecutionOpportunity {
        Instrument instrument;
        EventContext context;
        std::optional<std::uint64_t> source_sequence;
        std::variant<BarOpenPrice, TradePrice, QuotePrices> prices;
        bool operator==(const ExecutionOpportunity&) const = default;
    };

    enum class SimulationMode { HistoricalReplay, LivePaper };
    struct SimulationConfiguration {
        SimulationMode mode{SimulationMode::HistoricalReplay};
        Instrument instrument;
        Money initial_cash{};
        ExecutionConfiguration execution;
        std::string dataset_path;
        std::string source_timezone;
        std::string session_coverage{"unknown"};
        std::string price_adjustment_assumption{"unknown"};
    };

    enum class StrategyKind{DoNothing, Scripted, Momentum, VNMomentum};

    struct ScriptedDecision
    {
        std::uint64_t completed_bar_index{};
        Side side{Side::Buy};
        Quantity quantity{};
        bool operator ==(const ScriptedDecision&) const = default;
    };

    

    struct StrategyConfiguration {
        StrategyKind kind{StrategyKind::DoNothing};
        StrategyId strategy_id{"default"};
        std::vector<ScriptedDecision> decisions;
        std::size_t lookback{1};
        Quantity buy_quantity{1};
        // VNMomentum vnmomentum;
        double threshold{};
        double volFloor= 1e-8;
    };

    class PendingOrderView {
        public:
            explicit PendingOrderView(const std::vector<Order>& records = {}) {
                for (const auto& order : records)
                    if (order.status == OrderStatus::Accepted) orders_.push_back(order);
            }
            bool hasPending(InstrumentId instrument) const noexcept {
                for (const auto& order : orders_)
                    if (order.request.instrument.id == instrument) return true;
                return false;
            }
            const std::vector<Order>& orders() const noexcept { return orders_; }
        private:
            std::vector<Order> orders_;
    };

    enum class RunStatus { Ready, Running, Completed, Failed };
    struct RunConfiguration {
        SimulationConfiguration simulation;
        StrategyConfiguration strategy;
        std::size_t market_history_capacity{64};
        std::size_t close_observation_capacity{0}; // zero disables observations
    };
    struct RunStatistics {
        std::uint64_t completed_bars{};
        std::uint64_t gaps{}; // discontinuities, not missing-bar count
        std::uint64_t requests_submitted{};
        std::uint64_t requests_accepted{};
        std::uint64_t requests_rejected{};
        std::uint64_t execution_attempts{}; // non-null proposals only
        std::uint64_t executions_filled{};
        std::uint64_t executions_rejected{};
        std::uint64_t orders_expired{};
        bool operator==(const RunStatistics&) const = default;
    };

} // namspace trading_app_cli

// #pragma once
// #include <chrono>
// #include <string>
// #include <cstdint>
// #include <string_view>
// #include <optional>

// namespace trading_app_cli{
    

// } // namespace trading_app_cli