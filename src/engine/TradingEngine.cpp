#include "engine/TradingEngine.hpp"
#include "core/Validation.hpp"
#include "strategy/StrategyFactory.hpp"
#include <stdexcept>
#include <utility>
namespace trading_app_cli {
namespace {
RunConfiguration validated(RunConfiguration config) {
    validateInstrument(config.simulation.instrument);
    if (config.simulation.mode != SimulationMode::HistoricalReplay)
        throw std::invalid_argument("M1 engine supports HistoricalReplay only");
    if (config.simulation.initial_cash.units <= 0
        || config.simulation.execution.flat_fee.units < 0
        || config.simulation.execution.slippage_units < 0)
        throw std::invalid_argument("Invalid run capital/execution configuration");
    if (config.market_history_capacity == 0
        || config.market_history_capacity > std::deque<BarEvent>{}.max_size()
        || config.close_observation_capacity > std::deque<CloseObservation>{}.max_size())
        throw std::invalid_argument("Invalid run history capacity");
    validateStrategyConfiguration(config.strategy, config.simulation.instrument);
    return config;
}
void increment(std::uint64_t& value) { value = checkedNext(value); }
}
TradingEngine::TradingEngine(RunConfiguration config, std::unique_ptr<IFillModel> model)
    : TradingEngine(config, std::move(model), createStrategy(config.strategy, config.simulation.instrument)) {}
TradingEngine::TradingEngine(RunConfiguration config, std::unique_ptr<IFillModel> model,
    std::unique_ptr<ITradingModel> strategy, CloseObserver observer)
    : configuration_(validated(std::move(config))),
      market_(configuration_.simulation.instrument, configuration_.market_history_capacity),
      account_(configuration_.simulation.initial_cash), orders_(std::make_shared<OrderManager>(configuration_.simulation.instrument)),
      broker_(orders_, std::move(model)), strategy_(std::move(strategy)),  close_observer_(std::move(observer)) {
    if (!strategy_) throw std::invalid_argument("Engine requires a strategy");
    strategy_->reset();
}
RunStatus TradingEngine::status() const noexcept { return status_; }
RunResult TradingEngine::result() const { return buildResult(); }
RunResult TradingEngine::run(IMarketDataFeed& feed) {
    if (status_ != RunStatus::Ready) throw std::logic_error("TradingEngine is single-use");
    status_ = RunStatus::Running;
    try {
        for (;;) {
            const auto read = feed.nextEvent();
            switch (read.status) {
            case MarketDataReadStatus::Event:
                if (!read.event) throw std::logic_error("Event status requires a payload");
                processEvent(*read.event);
                break;
            case MarketDataReadStatus::EndOfData:
                if (read.event) throw std::logic_error("EOF must not contain an event");
                if (statistics_.completed_bars == 0) throw std::invalid_argument("Empty historical run");
                finalize();
                return buildResult();
            case MarketDataReadStatus::NoDataAvailable:
                throw std::logic_error("Historical runner cannot process NoDataAvailable");
            default: throw std::logic_error("Unknown feed status");
            }
        }
    } catch (const std::exception& error) {
        status_ = RunStatus::Failed;
        failure_ = error.what();
        throw;
    } catch (...) {
        status_ = RunStatus::Failed;
        failure_ = "Unknown run failure";
        throw;
    }
}
void TradingEngine::processEvent(const MarketDataEvent& event) {
    const auto* bar = std::get_if<BarEvent>(&event);
    if (!bar) throw std::invalid_argument("M1 historical engine accepts bars only");
    processBar(*bar);
}
void TradingEngine::validateBar(const BarEvent& bar) const {
    validateHistoricalBar(bar, configuration_.simulation.instrument);
    if (previous_end_ && (bar.start < *previous_end_
        || bar.source_sequence <= previous_source_sequence_))
        throw std::invalid_argument("Duplicate/overlapping/backward historical event");
    (void)checkedNext(checkedNext(dispatch_sequence_));
    (void)checkedNext(statistics_.completed_bars);
    if (previous_end_ && bar.start > *previous_end_) (void)checkedNext(statistics_.gaps);
}
void TradingEngine::processBar(const BarEvent& bar) {
    validateBar(bar);
    processOpening(bar);
    processCompletedBar(bar);
    if (previous_end_ && bar.start > *previous_end_) increment(statistics_.gaps);
    increment(statistics_.completed_bars);
    previous_end_ = bar.end;
    previous_source_sequence_ = bar.source_sequence;
    if (close_observer_)
        close_observer_({{clock_.now(), dispatch_sequence_}, bar.source_sequence, account_.snapshot()});
}
std::uint64_t TradingEngine::nextDispatchSequence() {
    dispatch_sequence_ = checkedNext(dispatch_sequence_);
    return dispatch_sequence_;
}
ExecutionId TradingEngine::nextExecutionId() {
    execution_id_ = checkedNext(execution_id_);
    return execution_id_;
}
void TradingEngine::processOpening(const BarEvent& bar) {
    if (!clock_.initialized()) clock_.initialize(bar.start);
    else clock_.advanceTo(bar.start);
    ExecutionOpportunity opportunity;
    opportunity.instrument = bar.instrument;
    opportunity.context = {clock_.now(), nextDispatchSequence()};
    opportunity.source_sequence = bar.source_sequence;
    opportunity.prices = BarOpenPrice{bar.open};
    executePending(opportunity);
}
void TradingEngine::executePending(const ExecutionOpportunity& opportunity) {
    const auto ids = broker_.eligibleOrders(opportunity);
    for (const auto id : ids) {
        const auto proposal = broker_.proposeFill(id, opportunity);
        if (proposal) processProposal(*proposal, opportunity);
    }
}
void TradingEngine::processProposal(const FillProposal& proposal, const ExecutionOpportunity& opportunity) {
    ExecutionReport report;
    report.order_id = proposal.order_id;
    report.execution_id = nextExecutionId();
    report.instrument = proposal.instrument;
    report.fill_time = proposal.fill_time;
    report.side = proposal.side;
    report.quantity = proposal.quantity;
    report.price = proposal.price;
    report.fee = proposal.fee;
    report.status = proposal.status;
    report.reason = proposal.reason;
    (void)checkedNext(statistics_.execution_attempts);
    (void)checkedNext(statistics_.executions_filled);
    (void)checkedNext(statistics_.executions_rejected);
    if (report.status == OrderStatus::Filled) {
        const auto applied = account_.applyFill(report);
        if (applied.status == FillApplyStatus::Rejected) {
            if (applied.reason != FillRejectReason::InsufficientCash
                && applied.reason != FillRejectReason::InsufficientHoldings)
                throw std::logic_error("Validated proposal failed accounting validation: " + applied.detail);
            report.status = OrderStatus::Rejected;
            report.quantity = 0;
            report.price = {};
            report.fee = {};
            report.reason = applied.detail;
        } else if (applied.status != FillApplyStatus::Applied
            && applied.status != FillApplyStatus::AlreadyApplied) {
            throw std::logic_error("Unknown accounting outcome");
        }
    }
    broker_.recordExecution(report);
    increment(statistics_.execution_attempts);
    if (report.status == OrderStatus::Filled) increment(statistics_.executions_filled);
    else increment(statistics_.executions_rejected);
    executions_.push_back({report, opportunity.context, opportunity.source_sequence});
}
PendingOrderView TradingEngine::makePendingView() const { return PendingOrderView{broker_.orders()}; }
void TradingEngine::processCompletedBar(const BarEvent& bar) {
    clock_.advanceTo(bar.end);
    const EventContext context{clock_.now(), nextDispatchSequence()};
    market_.onCompletedBar(bar, context.processing_time);
    account_.updateMark(bar.instrument.id, bar.close);
    const auto snapshot = account_.snapshot();
    const auto pending = makePendingView();
    const auto requests = strategy_->onBar(bar, snapshot, pending);
    submitRequests(requests, context);
    if (configuration_.close_observation_capacity != 0) {
        closes_.push_back({context, bar.source_sequence, snapshot});
        if (closes_.size() > configuration_.close_observation_capacity) closes_.pop_front();
    }
}
void TradingEngine::submitRequests(const std::vector<OrderRequest>& requests, const EventContext& context) {
    for (const auto& request : requests) {
        (void)checkedNext(statistics_.requests_submitted);
        (void)checkedNext(statistics_.requests_accepted);
        (void)checkedNext(statistics_.requests_rejected);
        const auto id = broker_.submit(request, context);
        increment(statistics_.requests_submitted);
        if (orders_->order(id).status == OrderStatus::Accepted) increment(statistics_.requests_accepted);
        else increment(statistics_.requests_rejected);
    }
}
void TradingEngine::finalize() {
    if (status_ == RunStatus::Completed) return;
    if (status_ != RunStatus::Running) throw std::logic_error("Cannot finalize this run");
    std::uint64_t expired = 0;
    for (const auto& order : broker_.orders())
        if (order.status == OrderStatus::Accepted) expired = checkedNext(expired);
    broker_.expirePendingAtEnd();
    statistics_.orders_expired = expired;
    status_ = RunStatus::Completed;
}
RunResult TradingEngine::buildResult() const {
    RunResult result;
    result.status = status_;
    result.portfolio = account_.snapshot();
    result.final_mark = market_.lastClose(configuration_.simulation.instrument.id);
    result.orders = broker_.orders();
    result.executions = executions_;
    result.closes.assign(closes_.begin(), closes_.end());
    result.statistics = statistics_;
    result.configuration = configuration_;
    if (clock_.initialized()) result.final_time = clock_.now();
    result.failure = failure_;
    return result;
}
} // namespace trading_app_cli
