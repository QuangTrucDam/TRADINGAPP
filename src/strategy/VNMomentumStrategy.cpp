#include "strategy/VNMomentumStrategy.hpp"
#include "core/Validation.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <algorithm>

namespace trading_app_cli {
namespace {
double computeScore(std::deque<Price>& closes, std::size_t lookback, double volFloor){
    if(closes.size() != lookback+1)
        throw std::logic_error("computeScore: incorrect window size");

    const double p_now = closes.back().units;
    const double p_pas = closes.front().units;

    const double R = (p_now/p_pas)-1.0;

    double sum{}, sum_sq{};
    
    for(std::size_t i=0;i<lookback;++i){
        const double r=(closes[i+1].units/closes[i].units)-1.0;
        sum+=r;
        sum_sq+=r*r;
    }

    const double mean = sum / static_cast<double>(lookback);
    const double var = (sum_sq - static_cast<double>(lookback) * mean * mean)
                       / static_cast<double>(lookback - 1);

    double sigma = std::sqrt(std::max(0.0, var));
    sigma = std::max(sigma, volFloor);
    return R / sigma;
}
} //namespace

VNMomentumStrategy::VNMomentumStrategy(Instrument instrument, StrategyId id,
    std::size_t lookback, Quantity buyQuantity, double threshold, double volFloor):
    instrument_(std::move(instrument)),
    id_(std::move(id)),
    lookback_(lookback),
    buy_quantity_(buyQuantity),
    threshold_(threshold),
    vol_floor_(volFloor)
{
    validateInstrument(instrument_);
    if (id_.empty()
    || lookback_ < 2
    || buy_quantity_ <= 0
    || lookback_ == std::numeric_limits<std::size_t>::max()
    || lookback_ + 1 > closes_.max_size()
    || !std::isfinite(threshold_) || threshold_ < 0.0
    || !std::isfinite(vol_floor_) || vol_floor_ <= 0.0)
        throw std::invalid_argument("Invalid risk-adjusted momentum configuration");
}

OrderRequest VNMomentumStrategy::makeRequest(Side side, Quantity quantity) const{
    return {instrument_, id_, side, quantity, OrderType::Market};
}

std::vector<OrderRequest> VNMomentumStrategy::onBar(const BarEvent& bar, const PortfolioSnapshot& portfolio,
        const PendingOrderView& pending)
{
    validateHistoricalBar(bar, instrument_);
    closes_.push_back(bar.close);

    if (closes_.size() > lookback_ + 1) 
        closes_.pop_front();
    if (closes_.size() < lookback_ + 1 || pending.hasPending(instrument_.id)) 
        return {};
    Quantity held = 0;
    for (const auto& position : portfolio.positions) {
        if (position.instrument.id == instrument_.id) {
            if (position.instrument != instrument_ || position.quantity < 0)
                throw std::logic_error("Invalid strategy portfolio observation");
            held = position.quantity;
            break;
        }
    }
    const double S = computeScore(closes_, lookback_, vol_floor_);

    if (S > threshold_ && held == 0)
        return {makeRequest(Side::Buy, buy_quantity_)};

    if (S < -threshold_ && held > 0)
        return {makeRequest(Side::Sell, held)};

    return {};
}
void VNMomentumStrategy::reset()
{
    closes_.clear();
}

} // namespace trading_app_cli