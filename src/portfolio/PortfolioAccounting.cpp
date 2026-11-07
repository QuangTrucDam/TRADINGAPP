#include "portfolio/PortfolioAccounting.hpp"
#include "core/FixedPoint.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace trading_app_cli {
namespace {
    FillApplyResult reject(FillRejectReason reason, std::string detail) {
        return {FillApplyStatus::Rejected, reason, std::move(detail)};
    }

    struct Valuation {
        std::optional<Money> unrealized;
        std::optional<Money> equity;
    };

    Valuation valuePortfolio(Money cash,
        const std::map<InstrumentId, Position>& positions,
        const std::map<InstrumentId, Price>& marks){
            Money market_value{};
            Money unrealized{};

            bool missing_mark = false;
            for(const auto& [id, position]:positions){
                if(position.quantity ==0) continue;
                const auto mark = marks.find(id);
                if(mark == marks.end()){
                    missing_mark= true;
                    continue;
                }

                const Money position_value = checkedNotional(position.quantity, mark->second);
                const Money position_remain = checkedSubtract(position_value, position.remaining_acquisition_cost);

                market_value = checkedAdd(market_value, position_value);
                unrealized = checkedAdd(unrealized,position_remain);

            }

            if(missing_mark) return {std::nullopt, std::nullopt};
            return {unrealized, checkedAdd(cash, market_value)};
        }

    void checkReconciliation(const Valuation& valuation, Money initial, Money realized){
        if(!valuation.equity) return;
        const Money change = checkedSubtract(*valuation.equity, initial);
        const Money pnl = checkedAdd(realized,*valuation.unrealized);
        if(change!=pnl) throw std::logic_error("Portfolio equity/PnL reconciliation failed");
    }
}// namespace

PortfolioAccounting::PortfolioAccounting(Money initialcash): initial_cash_(initialcash),cash_(initialcash){
    if (initialcash.units<0)
    {
        throw std::invalid_argument("Initial cash cannot be negative");
    }
}

FillApplyResult PortfolioAccounting::applyFill(const ExecutionReport& fill){
    const auto previous = applied_executions_.find(fill.execution_id);
    if(previous!= applied_executions_.end()){
        if(previous->second==fill){
            return {FillApplyStatus::AlreadyApplied, FillRejectReason::None, {}};
        }
        throw std::logic_error("Conflicting report for an applied execution ID");
     }

    if(fill.status!=OrderStatus::Filled||fill.order_id==0
        ||fill.execution_id==0
        ||(fill.side!=Side::Buy &&fill.side!=Side::Sell)){
            return reject(FillRejectReason::InvalidExecution, "Not a valid fill exercution");    
        }
    if(!validInstrument(fill.instrument))
        return reject(FillRejectReason::InvalidInstrument,  "Unsupported instrument");
    if (fill.quantity <= 0)
        return reject(FillRejectReason::InvalidQuantity, "Fill quantity must be positive");
    if (fill.price.units <= 0)
        return reject(FillRejectReason::InvalidPrice, "Fill price must be positive");
    if (fill.fee.units < 0)
        return reject(FillRejectReason::InvalidFee, "Fill fee cannot be negative");
    

    const auto existing = positions_.find(fill.instrument.id);
    if (existing != positions_.end() && existing->second.instrument != fill.instrument)
        return reject(FillRejectReason::InvalidInstrument, "Instrument ID metadata conflict");

    Position next_position = existing==positions_.end()?
        Position(fill.instrument,0,Money{},std::nullopt): existing->second;

    Money next_cash = cash_;
    Money next_realized = realized_pnl_;

    if(fill.side==Side::Buy){
        const Money notional = checkedNotional(fill.quantity, fill.price);
        const Money debit = checkedAdd(notional,fill.fee);

        if(debit.units>cash_.units)
            return reject(FillRejectReason::InsufficientCash, " In sufficient cash including buy fee");

        const auto max_quantity = std::numeric_limits<Quantity>::max();

        if(fill.quantity> max_quantity -next_position.quantity)
            throw std::overflow_error(" Position quantity overflow");

        next_position.quantity+=fill.quantity;
        next_position.remaining_acquisition_cost =checkedAdd(next_position.remaining_acquisition_cost,debit);

        next_cash= checkedSubtract(cash_,debit);
    }
    else {
        if(fill.quantity>next_position.quantity)
            return reject(FillRejectReason::InsufficientHoldings, " Sale exceeds held shares");

        const Money notional = checkedNotional(fill.quantity, fill.price);
        const Money credit = checkedSubtract(notional, fill.fee);
        next_cash = checkedAdd(cash_, credit);
        if (next_cash.units < 0)
            return reject(FillRejectReason::InsufficientCash, "Cash cannot cover the sell fee");

        const Money allocated = allocateAcquisitionCost(next_position.remaining_acquisition_cost, fill.quantity,next_position.quantity);

        const Money realized_increment = checkedSubtract(credit, allocated);
        next_realized = checkedAdd(realized_pnl_, realized_increment);
        next_position.quantity-=fill.quantity;
        next_position.remaining_acquisition_cost =checkedSubtract(next_position.remaining_acquisition_cost,allocated);

    }

    const Money next_fees = checkedAdd(total_fees_,fill.fee);
    if(next_position.quantity<0||next_position.remaining_acquisition_cost.units<0||
    (next_position.quantity==0&&next_position.remaining_acquisition_cost.units!=0))
        throw std::logic_error(" Invalid candidate position state");

    auto next_positions = positions_;
    auto next_executions = applied_executions_;

    next_positions.insert_or_assign(fill.instrument.id, std::move(next_position));
    next_executions.emplace(fill.execution_id, fill);
     
    const auto valuation = valuePortfolio(next_cash,next_positions, marks_);
    checkReconciliation(valuation, initial_cash_, next_realized);

    positions_.swap(next_positions);
    applied_executions_.swap(next_executions);
    cash_ = next_cash;
    realized_pnl_ = next_realized;
    total_fees_ = next_fees;
    return {FillApplyStatus::Applied, FillRejectReason::None, {}};

 }

 void PortfolioAccounting::updateMark(InstrumentId instrumentid, Price mark) {
    if (instrumentid == 0 || mark.units <= 0)
        throw std::invalid_argument("Mark requires a valid instrument ID and positive price");

    // Marks may arrive while flat, before the first buy.
    auto next_marks = marks_;
    next_marks.insert_or_assign(instrumentid, mark);
    const auto valuation = valuePortfolio(cash_, positions_, next_marks);
    checkReconciliation(valuation, initial_cash_, realized_pnl_);
    marks_.swap(next_marks);
}

Money PortfolioAccounting::cash() const { return cash_; }

Quantity PortfolioAccounting::quantity(InstrumentId instrumentid) const {
    const auto position = positions_.find(instrumentid);
    return position == positions_.end() ? 0 : position->second.quantity;
}

Money PortfolioAccounting::realizedPnl() const { return realized_pnl_; }

Money PortfolioAccounting::unrealizedPnl() const {
    const auto valuation = valuePortfolio(cash_, positions_, marks_);
    if (!valuation.unrealized)
        throw std::runtime_error("Unrealized PnL unavailable: held position has no mark");
    return *valuation.unrealized;
}

std::optional<Money> PortfolioAccounting::equity() const {
    return valuePortfolio(cash_, positions_, marks_).equity;
}

PortfolioSnapshot PortfolioAccounting::snapshot() const{
    const auto valuation = valuePortfolio(cash_, positions_,marks_);
    PortfolioSnapshot results;
    results.initial_cash = initial_cash_;
    results.cash=cash_;
    results.realized_pnl=realized_pnl_;
    results.total_fees=total_fees_;
    results.equity = valuation.equity;
    results.unrealized_pnl=valuation.unrealized;
    for(const auto& [id,position]:positions_){
        Position pos = position;
        const auto mark =marks_.find(id);
        pos.mark=mark==marks_.end()?std::nullopt:std::optional<Price>{mark->second};
        results.positions.push_back(std::move(pos));
    }
    return results;
}   


}// namespace trading_app_cli