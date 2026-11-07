#pragma once
#include "core/OrderManager.hpp"
#include "engine/SimulationClock.hpp"
#include "execution/PaperBroker.hpp"
#include "marketdata/IMarketDataFeed.hpp"
#include "marketstate/MarketState.hpp"
#include "strategy/ITradingModel.hpp"
#include <deque>
#include <functional>
#include <memory>
namespace trading_app_cli {
struct ExecutionObservation {
    ExecutionReport report;
    EventContext context;
    std::optional<std::uint64_t> source_sequence;
    bool operator==(const ExecutionObservation&) const = default;
};
struct CloseObservation {
    EventContext context;
    std::uint64_t source_sequence{};
    PortfolioSnapshot portfolio;
};
struct RunResult {
    RunStatus status{RunStatus::Ready};
    PortfolioSnapshot portfolio;
    std::optional<Price> final_mark;
    std::vector<Order> orders;
    std::vector<ExecutionObservation> executions;
    std::vector<CloseObservation> closes;
    RunStatistics statistics;
    RunConfiguration configuration;
    std::optional<EventTime> final_time;
    std::string failure;
};
using CloseObserver = std::function<void(const CloseObservation&)>;
class TradingEngine final {
public:
    TradingEngine(RunConfiguration configuration, std::unique_ptr<IFillModel> model);
    TradingEngine(RunConfiguration configuration, std::unique_ptr<IFillModel> model,
        std::unique_ptr<ITradingModel> strategy, CloseObserver observer = {});
    TradingEngine(const TradingEngine&) = delete;
    TradingEngine& operator=(const TradingEngine&) = delete;
    TradingEngine(TradingEngine&&) = delete;
    TradingEngine& operator=(TradingEngine&&) = delete;
    RunResult run(IMarketDataFeed& feed);
    RunStatus status() const noexcept;
    RunResult result() const; 
private:
    RunConfiguration configuration_;
    SimulationClock clock_;
    MarketState market_;
    PortfolioAccounting account_;
    std::shared_ptr<OrderManager> orders_;
    PaperBroker broker_;
    std::unique_ptr<ITradingModel> strategy_;
    RunStatus status_{RunStatus::Ready};
    RunStatistics statistics_;
    std::uint64_t dispatch_sequence_{};
    ExecutionId execution_id_{};
    std::optional<EventTime> previous_end_;
    std::uint64_t previous_source_sequence_{};
    std::vector<ExecutionObservation> executions_;
    std::deque<CloseObservation> closes_;
    std::string failure_;
    CloseObserver close_observer_;
    void processEvent(const MarketDataEvent& event);
    void processBar(const BarEvent& bar);
    void validateBar(const BarEvent& bar) const;
    void processOpening(const BarEvent& bar);
    void executePending(const ExecutionOpportunity& opportunity);
    void processProposal(const FillProposal& proposal, const ExecutionOpportunity& opportunity);
    void processCompletedBar(const BarEvent& bar);
    PendingOrderView makePendingView() const;
    void submitRequests(const std::vector<OrderRequest>& requests, const EventContext& context);
    std::uint64_t nextDispatchSequence();
    ExecutionId nextExecutionId();
    void finalize();
    RunResult buildResult() const;
};
} // namespace trading_app_cli
