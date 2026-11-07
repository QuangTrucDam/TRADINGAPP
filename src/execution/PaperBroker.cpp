#include "PaperBroker.hpp"

namespace trading_app_cli{
namespace{
    bool supportedInstrument(const Instrument& instrument){
        return instrument.id != 0 && !instrument.symbol.empty()
            && instrument.currency ==Currency::USD
            && instrument.price_scale == usd_scale
            && instrument.whole_share_step ==1;
        }
    
    bool validOutcome(OrderStatus status, Quantity quantity, Price price,
        Money fee, const std::string reason, Quantity requestQuantity){
            if(status == OrderStatus::Filled){
                return quantity==requestQuantity
                    && quantity>0
                    && price.units>0
                    && fee.units>=0
                    && reason.empty();
            }

            if(status ==OrderStatus::Rejected){
                return quantity==0
                    && price.units==0
                    && fee.units ==0
                    && !reason.empty();
            }

            return false;
        }

    ExecutionOpportunity legacyOpportunity(const BarEvent& bar) {
        return {bar.instrument, {bar.start, bar.source_sequence},
                bar.source_sequence, BarOpenPrice{bar.open}};
    }

}//namespace

PaperBroker::PaperBroker(Instrument instrument, std::unique_ptr<IFillModel> model)
    : instrument_(std::move(instrument)),
      model_(std::move(model)),
      orders_(supportedInstrument(instrument_)
                  ? std::make_shared<OrderManager>(instrument_)
                  : throw std::invalid_argument("PaperBroker: unsupported instrument")) {
    if (!model_) {
        throw std::invalid_argument("PaperBroker: fill model is required");
    }
}

PaperBroker::PaperBroker(std::shared_ptr<OrderManager> orders,
                         std::unique_ptr<IFillModel> model)
    : model_(std::move(model)),
      orders_(std::move(orders)) {
    if (!orders_) {
        throw std::invalid_argument("PaperBroker: order manager required");
    }
    instrument_ = orders_->instrument();
    if (!supportedInstrument(instrument_)) {
        throw std::invalid_argument("PaperBroker: unsupported instrument");
    }
    if (!model_) {
        throw std::invalid_argument("PaperBroker: fill model is required");
    }
}

OrderId PaperBroker::submit(const OrderRequest& request, const EventContext& context) {
    return orders_->submit(request, context);
}
OrderId PaperBroker::submit(const OrderRequest& request, EventTime time,
                           std::uint64_t sequence) {
    return orders_->submit(request, time, sequence);
}

std::vector<OrderId> PaperBroker::eligibleOrders(const ExecutionOpportunity& event) const {
    validateExecutionOpportunity(event);
    if (event.instrument != instrument_)
        throw std::invalid_argument("PaperBroker: instrument mismatch");
    std::vector<OrderId> ids;
    for (const auto& order : orders_->orders())
        if (order.status == OrderStatus::Accepted && order.submission_dispatch_sequence == 0)
            throw std::logic_error("Do not mix legacy and dispatch ordering APIs");
        else if (executionEligible(order, event)) ids.push_back(order.id);
    return ids;
}
void PaperBroker::validateProposal(const FillProposal& proposal, const Order& order,
                                   const ExecutionOpportunity& event) const {
    if (proposal.order_id != order.id || proposal.instrument != instrument_
        || proposal.side != order.request.side
        || proposal.fill_time != event.context.processing_time
        || proposal.dispatch_sequence != event.context.dispatch_sequence
        || proposal.source_sequence != event.source_sequence.value_or(0)
        || !validOutcome(proposal.status, proposal.quantity, proposal.price,
                         proposal.fee, proposal.reason, order.request.quantity))
        throw std::logic_error("PaperBroker: invalid model proposal");
}
void PaperBroker::validateBar(const BarEvent& bar) const
{
    if (bar.instrument != instrument_) {
        throw std::invalid_argument("PaperBroker: bar instrument mismatch");
    }
    if (bar.source_sequence == 0 || bar.start >= bar.end) {
        throw std::invalid_argument("PaperBroker: invalid bar provenance or interval");
    }
    if (bar.open.units <= 0 || bar.high.units <= 0
        || bar.low.units <= 0 || bar.close.units <= 0
        || bar.volume < 0
        || bar.low.units > std::min(bar.open.units, bar.close.units)
        || std::max(bar.open.units, bar.close.units) > bar.high.units) {
        throw std::invalid_argument("PaperBroker: invalid OHLCV bar");
    }
}
std::vector<OrderId> PaperBroker::eligibleOrders(const BarEvent& bar) const {
    validateBar(bar);
    std::vector<OrderId> ids;
    for (const auto& order : orders_->orders()) {
        if (order.status != OrderStatus::Accepted) continue;
        if (order.submission_dispatch_sequence != 0)
            throw std::logic_error("Do not mix legacy and dispatch ordering APIs");
        if (order.submission_sequence < bar.source_sequence
            && order.submission_time <= bar.start) ids.push_back(order.id);
    }
    return ids;
}
std::optional<FillProposal> PaperBroker::proposeFill(
    OrderId id, const ExecutionOpportunity& event) const {
    const auto& order = orders_->order(id);
    validateExecutionOpportunity(event);
    if (event.instrument != instrument_)
        throw std::invalid_argument("PaperBroker: instrument mismatch");
    if (order.status == OrderStatus::Accepted && order.submission_dispatch_sequence == 0)
        throw std::logic_error("Do not mix legacy and dispatch ordering APIs");
    if (!executionEligible(order, event)) return std::nullopt;
    auto result = model_->evaluate(order, event);
    if (result) validateProposal(*result, order, event);
    return result;
}
std::optional<FillProposal> PaperBroker::proposeFill(OrderId id, const BarEvent& bar) const {
    const auto& order = orders_->order(id);
    validateBar(bar);
    if (order.status != OrderStatus::Accepted) return std::nullopt;
    if (order.submission_dispatch_sequence != 0)
        throw std::logic_error("Do not mix legacy and dispatch ordering APIs");
    Order adapted = order;
    adapted.submission_dispatch_sequence = order.submission_sequence;
    const auto bar_ = legacyOpportunity(bar);
    if (!executionEligible(adapted, bar_)) return std::nullopt;
    auto result = model_->evaluate(adapted, bar_);
    if (result) {
        validateProposal(*result, adapted, bar_);
        result->dispatch_sequence = 0; 
    }
    return result;
}
void PaperBroker::recordExecution(const ExecutionReport& report) {
    orders_->recordExecution(report);
}
void PaperBroker::expirePendingAtEnd() { orders_->expirePendingAtEnd(); }
void PaperBroker::finishRun(const std::string& reason) {
    orders_->expirePendingAtEnd(reason);
}
const std::vector<Order>& PaperBroker::orders() const noexcept { 
    return orders_->orders(); 
}

}//namespace trading_app_cli

