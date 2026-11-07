#pragma once
#include "core/Types.hpp"
#include "core/OrderManager.hpp"
#include "IFillModel.hpp"
#include "ExecutionEligibility.hpp"

#include <string>
#include <cstdint>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <utility>


namespace trading_app_cli{
    class PaperBroker final
    {
    private:
        Instrument instrument_;
        std::unique_ptr<IFillModel> model_;
        std::shared_ptr<OrderManager> orders_;
        void validateBar(const BarEvent& bar) const;
        void validateProposal(const FillProposal& proposal, const Order& order,
                          const ExecutionOpportunity& event) const;



    public:
        PaperBroker(Instrument instrument, std::unique_ptr<IFillModel> model);

        PaperBroker(std::shared_ptr<OrderManager> orders, std::unique_ptr<IFillModel> model);

        PaperBroker(const PaperBroker&) = delete;
        PaperBroker& operator =(const PaperBroker&) =delete;

        PaperBroker(const PaperBroker&&) = delete;
        PaperBroker& operator =(const PaperBroker&&) =delete;

        OrderId submit(const OrderRequest&, const EventContext&);
        OrderId submit(const OrderRequest&, EventTime, std::uint64_t sourceSequence);

        std::vector<OrderId> eligibleOrders(const ExecutionOpportunity& event) const;
        std::optional<FillProposal> proposeFill(OrderId id, const ExecutionOpportunity& event) const;

        void recordExecution(const ExecutionReport& report);
        void expirePendingAtEnd();
        void finishRun(const std::string& reason);

        const std::vector<Order>& orders() const noexcept;
        
        std::vector<OrderId> eligibleOrders(const BarEvent&) const;
        std::optional<FillProposal> proposeFill(OrderId, const BarEvent&) const;
    };
    
    
    

} // namspace trading_app_cli