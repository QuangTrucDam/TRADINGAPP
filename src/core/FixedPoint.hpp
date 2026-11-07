#pragma once

#include <string>
#include <string_view>
#include "Types.hpp"

namespace trading_app_cli {
    Price parsePrice(std::string_view text);
    Money parseMoney(std::string_view text);
    std::string formatPrice(Price value);
    std::string formatMoney(Money value);
    Money checkedAdd(Money lhs, Money rhs);
    Money checkedSubtract(Money lhs, Money rhs);
    Money checkedNotional(Quantity quantity, Price price);
    Money allocateAcquisitionCost(Money cost, Quantity sold, Quantity held);

} // namespace trading_app_cli