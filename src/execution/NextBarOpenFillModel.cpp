#include "NextBarOpenFillModel.hpp"
#include "ExecutionEligibility.hpp"


namespace trading_app_cli{

NextBarOpenFillModel::NextBarOpenFillModel(ExecutionConfiguration configuration)
    : configuration_(configuration){
        if(configuration_.flat_fee.units<0)
            throw std::invalid_argument("NextBarOpenFillModel: fee must be nonnegative");

        if(configuration_.slippage_units<0)
            throw std::invalid_argument("NextBarOpenFillModel: slippage must be nonnegative");
}

void NextBarOpenFillModel::validateInputs(const Order& order, const BarEvent& bar) const{
    if(order.id==0 || order.submission_sequence==0
    || order.request.quantity<=0
    || order.request.strategy_id.empty()
    || order.request.type!=OrderType::Market
    || (order.request.side!= Side::Buy &&order.request.side != Side::Sell)
    || order.lifetime != OrderLifetime::UntilEndOfRun
    || !order.reason.empty()
    || !supportedInstrument(order.request.instrument))
        throw std::invalid_argument("NextBarOpenFillModel: invalid accepted order");
    
    if (!supportedInstrument(bar.instrument) || bar.source_sequence == 0
    || bar.start >= bar.end
    || bar.open.units <= 0 || bar.high.units <= 0
    || bar.low.units <= 0 || bar.close.units <= 0
    || bar.volume < 0
    || bar.low.units > std::min(bar.open.units, bar.close.units)
    || std::max(bar.open.units, bar.close.units) > bar.high.units) 
        throw std::invalid_argument("NextBarOpenFillModel: invalid normalized bar");
    
}

bool NextBarOpenFillModel::isEligible(const Order& order, const BarEvent& bar) const{
    return order.request.instrument==bar.instrument
        && order.submission_sequence<bar.source_sequence
        && order.submission_time<=bar.start;
}

FillProposal NextBarOpenFillModel::makeProposal(const Order& order, const BarEvent& bar)const {
    FillProposal proposal;
    proposal.order_id = order.id;
    proposal.instrument = bar.instrument;
    proposal.fill_time =bar.start;
    proposal.source_sequence =bar.source_sequence;
    proposal.side =order.request.side;

    const std::int64_t open = bar.open.units;
    const std::int64_t slippage = configuration_.slippage_units;

    if(order.request.side == Side::Buy){
        const auto maximun =std::numeric_limits<std::int64_t>::max();
        if(open>maximun -slippage) 
           throw std::overflow_error("NextBarOpenFillModel: buy price overflow");
        proposal.price.units = open+slippage; 
    }
    else{
        if(slippage>= open){
            proposal.status = OrderStatus::Rejected;
            proposal.reason = "Sell price is nonpositive after slippage";
            return proposal;
        }
        proposal.price.units = open-slippage;
    }
    
    proposal.status = OrderStatus::Filled;
    proposal.quantity = order.request.quantity;
    proposal.fee = configuration_.flat_fee;

    return proposal;

}

std::optional<FillProposal> NextBarOpenFillModel::evaluate(const Order& order, const BarEvent& bar) const {
    switch ((order.status))
    {
        case OrderStatus::Submitted:
        case OrderStatus::Filled:
        case OrderStatus::Rejected:
        case OrderStatus::Expired:
            return std::nullopt;
        case OrderStatus::Accepted:
            break;
        default:
            throw std::invalid_argument("NextBarOpenFillModel: invalid order status");
    }
    validateInputs(order,bar);
    if(!isEligible(order,bar))
        return std::nullopt;
    return makeProposal(order,bar);
}

std::optional<FillProposal> NextBarOpenFillModel::evaluate (const Order& order, const ExecutionOpportunity& opportunity) const{
    validateExecutionOpportunity(opportunity);

    const auto* prices = std::get_if<BarOpenPrice>(&opportunity.prices);

    if(!prices||!executionEligible(order,opportunity))
        return std::nullopt;

    if (order.id == 0 || order.request.quantity <= 0
        || order.request.strategy_id.empty() || !order.reason.empty()
        || order.request.type != OrderType::Market
        || (order.request.side != Side::Buy && order.request.side != Side::Sell)
        || order.lifetime != OrderLifetime::UntilEndOfRun)
        throw std::invalid_argument("Invalid accepted order");
        
    BarEvent pricingInput;
    pricingInput.instrument = opportunity.instrument;
    pricingInput.start = opportunity.context.processing_time;
    pricingInput.open = prices->open;
    pricingInput.source_sequence = opportunity.source_sequence.value_or(0);
    auto proposal = makeProposal(order, pricingInput);
    proposal.dispatch_sequence = opportunity.context.dispatch_sequence;
    return proposal;
}


}// namespace trading_app_cli