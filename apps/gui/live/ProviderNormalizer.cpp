#include "ProviderNormalizer.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace trading_app_cli::live {
std::int64_t decimalAtoms(const QString &text, int places) {
    auto s = text.toStdString();
    if (s.empty() || s.size() > 80)
        throw std::invalid_argument("invalid decimal");
    std::int64_t result = 0;
    int fractional = -1;
    for (char c : s) {
        if (c == '.' && fractional < 0) {
            fractional = 0;
            continue;
        }
        if (c < '0' || c > '9')
            throw std::invalid_argument("invalid decimal");
        if (fractional >= places) {
            if (c != '0')
                throw std::invalid_argument("unsupported precision");
            continue;
        }
        if (result > (std::numeric_limits<std::int64_t>::max() - (c - '0')) / 10)
            throw std::overflow_error("decimal overflow");
        result = result * 10 + (c - '0');
        if (fractional >= 0)
            ++fractional;
    }
    if (s == "." || s.back() == '.')
        throw std::invalid_argument("invalid decimal");
    for (int i = fractional < 0 ? 0 : fractional; i < places; ++i) {
        if (result > std::numeric_limits<std::int64_t>::max() / 10)
            throw std::overflow_error("decimal overflow");
        result *= 10;
    }
    return result;
}
namespace {
std::int64_t integer(QJsonValue v) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0 ||
        v.toDouble() > 9'007'199'254'740'991.0 || std::floor(v.toDouble()) != v.toDouble())
        throw std::invalid_argument("invalid integer");
    return v.toInteger(-1);
}
std::int64_t numberAtoms(QJsonValue v, int places) {
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0)
        throw std::invalid_argument("invalid number");
    const double scale = std::pow(10.0, places), x = v.toDouble() * scale;
    if (x > 9'007'199'254'740'991.0)
        throw std::overflow_error("number range");
    const double rounded = std::round(x);
    if (std::abs(x - rounded) > std::max(1e-5, 4 * std::numeric_limits<double>::epsilon() * std::abs(x)))
        throw std::invalid_argument("unsupported precision");
    return static_cast<std::int64_t>(rounded);
}
Trade parse(const QJsonObject &o, Provider provider, const std::string &symbol, EventTime receipt) {
    const bool binance = provider == Provider::Binance;
    if (o.value(binance ? "s" : "s").toString().toStdString() != symbol)
        throw std::invalid_argument("unexpected symbol");
    auto ms = integer(o.value(binance ? "T" : "t"));
    // Millisecond UTC timestamps supported by sys_time<nanoseconds>.
    if (ms <= 0 || ms > 9'000'000'000'000LL)
        throw std::invalid_argument("timestamp range");
    Trade t;
    t.time = EventTime{std::chrono::milliseconds(ms)};
    if (t.time > receipt + std::chrono::seconds(5) || t.time < receipt - std::chrono::hours(24))
        throw std::invalid_argument("timestamp outside receipt window");
    t.price.units = binance ? decimalAtoms(o.value("p").toString(), 4) : numberAtoms(o.value("p"), 4);
    t.quantity_atoms = binance ? decimalAtoms(o.value("q").toString(), 8) : numberAtoms(o.value("v"), 8);
    if (t.price.units <= 0 || t.quantity_atoms <= 0)
        throw std::invalid_argument("non-positive trade or price-only update");
    if (binance)
        t.id = static_cast<std::uint64_t>(integer(o.value("t")));
    return t;
}
} // namespace
DecodeResult decode(Provider provider, const std::string &symbol, const std::string &raw, EventTime receipt) {
    DecodeResult r;
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(raw), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        r.reasons.push_back("malformed JSON");
        return r;
    }
    auto o = doc.object();
    const auto type = o.value("type").toString();
    if (type == "quote_unavailable") {
        r.quote_unavailable = true;
        return r;
    }
    if ((provider == Provider::Binance && o.contains("u") && o.contains("b") && o.contains("a")) ||
        (provider == Provider::Finnhub && type == "bidask")) {
        try {
            if (o.value("s").toString().toStdString() != symbol)
                throw std::invalid_argument("unexpected quote instrument");
            LiveQuote q;
            if (provider == Provider::Binance) {
                q.bid = {decimalAtoms(o.value("b").toString(), 4)};
                q.ask = {decimalAtoms(o.value("a").toString(), 4)};
                q.update_id = integer(o.value("u"));
            } else {
                q.bid = {numberAtoms(o.value("b"), 4)};
                q.ask = {numberAtoms(o.value("a"), 4)};
                auto ms = integer(o.value("t"));
                if (ms <= 0 || ms > 9000000000000LL)
                    throw std::invalid_argument("invalid quote timestamp");
                q.source_time = EventTime{std::chrono::milliseconds(ms)};
            }
            if (q.bid.units <= 0 || q.ask.units < q.bid.units)
                throw std::invalid_argument("crossed or nonpositive quote");
            r.quotes.push_back(q);
        } catch (const std::exception &e) {
            r.reasons.push_back(e.what());
        }
        return r;
    }
    if (type == "ping") {
        r.heartbeat = true;
        return r;
    }
    if (type == "error" || o.contains("code")) {
        r.reasons.push_back("provider rejected subscription");
        r.fatal = true;
        return r;
    }
    if (provider == Provider::Binance && o.contains("id") && o.contains("result")) {
        r.acknowledgement = o.value("id").toInteger() == 1 && o.value("result").isNull();
        if (!r.acknowledgement)
            r.reasons.push_back("unexpected subscription response");
        return r;
    }
    if (provider == Provider::Binance) {
        if (o.value("e").toString() != "trade") {
            r.reasons.push_back("unexpected Binance event");
            return r;
        }
        try {
            r.trades.push_back(parse(o, provider, symbol, receipt));
        } catch (const std::exception &e) {
            r.reasons.push_back(e.what());
        }
    } else {
        if (type != "trade" || !o.value("data").isArray()) {
            r.reasons.push_back("unexpected Finnhub event");
            return r;
        }
        auto data = o.value("data").toArray();
        if (data.size() > 4096) {
            r.reasons.push_back("trade batch exceeds limit");
            return r;
        }
        for (const auto &value : data) {
            try {
                if (!value.isObject())
                    throw std::invalid_argument("invalid trade object");
                r.trades.push_back(parse(value.toObject(), provider, symbol, receipt));
            } catch (const std::exception &e) {
                r.reasons.push_back(e.what());
            }
        }
    }
    return r;
}
} // namespace trading_app_cli::live
