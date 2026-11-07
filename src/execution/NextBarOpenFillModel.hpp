#pragma once
#include "core/Types.hpp"
#include "IFillModel.hpp"
#include "core/Validation.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace trading_app_cli{


class NextBarOpenFillModel final : public IFillModel {
    public:
        explicit NextBarOpenFillModel(ExecutionConfiguration configuration);
        std::optional<FillProposal> evaluate(const Order& order, const ExecutionOpportunity& opportunity) const override;

        std::optional<FillProposal> evaluate(const Order& order, const BarEvent& bar) const;

    private:
        ExecutionConfiguration configuration_;
        void validateInputs(const Order& order, const BarEvent& bar) const;
        bool isEligible(const Order& order, const BarEvent& bar) const;
        FillProposal makeProposal(const Order& order, const BarEvent& bar) const;
};

}// namespace trading_app_cli