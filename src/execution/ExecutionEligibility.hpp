#pragma once

#include"core/Types.hpp"
#include <stdexcept>
#include <type_traits>

namespace trading_app_cli {
inline void validateExecutionOpportunity(const ExecutionOpportunity& event) {
    if (event.instrument.id == 0 || event.instrument.symbol.empty()
        || event.instrument.currency != Currency::USD
        || event.instrument.price_scale != usd_scale
        || event.instrument.whole_share_step != 1
        || event.context.dispatch_sequence == 0
        || (event.source_sequence && *event.source_sequence == 0))
        throw std::invalid_argument("Invalid execution opportunity identity/context");
    std::visit([](const auto& prices) {
        using T = std::decay_t<decltype(prices)>;
        if constexpr (std::is_same_v<T, BarOpenPrice>) {
            if (prices.open.units <= 0) throw std::invalid_argument("Invalid open price");
        } else if constexpr (std::is_same_v<T, TradePrice>) {
            if (prices.price.units <= 0) throw std::invalid_argument("Invalid trade price");
        } else {
            if (prices.bid.units <= 0 || prices.ask.units <= 0
                || prices.bid.units > prices.ask.units)
                throw std::invalid_argument("Invalid bid/ask prices");
        }
    }, event.prices);
}
inline bool executionEligible(const Order& order, const ExecutionOpportunity& event) {
    return order.status == OrderStatus::Accepted
        && order.request.instrument == event.instrument
        && order.submission_dispatch_sequence != 0
        && order.submission_dispatch_sequence < event.context.dispatch_sequence
        && order.submission_time <= event.context.processing_time;
}
} // namespace trading_app_cli