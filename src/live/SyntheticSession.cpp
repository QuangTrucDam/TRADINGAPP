#include "SyntheticSession.hpp"
#include <algorithm>
#include <stdexcept>
namespace trading_app_cli::live {
std::vector<Input> syntheticSession(std::size_t minutes) {
    if (minutes < 4 || minutes > 20000)
        throw std::invalid_argument("Synthetic duration outside 4..20000 minutes");
    std::vector<Input> inputs;
    const EventTime base = std::chrono::sys_days{std::chrono::year{2026} / 11 / 2} + std::chrono::hours(15);
    auto add = [&](Input::Kind kind, std::int64_t ms) {
        Input i;
        i.kind = kind;
        i.sequence = inputs.size() + 1;
        i.time = base + std::chrono::milliseconds(ms);
        i.clock_ns = ms * 1000000;
        i.receipt_ns = i.clock_ns;
        inputs.push_back(i);
    };
    add(Input::Kind::Connected, 0);
    add(Input::Kind::Subscribed, 1);
    add(Input::Kind::Quote, 2);
    inputs.back().quote =
        LiveQuote{Price{999000}, Price{1001000}, base + std::chrono::milliseconds(2), 2000000, {}};
    add(Input::Kind::Start, 3);
    auto &start = inputs.back();
    start.session_id = "synthetic-paper";
    SessionConfiguration c;
    c.strategy.kind = StrategyKind::Scripted;
    c.strategy.strategy_id = "synthetic-script";
    c.realtime_confirmed = true;
    c.order_timeout = std::chrono::milliseconds(5000);
    c.execution.flat_fee = Money{1000};
    c.execution.slippage_units = 1;
    c.maximum_order_quantity = 20;
    c.strategy.decisions = {{1, Side::Buy, 10}, {2, Side::Sell, 10}, {3, Side::Buy, 5}, {4, Side::Sell, 5}};
    start.configuration = c;
    for (std::size_t n = 1; n <= minutes; ++n) {
        auto ms = static_cast<std::int64_t>(n) * 60000;
        auto close = 1000000 + static_cast<std::int64_t>(n % 10) * 1000;
        add(Input::Kind::Trade, ms + 1000);
        inputs.back().trade = Trade{inputs.back().time, Price{close}, 10 * quantity_scale, n};
        add(Input::Kind::Quote, ms + 61990);
        inputs.back().quote = LiveQuote{
            Price{close - 1000}, Price{close + 1000}, inputs.back().time, inputs.back().receipt_ns, {}};
        add(Input::Kind::Timer, ms + 62000);
        add(Input::Kind::Quote, ms + 62010);
        inputs.back().quote = LiveQuote{
            Price{close - 900}, Price{close + 1100}, inputs.back().time, inputs.back().receipt_ns, {}};
        // The next trade is later in receipt order despite its source time in the next minute.
        if (n < minutes) {
            add(Input::Kind::Trade, ms + 63000);
            inputs.back().trade = Trade{inputs.back().time, Price{close + 50}, quantity_scale, 100000 + n};
        }
    }
    // Sort by logical arrival time, then assign fresh dispatch positions.
    std::stable_sort(inputs.begin(), inputs.end(),
                     [](const Input &a, const Input &b) { return a.clock_ns < b.clock_ns; });
    add(Input::Kind::Stop, static_cast<std::int64_t>(minutes) * 60000 + 64000);
    inputs.back().reason = "synthetic stop";
    add(Input::Kind::End, static_cast<std::int64_t>(minutes) * 60000 + 65000);
    for (std::size_t n = 0; n < inputs.size(); ++n)
        inputs[n].sequence = n + 1;
    return inputs;
}
std::vector<FaultResult> runFaultScenarios() {
    std::vector<FaultResult> report;
    auto run = [&](std::string name, std::string expected, std::vector<Input> inputs, const auto &verify) {
        try {
            LiveProcessor processor("AAPL");
            for (std::size_t n = 0; n < inputs.size(); ++n) {
                inputs[n].sequence = n + 1;
                processor.apply(inputs[n]);
            }
            auto s = processor.state();
            bool financial = true;
            if (s.paper.account) {
                const auto &p = *s.paper.account;
                financial = p.cash.units >= 0 && s.paper.reserved_cash.units <= p.cash.units &&
                            s.paper.reserved_shares >= 0 &&
                            (!p.equity || p.equity->units - p.initial_cash.units ==
                                              p.realized_pnl.units + p.unrealized_pnl->units);
            }
            report.push_back({std::move(name), std::move(expected), financial && verify(s)});
        } catch (...) {
            report.push_back({std::move(name), std::move(expected), false});
        }
    };
    auto base = syntheticSession(6);
    run("baseline", "four fills; stopped; no reservations", base, [](const auto &s) {
        return s.paper.fills == 4 && s.paper.state == "Stopped" && s.paper.reserved_cash.units == 0 &&
               s.paper.pending == 0;
    });
    auto duplicate = base;
    auto t = std::find_if(duplicate.begin(), duplicate.end(),
                          [](const auto &i) { return i.kind == Input::Kind::Trade; });
    duplicate.insert(t + 1, *t);
    run("duplicate trade", "deduplicated; same four fills", duplicate,
        [](const auto &s) { return s.duplicates == 1 && s.paper.fills == 4; });
    auto disconnected = base;
    auto q = std::find_if(disconnected.begin(), disconnected.end(),
                          [](const auto &i) { return i.kind == Input::Kind::Timer; });
    Input gap = *q;
    gap.kind = Input::Kind::Gap;
    gap.reason = "injected disconnect";
    disconnected.insert(q + 1, gap);
    run("disconnect while pending", "expire pending; no recovery without reconnect", disconnected,
        [](const auto &s) { return s.paper.fills == 0 && s.paper.expired >= 1 && s.paper.pending == 0; });
    auto crossed = base;
    for (auto &i : crossed)
        if (i.quote && i.sequence > 5)
            i.quote->ask = Price{1};
    run("crossed quotes", "rejected; no fills", crossed,
        [](const auto &s) { return s.rejected > 0 && s.paper.fills == 0; });
    auto stale = base;
    for (auto &i : stale)
        if (i.quote && i.sequence > 5)
            i.quote->source_time = i.time - std::chrono::seconds(30);
    run("stale quotes", "no executions", stale, [](const auto &s) { return s.paper.fills == 0; });
    auto protected_quotes = base;
    for (auto &i : protected_quotes)
        if (i.quote && i.clock_ns % 60000000000LL == 2010000000LL) {
            i.quote->bid.units += 100000;
            i.quote->ask.units += 100000;
        }
    run("price protection", "out-of-bound quote expires order", protected_quotes,
        [](const auto &s) { return s.paper.expired > 0; });
    auto early = std::vector<Input>(base.begin(), base.begin() + 4);
    Input stop = early.back();
    stop.kind = Input::Kind::Stop;
    stop.configuration.reset();
    early.push_back(stop);
    early.push_back(stop);
    stop.kind = Input::Kind::End;
    early.push_back(stop);
    run("repeated stop before trades", "zero fills; retained initial cash", early, [](const auto &s) {
        return s.paper.fills == 0 && s.paper.state == "Stopped" &&
               s.paper.account->cash.units == 10000 * usd_scale;
    });
    auto quiet = base;
    quiet.erase(std::remove_if(quiet.begin(), quiet.end(),
                               [](const auto &i) { return i.kind == Input::Kind::Trade; }),
                quiet.end());
    run("quiet healthy market", "no fabricated bars or orders", quiet,
        [](const auto &s) { return s.paper.orders == 0 && s.paper.bars == 0; });
    auto late = base;
    auto late_it =
        std::find_if(late.begin(), late.end(), [](const auto &i) { return i.kind == Input::Kind::Timer; });
    Input late_trade = *late_it;
    late_trade.kind = Input::Kind::Trade;
    late_trade.trade =
        Trade{late_trade.time - std::chrono::seconds(50), Price{1000000}, quantity_scale, 999999};
    late.insert(late_it + 1, late_trade);
    run("late trade", "excluded from finalized minute", late, [](const auto &s) { return s.late > 0; });
    auto failure = std::vector<Input>(base.begin(), base.begin() + 4);
    Input bad = failure.back();
    bad.kind = Input::Kind::Failure;
    bad.configuration.reset();
    bad.reason = "injected overflow";
    failure.push_back(bad);
    run("unrecoverable integrity failure", "failed account retains committed cash", failure,
        [](const auto &s) { return s.failed && s.paper.state == "Failed" && s.paper.pending == 0; });
    return report;
}
} // namespace trading_app_cli::live
