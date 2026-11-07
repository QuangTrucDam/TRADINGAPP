#pragma once

#include "core/Types.hpp"

namespace trading_app_cli{
class IClock
{

public:
    virtual ~IClock()=default;
    virtual EventTime now() const =0;
};


}// namespace trading_app_cli