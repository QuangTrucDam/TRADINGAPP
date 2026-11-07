#pragma once
#include "core/Types.hpp"
#include "portfolio/PortfolioAccounting.hpp"
#include <vector>
namespace trading_app_cli {
class ITradingModel {
public:
    virtual ~ITradingModel() = default;
    virtual std::vector<OrderRequest> onBar(const BarEvent& completedBar,
        const PortfolioSnapshot& portfolio, const PendingOrderView& pending) = 0;
    virtual void reset() = 0;
};
} // namespace trading_app_cli
