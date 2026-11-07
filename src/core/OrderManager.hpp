#pragma once
#include "Types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace trading_app_cli {
class OrderManager
{
private:
    Instrument instrument_;
    std::vector<Order> orders_;
    std::unordered_map<ExecutionId, ExecutionReport> recorded_executions_;
    OrderId last_order_id_{};
    bool end_of_run_{};
    Order& findOrder(OrderId id);
    const Order& findOrder(OrderId id) const;
    void validateReport(const ExecutionReport& report,
                            const Order& order) const;
    std::string rejectionReason(const OrderRequest request, std::uint64_t sequence) const;
        

public:
    explicit OrderManager(Instrument instrument);

    OrderManager( const OrderManager&) =delete;
    OrderManager& operator =(const OrderManager&) =delete;
    OrderManager( const OrderManager&&) =delete;
    OrderManager& operator =(const OrderManager&&) =delete;

    OrderId submit(const OrderRequest&, EventTime, std::uint64_t sourceSequence);
    OrderId submit(const OrderRequest&, const EventContext&);

    void recordExecution(const ExecutionReport&);
    void expirePendingAtEnd();
    void expirePendingAtEnd(const std::string& reason);
    const std::vector<Order>& orders() const noexcept;
    const Order& order(OrderId id) const;
    const Instrument& instrument() const noexcept { return instrument_; }
    
};
}// namespace trading_app_cli
