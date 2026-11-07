#include "strategy/ScriptedStrategy.hpp"
#include "core/Validation.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace trading_app_cli {
ScriptedStrategy::ScriptedStrategy(Instrument instrument, StrategyId id,
    std::vector<ScriptedDecision> decisions)
    : instrument_(std::move(instrument)), id_(std::move(id)), decisions_(std::move(decisions)) {
    validateInstrument(instrument_);
    if (id_.empty()) throw std::invalid_argument("Strategy ID must not be empty");
    for (const auto& decision : decisions_)
        if (decision.completed_bar_index == 0 || decision.quantity <= 0
            || (decision.side != Side::Buy && decision.side != Side::Sell))
            throw std::invalid_argument("Invalid scripted decision");
    std::stable_sort(decisions_.begin(), decisions_.end(), [](const auto& a, const auto& b) {
        return a.completed_bar_index < b.completed_bar_index;
    });
}
OrderRequest ScriptedStrategy::makeRequest(const ScriptedDecision& decision) const {
    return {instrument_, id_, decision.side, decision.quantity, OrderType::Market};
}
std::vector<OrderRequest> ScriptedStrategy::onBar(const BarEvent& bar,
    const PortfolioSnapshot&, const PendingOrderView&) {
    validateHistoricalBar(bar, instrument_);
    const auto nextBar = checkedNext(completed_bars_);
    auto nextCursor = cursor_;
    std::vector<OrderRequest> requests;
    while (nextCursor < decisions_.size()
        && decisions_[nextCursor].completed_bar_index == nextBar) {
        requests.push_back(makeRequest(decisions_[nextCursor]));
        ++nextCursor;
    }
    completed_bars_ = nextBar;
    cursor_ = nextCursor;
    return requests;
}
void ScriptedStrategy::reset() { completed_bars_ = 0; cursor_ = 0; }
} // namespace trading_app_cli
