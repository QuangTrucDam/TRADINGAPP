#pragma once 

#include "ITradingModel.hpp"

namespace trading_app_cli{
    class DoNothingStrategy final : public ITradingModel
    {
    public:
        std::vector<OrderRequest> onBar(const BarEvent& completedBar,
        const PortfolioSnapshot& portfolio, const PendingOrderView& pending) override;
    virtual void reset() override;
    };
    
    
}//namespace trading_app_cli