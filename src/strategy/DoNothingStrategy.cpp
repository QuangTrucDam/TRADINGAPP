#include "strategy/DoNothingStrategy.hpp"
namespace trading_app_cli {
std::vector<OrderRequest> DoNothingStrategy::onBar(const BarEvent&,
    const PortfolioSnapshot&, const PendingOrderView&) { return {}; }
void DoNothingStrategy::reset() {}
} // namespace trading_app_cli