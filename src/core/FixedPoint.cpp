#include "FixedPoint.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace trading_app_cli {
namespace {
        constexpr auto max_units = std::numeric_limits<std::int64_t>::max();
        constexpr auto min_units = std::numeric_limits<std::int64_t>::min();
        constexpr auto scale = static_cast<std::uint64_t>(usd_scale);

        bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

        std::int64_t parseUnits(std::string_view text){
            if(text.empty()) 
                throw std::invalid_argument("Empty decimal value");

            bool negative = false;
            std::size_t index{};
            if(text.front()=='+'||text.front()=='-'){
                negative = text.front()=='-';
                ++index;
            }

            if(index==text.size()||!isDigit(text[index])) 
                throw std::invalid_argument("Decimal requires an integer part");

            const auto limit = static_cast<std::uint64_t>(max_units)+ (negative ? 1U : 0U);
            std::uint64_t magnitude = 0;

            auto appendDigit = [&](unsigned digit) {
                if (magnitude > (limit - digit) / 10U)
                    throw std::overflow_error("Decimal exceeds int64 fixed-point range");
                magnitude = magnitude * 10U + digit;
            };

            while (index < text.size() && isDigit(text[index])) {
                appendDigit(static_cast<unsigned>(text[index++] - '0'));
            }

            unsigned fractional_digits = 0;
            
            if (index < text.size() && text[index] == '.') {
                ++index;
                while (index < text.size() && isDigit(text[index])) {
                    if (fractional_digits == 4)
                        throw std::invalid_argument("At most four fractional digits allowed");
                    appendDigit(static_cast<unsigned>(text[index] - '0'));
                    ++fractional_digits;
                    ++index;
                }
                if (fractional_digits == 0)
                    throw std::invalid_argument("Decimal point requires fractional digits");
            }

            if (index != text.size())
                throw std::invalid_argument("Invalid character in decimal value");

            for (; fractional_digits < 4; ++fractional_digits) appendDigit(0);

            if (!negative) return static_cast<std::int64_t>(magnitude);
            if (magnitude == static_cast<std::uint64_t>(max_units) + 1U) return min_units;
            return -static_cast<std::int64_t>(magnitude);

        }
        std::string formatUnits(std::int64_t units) {
            const bool negative = units < 0;
            const auto magnitude = negative
                ? static_cast<std::uint64_t>(-(units + 1)) + 1U
                : static_cast<std::uint64_t>(units);
            const auto fraction = std::to_string(magnitude % scale);
            return (negative ? std::string("-") : std::string{})
                + std::to_string(magnitude / scale) + "."
                + std::string(4 - fraction.size(), '0') + fraction;
        }

        struct DivisionResult { 
            std::uint64_t quotient; 
            std::uint64_t remainder; 
        };
        
        DivisionResult multiplyDivide(std::uint64_t a, std::uint64_t b,
                                std::uint64_t divisor) noexcept {
            std::uint64_t quotient = 0;
            std::uint64_t remainder = 0;
            for (int bit = 63; bit >= 0; --bit) {
                quotient *= 2U;
                remainder *= 2U;
                if (remainder >= divisor) {
                    remainder -= divisor;
                    ++quotient;
                }
                if (((a >> bit) & 1U) != 0) {
                    remainder += b;
                    if (remainder >= divisor) {
                        remainder -= divisor;
                        ++quotient;
                    }
                }
            }
            return {quotient, remainder};
        }

    }// name space

    Price parsePrice(std::string_view text) {
        const auto units = parseUnits(text);
        if (units <= 0) throw std::invalid_argument("Price must be positive");
        return Price{units};
    }


    Money parseMoney(std::string_view text) { return Money{parseUnits(text)}; }
    std::string formatPrice(Price value) { return formatUnits(value.units); }
    std::string formatMoney(Money value) { return formatUnits(value.units); }

    Money checkedAdd(Money lhs, Money rhs) {
        if ((rhs.units > 0 && lhs.units > max_units - rhs.units) ||
            (rhs.units < 0 && lhs.units < min_units - rhs.units))
            throw std::overflow_error("Money addition overflow");
        return Money{lhs.units + rhs.units};
    }

    Money checkedSubtract(Money lhs, Money rhs) {
        if ((rhs.units > 0 && lhs.units < min_units + rhs.units) ||
            (rhs.units < 0 && lhs.units > max_units + rhs.units))
            throw std::overflow_error("Money subtraction overflow");
        return Money{lhs.units - rhs.units};
    }

    Money checkedNotional(Quantity quantity, Price price) {
        if (quantity <= 0 || price.units <= 0)
            throw std::invalid_argument("Notional requires positive quantity and price");
        if (quantity > max_units / price.units)
            throw std::overflow_error("Quantity times price overflow");
        return Money{quantity * price.units};
    }

    Money allocateAcquisitionCost(Money cost, Quantity sold, Quantity held) {
        if (cost.units < 0 || held <= 0 || sold <= 0 || sold > held)
            throw std::invalid_argument("Invalid acquisition-cost allocation inputs");
        if (sold == held) return cost;

        const auto divisor = static_cast<std::uint64_t>(held);
        auto result = multiplyDivide(static_cast<std::uint64_t>(cost.units),
                                    static_cast<std::uint64_t>(sold), divisor);
        const auto half_up_threshold = divisor / 2U + divisor % 2U;
        if (result.remainder >= half_up_threshold) ++result.quotient;
        return Money{static_cast<std::int64_t>(result.quotient)};
    }

}// namespace trading_app_cli

