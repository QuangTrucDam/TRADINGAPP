#include "SessionArchive.hpp"
#include "core/FixedPoint.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
namespace trading_app_cli::live {
namespace {
QJsonValue number(std::int64_t n) { return QString::number(n); }
std::int64_t integer(const QJsonObject &o, const char *key) {
    if (!o.value(key).isString())
        throw std::invalid_argument(std::string("Missing exact integer: ") + key);
    bool ok = false;
    auto n = o.value(key).toString().toLongLong(&ok);
    if (!ok)
        throw std::invalid_argument("Invalid integer in archive");
    return n;
}
std::uint64_t natural(const QJsonObject &o, const char *key) {
    auto n = integer(o, key);
    if (n < 0)
        throw std::invalid_argument("Negative unsigned archive field");
    return static_cast<std::uint64_t>(n);
}
QString text(const std::string &s) { return QString::fromStdString(s); }
QByteArray canonical(QJsonObject o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
std::string string(const QJsonObject &o, const char *key) {
    if (!o.value(key).isString())
        throw std::invalid_argument("Missing archive text field");
    return o.value(key).toString().toStdString();
}
EventTime time(const QJsonObject &o, const char *key) {
    return EventTime{std::chrono::nanoseconds(integer(o, key))};
}
QJsonObject instrumentJson(const Instrument &i) {
    return {{"id", number(i.id)},
            {"symbol", text(i.symbol)},
            {"currency", "USD"},
            {"price_scale", number(i.price_scale)},
            {"step", number(i.whole_share_step)}};
}
Instrument instrumentFromJson(const QJsonObject &o) {
    if (o.value("currency").toString() != "USD")
        throw std::invalid_argument("Unsupported account currency");
    return {natural(o, "id"), string(o, "symbol"), Currency::USD, integer(o, "price_scale"),
            integer(o, "step")};
}
QJsonObject tradeJson(const Trade &t) {
    QJsonObject o{{"time", number(t.time.time_since_epoch().count())},
                  {"price", number(t.price.units)},
                  {"quantity_atoms", number(t.quantity_atoms)}};
    if (t.id)
        o["id"] = number(*t.id);
    return o;
}
QJsonObject quoteJson(const LiveQuote &q) {
    QJsonObject o{
        {"bid", number(q.bid.units)}, {"ask", number(q.ask.units)}, {"receipt_ns", number(q.receipt_ns)}};
    if (q.source_time)
        o["time"] = number(q.source_time->time_since_epoch().count());
    if (q.update_id)
        o["update_id"] = number(*q.update_id);
    return o;
}
QJsonObject barJson(const Bar &b) {
    return {{"start", number(b.start.time_since_epoch().count())},
            {"end", number(b.end.time_since_epoch().count())},
            {"open", number(b.open.units)},
            {"high", number(b.high.units)},
            {"low", number(b.low.units)},
            {"close", number(b.close.units)},
            {"volume_atoms", number(b.volume_atoms)},
            {"sequence", number(b.sequence)}};
}
QJsonObject portfolioJson(const PortfolioSnapshot &p) {
    QJsonArray positions;
    for (const auto &pos : p.positions) {
        QJsonObject q{{"instrument", instrumentJson(pos.instrument)},
                      {"quantity", number(pos.quantity)},
                      {"cost", number(pos.remaining_acquisition_cost.units)}};
        if (pos.mark)
            q["mark"] = number(pos.mark->units);
        positions.append(q);
    }
    QJsonObject out{{"initial_cash", number(p.initial_cash.units)},
                    {"cash", number(p.cash.units)},
                    {"realized", number(p.realized_pnl.units)},
                    {"fees", number(p.total_fees.units)},
                    {"positions", positions}};
    if (p.equity)
        out["equity"] = number(p.equity->units);
    if (p.unrealized_pnl)
        out["unrealized"] = number(p.unrealized_pnl->units);
    return out;
}
QJsonObject orderJson(const PaperOrder &p) {
    const auto &o = p.order;
    return {{"id", number(o.id)},
            {"instrument", instrumentJson(o.request.instrument)},
            {"strategy", text(o.request.strategy_id)},
            {"side", o.request.side == Side::Buy ? "buy" : "sell"},
            {"quantity", number(o.request.quantity)},
            {"status", static_cast<int>(o.status)},
            {"time", number(o.submission_time.time_since_epoch().count())},
            {"sequence", number(o.submission_dispatch_sequence)},
            {"reason", text(o.reason)},
            {"protection", number(p.protection.units)},
            {"reserved_cash", number(p.reserved_cash.units)},
            {"reserved_shares", number(p.reserved_shares)},
            {"submitted_ns", number(p.submitted_ns)},
            {"expiry_ns", number(p.expiry_ns)}};
}
Side side(const QJsonObject &o) {
    auto s = o.value("side").toString();
    if (s != "buy" && s != "sell")
        throw std::invalid_argument("Invalid side");
    return s == "buy" ? Side::Buy : Side::Sell;
}
PaperOrder orderFromJson(const QJsonObject &o) {
    PaperOrder p;
    p.order.id = natural(o, "id");
    p.order.request = {instrumentFromJson(o.value("instrument").toObject()), string(o, "strategy"), side(o),
                       integer(o, "quantity"), OrderType::Market};
    int status = o.value("status").toInt(-1);
    if (status < 0 || status > static_cast<int>(OrderStatus::Expired))
        throw std::invalid_argument("Invalid order status");
    p.order.status = static_cast<OrderStatus>(status);
    p.order.submission_time = time(o, "time");
    p.order.submission_dispatch_sequence = natural(o, "sequence");
    p.order.submission_sequence = p.order.submission_dispatch_sequence;
    p.order.reason = string(o, "reason");
    p.protection = {integer(o, "protection")};
    p.reserved_cash = {integer(o, "reserved_cash")};
    p.reserved_shares = integer(o, "reserved_shares");
    p.submitted_ns = integer(o, "submitted_ns");
    p.expiry_ns = integer(o, "expiry_ns");
    return p;
}
QJsonObject executionJson(const ExecutionReport &e) {
    return {{"order_id", number(e.order_id)},
            {"execution_id", number(e.execution_id)},
            {"instrument", instrumentJson(e.instrument)},
            {"time", number(e.fill_time.time_since_epoch().count())},
            {"side", e.side == Side::Buy ? "buy" : "sell"},
            {"quantity", number(e.quantity)},
            {"price", number(e.price.units)},
            {"fee", number(e.fee.units)}};
}
ExecutionReport executionFromJson(const QJsonObject &o) {
    return {natural(o, "order_id"),
            natural(o, "execution_id"),
            instrumentFromJson(o.value("instrument").toObject()),
            time(o, "time"),
            side(o),
            integer(o, "quantity"),
            Price{integer(o, "price")},
            Money{integer(o, "fee")},
            OrderStatus::Filled,
            {}};
}
QJsonObject paperJson(const PaperSnapshot &p) {
    QJsonObject o{{"id", text(p.id)},
                  {"state", text(p.state)},
                  {"reserved_cash", number(p.reserved_cash.units)},
                  {"reserved_shares", number(p.reserved_shares)},
                  {"bars", number(p.bars)},
                  {"orders", number(p.orders)},
                  {"fills", number(p.fills)},
                  {"rejected", number(p.rejected)},
                  {"expired", number(p.expired)},
                  {"pending", number(p.pending)},
                  {"warmup_required", number(p.warmup_required)},
                  {"warmup_completed", number(p.warmup_completed)}};
    if (p.account)
        o["account"] = portfolioJson(*p.account);
    if (p.mark_time)
        o["mark_time"] = number(p.mark_time->time_since_epoch().count());
    return o;
}
QJsonObject wrap(QJsonObject body, std::uint64_t index, const QByteArray &previous) {
    return {{"index", number(index)}, {"previous", QString::fromLatin1(previous)}, {"body", body}};
}
QByteArray digest(const QJsonObject &object) {
    return QCryptographicHash::hash(canonical(object), QCryptographicHash::Sha256).toHex();
}
} // namespace
QJsonObject configurationJson(const SessionConfiguration &c) {
    QJsonArray decisions;
    for (const auto &d : c.strategy.decisions)
        decisions.append(QJsonObject{{"bar", number(d.completed_bar_index)},
                                     {"side", d.side == Side::Buy ? "buy" : "sell"},
                                     {"quantity", number(d.quantity)}});
    return {{"instrument", instrumentJson(c.instrument)},
            {"initial_cash", number(c.initial_cash.units)},
            {"strategy", static_cast<int>(c.strategy.kind)},
            {"strategy_id", text(c.strategy.strategy_id)},
            {"lookback", number(c.strategy.lookback)},
            {"buy_quantity", number(c.strategy.buy_quantity)},
            {"threshold", c.strategy.threshold},
            {"vol_floor", c.strategy.volFloor},
            {"decisions", decisions},
            {"fee", number(c.execution.flat_fee.units)},
            {"slippage", number(c.execution.slippage_units)},
            {"max_order_quantity", number(c.maximum_order_quantity)},
            {"max_position", number(c.maximum_position)},
            {"max_order_notional", number(c.maximum_order_notional.units)},
            {"max_pending", number(c.maximum_pending)},
            {"max_records", number(c.maximum_records)},
            {"protection_bps", c.protection_bps},
            {"order_timeout_ms", number(c.order_timeout.count())},
            {"quote_max_age_ms", number(c.quote_max_age.count())},
            {"realtime_confirmed", c.realtime_confirmed}};
}
SessionConfiguration configurationFromJson(const QJsonObject &o) {
    SessionConfiguration c;
    c.instrument = instrumentFromJson(o.value("instrument").toObject());
    c.initial_cash = {integer(o, "initial_cash")};
    auto kind = o.value("strategy").toInt(-1);
    if (kind < 0 || kind > static_cast<int>(StrategyKind::VNMomentum))
        throw std::invalid_argument("Invalid strategy kind");
    c.strategy.kind = static_cast<StrategyKind>(kind);
    c.strategy.strategy_id = string(o, "strategy_id");
    c.strategy.lookback = natural(o, "lookback");
    c.strategy.buy_quantity = integer(o, "buy_quantity");
    c.strategy.threshold = o.value("threshold").toDouble();
    c.strategy.volFloor = o.value("vol_floor").toDouble();
    for (auto value : o.value("decisions").toArray()) {
        auto d = value.toObject();
        c.strategy.decisions.push_back({natural(d, "bar"), side(d), integer(d, "quantity")});
    }
    c.execution.flat_fee = {integer(o, "fee")};
    c.execution.slippage_units = integer(o, "slippage");
    c.maximum_order_quantity = integer(o, "max_order_quantity");
    c.maximum_position = integer(o, "max_position");
    c.maximum_order_notional = {integer(o, "max_order_notional")};
    c.maximum_pending = natural(o, "max_pending");
    c.maximum_records = natural(o, "max_records");
    c.protection_bps = o.value("protection_bps").toInt(-1);
    c.order_timeout = std::chrono::milliseconds(integer(o, "order_timeout_ms"));
    c.quote_max_age = std::chrono::milliseconds(integer(o, "quote_max_age_ms"));
    c.realtime_confirmed = o.value("realtime_confirmed").toBool();
    validateSession(c);
    return c;
}
QJsonObject inputJson(const Input &i) {
    QJsonObject o{{"kind", static_cast<int>(i.kind)},   {"sequence", number(i.sequence)},
                  {"command_id", number(i.command_id)}, {"time", number(i.time.time_since_epoch().count())},
                  {"clock_ns", number(i.clock_ns)},     {"receipt_ns", number(i.receipt_ns)},
                  {"reason", text(i.reason)},           {"session_id", text(i.session_id)},
                  {"period", number(i.period)}};
    if (i.trade)
        o["trade"] = tradeJson(*i.trade);
    if (i.quote)
        o["quote"] = quoteJson(*i.quote);
    if (i.configuration) {
        auto cfg = configurationJson(*i.configuration);
        o["configuration"] = cfg;
        o["configuration_hash"] = QString::fromLatin1(digest(cfg));
    }
    return o;
}
Input inputFromJson(const QJsonObject &o) {
    Input i;
    auto kind = o.value("kind").toInt(-1);
    if (kind < 0 || kind > static_cast<int>(Input::Kind::Failure))
        throw std::invalid_argument("Invalid recorded event kind");
    i.kind = static_cast<Input::Kind>(kind);
    i.sequence = natural(o, "sequence");
    i.command_id = natural(o, "command_id");
    i.time = time(o, "time");
    i.clock_ns = integer(o, "clock_ns");
    i.receipt_ns = integer(o, "receipt_ns");
    i.reason = string(o, "reason");
    i.session_id = string(o, "session_id");
    i.period = natural(o, "period");
    if (o.contains("trade")) {
        auto t = o.value("trade").toObject();
        Trade trade{time(t, "time"), Price{integer(t, "price")}, integer(t, "quantity_atoms"), {}};
        if (t.contains("id"))
            trade.id = natural(t, "id");
        i.trade = trade;
    }
    if (o.contains("quote")) {
        auto q = o.value("quote").toObject();
        LiveQuote quote{Price{integer(q, "bid")}, Price{integer(q, "ask")}, {}, integer(q, "receipt_ns"), {}};
        if (q.contains("time"))
            quote.source_time = time(q, "time");
        if (q.contains("update_id"))
            quote.update_id = natural(q, "update_id");
        i.quote = quote;
    }
    if (o.contains("configuration")) {
        auto cfg = o.value("configuration").toObject();
        if (o.value("configuration_hash").toString().toLatin1() != digest(cfg))
            throw std::invalid_argument("Configuration hash mismatch");
        i.configuration = configurationFromJson(cfg);
    }
    return i;
}
QJsonObject resultJson(const ProcessingResult &r) {
    QJsonArray bars, events;
    for (const auto &b : r.bars)
        bars.append(barJson(b));
    for (const auto &t : r.transitions) {
        QJsonObject o{{"kind", text(t.kind)}, {"reason", text(t.reason)}};
        if (t.order)
            o["order"] = orderJson(*t.order);
        if (t.execution)
            o["execution"] = executionJson(*t.execution);
        events.append(o);
    }
    return {{"quality", text(r.quality)}, {"bars", bars}, {"events", events}};
}
QJsonObject stateJson(const ProcessorState &s) {
    QJsonObject o{{"feed_state", text(s.feed_state)},
                  {"error", text(s.error)},
                  {"connected", s.connected},
                  {"subscribed", s.subscribed},
                  {"failed", s.failed},
                  {"processed", number(s.processed)},
                  {"trades", number(s.trades)},
                  {"quotes", number(s.quotes)},
                  {"rejected", number(s.rejected)},
                  {"duplicates", number(s.duplicates)},
                  {"gaps", number(s.gaps)},
                  {"reconnects", number(s.reconnects)},
                  {"late", number(s.late)},
                  {"command_ack", number(s.command_ack)},
                  {"time", number(s.now.time_since_epoch().count())},
                  {"clock_ns", number(s.clock_ns)},
                  {"indicator_period", number(s.indicator_period)},
                  {"warmup", number(s.warmup)},
                  {"paper", paperJson(s.paper)}};
    if (s.last)
        o["last"] = tradeJson(*s.last);
    if (s.quote)
        o["quote"] = quoteJson(*s.quote);
    if (s.forming)
        o["forming"] = barJson(*s.forming);
    if (s.sma)
        o["sma"] = *s.sma;
    if (s.ema)
        o["ema"] = *s.ema;
    if (s.rsi)
        o["rsi"] = *s.rsi;
    return o;
}
ArchiveWriter::ArchiveWriter(const std::string &path, QJsonObject metadata) : recorder_(path) {
    metadata["kind"] = "metadata";
    metadata["schema"] = 3;
    metadata["bar_interval_seconds"] = 60;
    metadata["grace_ms"] = 2000;
    metadata["calendar"] = "observed trade minutes only; source-time freshness required";
    metadata["mark_policy"] = "last observed nondecreasing source-time trade; frozen on stop";
    metadata["execution_model"] = "long-only whole-share USD; subsequent fresh timestamped quotes; full "
                                  "fill; protected price; flat fee";
    metadata["indicator_policy"] =
        "SMA n; SMA-seeded EMA 2/(n+1); Wilder RSI n+1 closes; flat=50; reset after integrity gap";
    metadata["quantity_scale"] = number(quantity_scale);
    metadata["price_scale"] = number(usd_scale);
#ifndef M3_BUILD_ID
#define M3_BUILD_ID "unknown"
#endif
    metadata["build_id"] = M3_BUILD_ID;
#ifdef M3_COMPILER_INFO
    metadata["compiler_info"] = M3_COMPILER_INFO;
#endif
#ifdef M3_SOURCE_HASH
    metadata["code_sha256"] = M3_SOURCE_HASH;
#endif
    write(metadata);
}
void ArchiveWriter::write(QJsonObject body) {
    auto frame = wrap(body, records_, previous_);
    auto hash = digest(frame);
    frame["sha256"] = QString::fromLatin1(hash);
    if (!recorder_.append(canonical(frame).toStdString()))
        throw std::runtime_error("Audit recorder unavailable/overflow");
    previous_ = hash;
    ++records_;
}
void ArchiveWriter::step(const Input &in, const ProcessingResult &result, const ProcessorState &state) {
    write({{"kind", "step"},
           {"input", inputJson(in)},
           {"output", resultJson(result)},
           {"commit", stateJson(state)}});
}
void ArchiveWriter::finish(const ProcessorState &state) {
    write({{"kind", "terminal"},
           {"complete", true},
           {"steps", number(state.processed)},
           {"final", stateJson(state)}});
    recorder_.close();
}
std::size_t ArchiveWriter::highWater() const { return recorder_.highWater(); }
Archive loadArchive(const std::string &path) {
    QFile file(text(path));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot open recording");
    if (file.size() > 64 * 1024 * 1024)
        throw std::invalid_argument("Recording exceeds 64 MiB reader limit");
    Archive out;
    QByteArray previous(64, '0');
    std::uint64_t index = 0;
    bool terminal = false;
    while (!file.atEnd()) {
        auto line = file.readLine(1024 * 1024 + 1);
        if (line.size() > 1024 * 1024 || !line.endsWith('\n'))
            throw std::invalid_argument("Incomplete or oversized recording record");
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            throw std::invalid_argument("Malformed recording record");
        auto frame = doc.object();
        auto hash = frame.take("sha256").toString().toLatin1();
        if (terminal || natural(frame, "index") != index ||
            frame.value("previous").toString().toLatin1() != previous || digest(frame) != hash)
            throw std::invalid_argument("Recording hash chain/order mismatch");
        previous = hash;
        ++index;
        auto body = frame.value("body").toObject();
        auto kind = body.value("kind").toString();
        if (index == 1) {
            if (kind != "metadata" || body.value("schema").toInt() != 3 || !body.value("symbol").isString())
                throw std::invalid_argument("Unsupported recording schema/metadata");
            out.metadata = body;
        } else if (kind == "step")
            out.steps.push_back(body);
        else if (kind == "terminal") {
            terminal = true;
            out.terminal = body;
        } else
            throw std::invalid_argument("Unexpected recording record");
    }
    if (!terminal || !out.terminal.value("complete").toBool() ||
        natural(out.terminal, "steps") != out.steps.size())
        throw std::invalid_argument("Unfinished recording: review only; automatic resume unsupported");
    std::uint64_t sequence = 0;
    for (const auto &step : out.steps)
        if (natural(step.value("input").toObject(), "sequence") != ++sequence)
            throw std::invalid_argument("Missing recorded input position");
    return out;
}
ReviewResult decisionReplay(const Archive &archive, double speed, std::atomic<bool> *stop,
                            const std::function<void(const ProcessorState &)> &progress) {
    ReviewResult result;
    auto start = std::chrono::steady_clock::now();
    TimingSeries timings;
    try {
        if (speed < 0 || speed > 10000)
            throw std::invalid_argument("Replay speed outside 0..10000");
        LiveProcessor processor(string(archive.metadata, "symbol"));
        std::optional<std::int64_t> origin;
        for (const auto &step : archive.steps) {
            if (stop && stop->load()) {
                result.cancelled = true;
                result.message = "Review cancelled";
                result.state = processor.state();
                return result;
            }
            auto input = inputFromJson(step.value("input").toObject());
            if (!origin)
                origin = input.clock_ns;
            if (speed > 0) {
                auto due = start + std::chrono::nanoseconds(
                                       static_cast<std::int64_t>((input.clock_ns - *origin) / speed));
                while (std::chrono::steady_clock::now() < due) {
                    if (stop && stop->load()) {
                        result.cancelled = true;
                        result.message = "Review cancelled";
                        return result;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            auto before = std::chrono::steady_clock::now();
            auto output = processor.apply(input);
            auto after = std::chrono::steady_clock::now();
            timings.add(std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
            if (resultJson(output) != step.value("output").toObject() ||
                stateJson(processor.state()) != step.value("commit").toObject())
                throw std::runtime_error("Decision/commit mismatch at input " +
                                         std::to_string(input.sequence));
            for (const auto &event : output.transitions)
                if (event.execution)
                    ++result.executions;
            ++result.steps;
            if (progress && result.steps % 100 == 0)
                progress(processor.state());
        }
        if (stateJson(processor.state()) != archive.terminal.value("final").toObject())
            throw std::runtime_error("Terminal replay mismatch");
        result.state = processor.state();
        // Total execution count accumulated across all recorded account sessions.
        result.passed = true;
        result.message = "Decision replay matches all outputs and exact committed state";
    } catch (const std::exception &e) {
        result.message = e.what();
    }
    result.processing = timings.summary();
    result.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}
ReviewResult ledgerAudit(const Archive &archive, std::atomic<bool> *stop) {
    ReviewResult result;
    auto start = std::chrono::steady_clock::now();
    try {
        std::unique_ptr<PortfolioAccounting> account;
        std::map<OrderId, PaperOrder> orders;
        std::set<ExecutionId> executions;
        std::optional<SessionConfiguration> config;
        std::vector<ExecutionReport> ledger_executions;
        for (const auto &step : archive.steps) {
            if (stop && stop->load()) {
                result.cancelled = true;
                result.message = "Audit cancelled";
                return result;
            }
            auto input = inputFromJson(step.value("input").toObject());
            auto commit = step.value("commit").toObject();
            auto paper = commit.value("paper").toObject();
            auto events = step.value("output").toObject().value("events").toArray();
            for (const auto &value : events) {
                auto event = value.toObject();
                auto kind = event.value("kind").toString();
                if (kind == "start") {
                    if (!input.configuration || input.kind != Input::Kind::Start)
                        throw std::runtime_error("Start ledger configuration missing");
                    config = input.configuration;
                    account = std::make_unique<PortfolioAccounting>(config->initial_cash);
                    orders.clear();
                    executions.clear();
                    ledger_executions.clear();
                }
                if (!event.contains("order"))
                    continue;
                if (!account || !config)
                    throw std::runtime_error("Order before account start");
                auto order = orderFromJson(event.value("order").toObject());
                if (kind == "order") {
                    if (orders.contains(order.order.id) || order.order.id != orders.size() + 1)
                        throw std::runtime_error("Duplicate/missing order identity");
                    if (order.order.status == OrderStatus::Accepted) {
                        auto qty = order.order.request.quantity;
                        if (qty <= 0 || qty > config->maximum_order_quantity ||
                            order.order.request.instrument != config->instrument ||
                            order.protection.units <= 0)
                            throw std::runtime_error("Invalid accepted order");
                        auto notional = checkedNotional(qty, order.protection);
                        if (notional.units > config->maximum_order_notional.units)
                            throw std::runtime_error("Accepted notional risk violation");
                        if (order.order.request.side == Side::Buy) {
                            if (order.reserved_cash != checkedAdd(notional, config->execution.flat_fee) ||
                                order.reserved_shares)
                                throw std::runtime_error("Buy reservation mismatch");
                        } else if (order.reserved_cash.units || order.reserved_shares != qty)
                            throw std::runtime_error("Sell reservation mismatch");
                    } else if (order.order.status != OrderStatus::Rejected || order.reserved_cash.units ||
                               order.reserved_shares)
                        throw std::runtime_error("Invalid rejected order transition");
                    orders.emplace(order.order.id, order);
                } else if (kind == "fill" || kind == "expire") {
                    auto old = orders.find(order.order.id);
                    if (old == orders.end() || old->second.order.status != OrderStatus::Accepted ||
                        old->second.order.request != order.order.request || order.reserved_cash.units ||
                        order.reserved_shares)
                        throw std::runtime_error("Invalid terminal order transition/reservation release");
                    if (kind == "fill") {
                        auto e = executionFromJson(event.value("execution").toObject());
                        if (order.order.status != OrderStatus::Filled || e.order_id != order.order.id ||
                            e.quantity != order.order.request.quantity ||
                            e.side != order.order.request.side || e.instrument != config->instrument ||
                            e.fee != config->execution.flat_fee || !executions.insert(e.execution_id).second)
                            throw std::runtime_error("Execution identity/quantity/fee mismatch or duplicate");
                        if (input.kind != Input::Kind::Quote || !input.quote ||
                            input.sequence <= old->second.order.submission_dispatch_sequence ||
                            input.quote->receipt_ns < old->second.submitted_ns ||
                            !freshQuote(*input.quote, input.time, input.clock_ns, config->quote_max_age))
                            throw std::runtime_error("Ineligible recorded execution opportunity");
                        auto expected = e.side == Side::Buy
                                            ? checkedAdd(Money{input.quote->ask.units},
                                                         Money{config->execution.slippage_units})
                                                  .units
                                            : checkedSubtract(Money{input.quote->bid.units},
                                                              Money{config->execution.slippage_units})
                                                  .units;
                        if (e.price.units != expected || e.fill_time != input.time ||
                            (e.side == Side::Buy ? e.price.units > old->second.protection.units
                                                 : e.price.units < old->second.protection.units))
                            throw std::runtime_error("Fill price/protection mismatch");
                        if (account->applyFill(e).status != FillApplyStatus::Applied)
                            throw std::runtime_error("Ledger account rejected fill");
                        ++result.executions;
                        ledger_executions.push_back(e);
                    } else if (order.order.status != OrderStatus::Expired)
                        throw std::runtime_error("Invalid expiry status");
                    old->second = order;
                } else
                    throw std::runtime_error("Unknown ledger transition");
            }
            if (account) {
                auto recorded = paper.value("account").toObject();
                // Ledger audit uses recorded marks, independently recomputing financial valuations.
                for (const auto &v : recorded.value("positions").toArray()) {
                    auto pos = v.toObject();
                    if (pos.contains("mark"))
                        account->updateMark(natural(pos.value("instrument").toObject(), "id"),
                                            Price{integer(pos, "mark")});
                }
                if (portfolioJson(account->snapshot()) != recorded)
                    throw std::runtime_error("Ledger cash/position/cost/fee/PnL mismatch");
                Money reserved;
                Quantity shares = 0, pending_buys = 0;
                std::size_t pending = 0;
                for (const auto &[id, p] : orders) {
                    (void)id;
                    if (p.order.status != OrderStatus::Accepted)
                        continue;
                    ++pending;
                    reserved = checkedAdd(reserved, p.reserved_cash);
                    shares += p.reserved_shares;
                    if (p.order.request.side == Side::Buy)
                        pending_buys += p.order.request.quantity;
                }
                if (natural(paper, "orders") != orders.size() || natural(paper, "fills") != executions.size())
                    throw std::runtime_error("Ledger order/fill count mismatch");
                if (reserved.units != integer(paper, "reserved_cash") ||
                    shares != integer(paper, "reserved_shares") || pending != natural(paper, "pending") ||
                    reserved.units > account->cash().units ||
                    shares > account->quantity(config->instrument.id) || pending > config->maximum_pending ||
                    pending_buys + account->quantity(config->instrument.id) > config->maximum_position)
                    throw std::runtime_error("Ledger reservations/position constraints do not reconcile");
            } else if (paper.contains("account"))
                throw std::runtime_error("Unexpected account without start");
            ++result.steps;
        }
        if (!archive.steps.empty() && archive.steps.back().value("commit") != archive.terminal.value("final"))
            throw std::runtime_error("Terminal ledger mismatch");
        if (account) {
            const auto final_paper = archive.terminal.value("final").toObject().value("paper").toObject();
            result.state.paper.account = account->snapshot();
            result.state.paper.id = string(final_paper, "id");
            result.state.paper.state = string(final_paper, "state");
            result.state.paper.orders = orders.size();
            result.state.paper.fills = executions.size();
            result.state.processed = result.steps;
            for (const auto &[id, p] : orders) {
                (void)id;
                result.state.paper.recent_orders.push_back(p);
            }
            if (result.state.paper.recent_orders.size() > 200)
                result.state.paper.recent_orders.erase(result.state.paper.recent_orders.begin(),
                                                       result.state.paper.recent_orders.end() - 200);
            result.state.paper.recent_executions = ledger_executions;
            if (result.state.paper.recent_executions.size() > 200)
                result.state.paper.recent_executions.erase(result.state.paper.recent_executions.begin(),
                                                           result.state.paper.recent_executions.end() - 200);
        }
        result.passed = true;
        result.message =
            "Independent ledger audit reconciles fills, reservations, cash, holdings, cost, fees and PnL";
    } catch (const std::exception &e) {
        result.message = e.what();
    }
    result.elapsed_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}
QString replayPerformanceMarkdown(const Archive &archive, const ReviewResult &result) {
    const auto &s = result.processing;
    auto optional = [](std::optional<double> n) {
        return n ? QString::number(*n, 'f', 3) : "insufficient samples";
    };
    return QString("# M3 replay performance\n\nBuild: %1. OS: %2. Architecture: %3. Logical CPUs: "
                   "%4.\n\nSynthetic/provider mode: %5. Steps: %6. Replay elapsed: %7 s. Throughput: %8 "
                   "events/s.\n\n| Core apply metric | Microseconds |\n|---|---:|\n| p50 | %9 |\n| p95 | %10 "
                   "|\n| p99 | %11 |\n| p99.9 | %12 |\n| maximum | %13 |\n\nSamples retained: %14 of %15 "
                   "(100,000-sample ring). p99 requires 1,000 retained samples; p99.9 requires 10,000. This "
                   "measures deterministic core apply separately from archive decoding/comparison; total "
                   "throughput includes review overhead. No network/GUI/disk-write cost or "
                   "exchange-to-application latency is implied. Allocation counts, CPU utilization and RSS "
                   "are not measured by this replay-only report.\n\nValidation: %16\n")
        .arg(archive.metadata.value("build_id").toString(), QSysInfo::prettyProductName(),
             QSysInfo::currentCpuArchitecture())
        .arg(QThread::idealThreadCount())
        .arg(archive.metadata.value("source_mode").toString())
        .arg(result.steps)
        .arg(result.elapsed_seconds, 0, 'f', 6)
        .arg(result.elapsed_seconds > 0 ? result.steps / result.elapsed_seconds : 0, 0, 'f', 2)
        .arg(s.p50_us, 0, 'f', 3)
        .arg(s.p95_us, 0, 'f', 3)
        .arg(optional(s.p99_us), optional(s.p999_us))
        .arg(s.maximum_us, 0, 'f', 3)
        .arg(s.retained)
        .arg(s.count)
        .arg(text(result.message));
}
} // namespace trading_app_cli::live
