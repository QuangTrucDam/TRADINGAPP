#pragma once

#include "core/Types.hpp"

namespace trading_app_cli {

// Bar-only synchronous feed. The engine controls simulated event publication.
class IMarketDataFeed {
public:
    virtual ~IMarketDataFeed() = default;
    virtual MarketDataReadResult nextEvent() = 0;
    virtual BarReadResult nextBar() {
        throw std::logic_error("This feed does not support nextBar; use nextEvent");
    }
};

} // namespace trading_app_cli