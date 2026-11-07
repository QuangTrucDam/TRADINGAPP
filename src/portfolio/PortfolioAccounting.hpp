#pragma once

#include "core/Types.hpp"
#include "core/Validation.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace trading_app_cli {


struct PortfolioSnapshot {
    Money initial_cash{};
    Money cash{};
    Money realized_pnl{};
    Money total_fees{};
    std::optional<Money> unrealized_pnl;
    std::optional<Money> equity;
    std::vector<Position> positions; 
};

enum class FillApplyStatus { Applied, AlreadyApplied, Rejected };

enum class FillRejectReason {
    None,
    InvalidExecution,
    InvalidInstrument,
    InvalidQuantity,
    InvalidPrice,
    InvalidFee,
    InsufficientCash,
    InsufficientHoldings
};

struct FillApplyResult {
    FillApplyStatus status{FillApplyStatus::Rejected};
    FillRejectReason reason{FillRejectReason::None};
    std::string detail;
};

class PortfolioAccounting {
public:
    explicit PortfolioAccounting(Money initialCash);
    FillApplyResult applyFill(const ExecutionReport& fill);
    void updateMark(InstrumentId instrumentid, Price mark);
    Money cash() const;
    Quantity quantity(InstrumentId instrumentid) const;
    Money realizedPnl() const;
    Money unrealizedPnl() const;
    std::optional<Money> equity() const;
    PortfolioSnapshot snapshot() const;

private:
    Money initial_cash_{};
    Money cash_{};
    Money realized_pnl_{};
    Money total_fees_{};

    std::map<InstrumentId, Position> positions_;
    std::map<InstrumentId, Price> marks_;
    std::map<ExecutionId, ExecutionReport> applied_executions_;
};

} // namespace trading_app_cli