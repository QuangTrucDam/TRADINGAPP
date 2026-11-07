#include "OrderManager.hpp"

#include <limits>
#include <stdexcept>
#include <utility>
namespace trading_app_cli {
namespace{

    bool supportedInstrument(const Instrument& instrument)
    {
        return instrument.id != 0 && !instrument.symbol.empty()
            && instrument.currency == Currency::USD
            && instrument.price_scale == usd_scale
            && instrument.whole_share_step == 1;
    }
    bool validOutcome(OrderStatus status, Quantity quantity, Price price,
                    Money fee, const std::string& reason,
                    Quantity requestedQuantity)
    {
        if (status == OrderStatus::Filled) {
            return quantity == requestedQuantity && quantity > 0
                && price.units > 0 && fee.units >= 0 && reason.empty();
        }
        if (status == OrderStatus::Rejected) {
            return quantity == 0 && price.units == 0 && fee.units == 0
                && !reason.empty();
        }
        return false;
    }

}//namespace

OrderManager::OrderManager(Instrument instrument) : instrument_(std::move(instrument)) {
    if (!supportedInstrument(instrument_))
        throw std::invalid_argument("OrderManager: unsupported instrument");
}

std::string OrderManager::rejectionReason(const OrderRequest request, std::uint64_t sequence) const{
    if (request.instrument != instrument_) {
        return "Order instrument does not match the configured instrument";
    }
    if (request.type != OrderType::Market) {
        return "Only market orders are supported";
    }
    if (request.side != Side::Buy && request.side != Side::Sell) {
        return "Unsupported order side";
    }
    if (request.quantity <= 0) {
        return "Order quantity must be positive whole shares";
    }
    if (request.strategy_id.empty()) {
        return "Strategy ID must not be empty";
    }
    if (sequence == 0) {
        return "Submission sequence must be positive";
    }
    return {};
}

OrderId OrderManager::submit(const OrderRequest& request, EventTime submttedAt, std::uint64_t submissionSequence){
    if(end_of_run_)
        throw std::logic_error("PaperBroker: cannot submit after EOF");
    if(last_order_id_==std::numeric_limits<OrderId>::max())
        throw std::overflow_error("PaperBroker: order ID exhausted");

    Order order;

    order.id = last_order_id_ +1;
    order.request = request;
    order.submission_time = submttedAt;
    order.submission_sequence = submissionSequence;
    order.lifetime = OrderLifetime::UntilEndOfRun;
    order.reason = rejectionReason(request, submissionSequence);
    order.status = order.reason.empty()? OrderStatus::Accepted : OrderStatus::Rejected;

    const OrderId id = order.id;
    orders_.push_back(std::move(order));
    last_order_id_ = id;
    return id;
}

Order& OrderManager::findOrder(OrderId id)
{
    if (id == 0 || id > orders_.size()) {
        throw std::out_of_range("OrderManager: unknown order ID");
    }
    return orders_[static_cast<std::size_t>(id - 1)];
}

const Order& OrderManager::findOrder(OrderId id) const
{
    if (id == 0 || id > orders_.size()) {
        throw std::out_of_range("OrderManager: unknown order ID");
    }
    return orders_[static_cast<std::size_t>(id - 1)];
}

void OrderManager::validateReport(const ExecutionReport& report,
                                 const Order& order) const
{
    if (order.status != OrderStatus::Accepted) {
        throw std::logic_error("OrderManager: order already has a terminal outcome");
    }
    if (report.instrument != instrument_ || report.side != order.request.side
        || report.fill_time < order.submission_time
        || !validOutcome(report.status, report.quantity, report.price,
                         report.fee, report.reason, order.request.quantity)) {
        throw std::invalid_argument("OrderManager: invalid execution report");
    }
}

void OrderManager::recordExecution(const ExecutionReport& report)
{
    if (report.execution_id == 0) {
        throw std::invalid_argument("OrderManager: execution ID must be positive");
    }

    const auto previous = recorded_executions_.find(report.execution_id);
    if (previous != recorded_executions_.end()) {
        if (previous->second == report) {
            return;
        }
        throw std::logic_error("OrderManager: conflicting execution ID reuse");
    }

    Order& order = findOrder(report.order_id);
    validateReport(report, order);

    
    std::string reason = report.reason;
    recorded_executions_.emplace(report.execution_id, report);
    order.reason.swap(reason); // noexcept for std::string's default allocator
    order.status = report.status;
}

void OrderManager::expirePendingAtEnd()
{
    expirePendingAtEnd("End of data before an eligible execution was completed");
}

void OrderManager::expirePendingAtEnd(const std::string& reason)
{
    if (reason.empty()) throw std::invalid_argument("Run-end reason must be nonempty");
    if (end_of_run_) {
        return;
    }

    // Stage all messages first: allocation failure leaves all orders unchanged.
    std::vector<std::string> reasons(orders_.size());
    for (std::size_t i = 0; i < orders_.size(); ++i) {
        if (orders_[i].status == OrderStatus::Accepted) {
            reasons[i] = reason;
        }
    }
    for (std::size_t i = 0; i < orders_.size(); ++i) {
        if (orders_[i].status == OrderStatus::Accepted) {
            orders_[i].reason.swap(reasons[i]);
            orders_[i].status = OrderStatus::Expired;
        }
    }
    end_of_run_ = true;
}

const std::vector<Order>& OrderManager::orders() const noexcept
{
    return orders_;
}
OrderId OrderManager::submit(const OrderRequest& request, const EventContext& context) {
    // Store using the existing checked path; conversion below cannot throw.
    const OrderId id = submit(request, context.processing_time, context.dispatch_sequence);
    Order& order = findOrder(id);
    order.submission_sequence = 0; // no CSV/provider sequence inferred
    order.submission_dispatch_sequence = context.dispatch_sequence;
    return id;
}
const Order& OrderManager::order(OrderId id) const { return findOrder(id); }


}// namespace trading_app_cli
