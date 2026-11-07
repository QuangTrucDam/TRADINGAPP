#pragma once
#include "core/Types.hpp"
#include <optional>

namespace trading_app_cli{

class IFillModel
{
    
public:
    virtual ~IFillModel()=default;

    virtual std::optional<FillProposal> evaluate( const Order& order, const ExecutionOpportunity& opportunity) const= 0;
};

} // namspace trading_app_cli

