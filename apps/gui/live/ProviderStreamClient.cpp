#include "ProviderStreamClient.hpp"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>
#include <algorithm>
#include <stdexcept>
namespace trading_app_cli::live {
namespace {
// Use fixed descriptions because Qt errors can contain a URL with the Finnhub token.
const char *socketFailure(QAbstractSocket::SocketError error) {
    switch (error) {
    case QAbstractSocket::HostNotFoundError:
        return "DNS lookup failed";
    case QAbstractSocket::ConnectionRefusedError:
        return "connection refused";
    case QAbstractSocket::RemoteHostClosedError:
        return "server closed the connection";
    case QAbstractSocket::SocketTimeoutError:
        return "socket timed out";
    case QAbstractSocket::NetworkError:
        return "network error";
    case QAbstractSocket::SslHandshakeFailedError:
        return "TLS handshake failed";
    case QAbstractSocket::ProxyAuthenticationRequiredError:
        return "proxy authentication required";
    default:
        return "WebSocket error";
    }
}
} // namespace

void runProviderStream(const Configuration &config, LiveEventQueue<Envelope> &queue, std::atomic<bool> &stop,
                       std::atomic<bool> &overflow, std::chrono::steady_clock::time_point origin,
                       const std::string &loopback_test_endpoint) {
    QUrl test_url(QString::fromStdString(loopback_test_endpoint));
    if (!loopback_test_endpoint.empty() &&
        (test_url.scheme() != "ws" || test_url.host() != "127.0.0.1" || !test_url.userInfo().isEmpty()))
        throw std::invalid_argument("Test transport override must use loopback ws");
    QEventLoop loop;
    QNetworkAccessManager rest;
    bool rest_requested = false;
    QWebSocket socket;
    socket.setMaxAllowedIncomingMessageSize(64 * 1024);
    socket.setMaxAllowedIncomingFrameSize(64 * 1024);
    QTimer clock, retry, bidask;
    std::uint64_t generation = 0;
    bool bidask_pending = false;
    retry.setSingleShot(true);
    std::uint64_t sequence = 0;
    int attempts = 0;
    bool ending = false, waiting_pong = false;
    std::string endpoint_host;
    const auto stream_symbol = QString::fromStdString(config.symbol).toLower();
    auto last_activity = std::chrono::steady_clock::now(), opened = last_activity, last_ping = last_activity;
    auto send = [&](Envelope::Kind kind, std::string text = {}) {
        Envelope e{
            ++sequence,
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - origin)
                .count(),
            utcNow(), kind, std::move(text)};
        e.enqueued_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - origin)
                .count();
        if (!queue.push(std::move(e))) {
            overflow = true;
            stop = true;
            loop.quit();
        }
    };
    auto open = [&] {
        if (stop)
            return;
        ++generation;
        opened = last_activity = std::chrono::steady_clock::now();
        waiting_pong = false;
        bidask.stop();
        QUrl url(config.provider == Provider::Binance
                     ? (attempts % 2 == 0 ? "wss://data-stream.binance.vision:443/ws"
                                          : "wss://stream.binance.com:443/ws")
                     : "wss://ws.finnhub.io");
        if (!loopback_test_endpoint.empty())
            url = test_url;
        endpoint_host = url.host().toStdString();
        if (config.provider == Provider::Binance) {
            // The URL itself subscribes to trades. Add book ticker after the upgrade.
            url.setPath(QStringLiteral("/ws/") + stream_symbol + QStringLiteral("@trade"));
        } else {
            // Match the successful Finnhub HTTP upgrade target: /?token=<encoded token>.
            url.setPath(QStringLiteral("/"));
            const auto token = QUrl::toPercentEncoding(QString::fromStdString(config.api_key));
            url.setQuery(QStringLiteral("token=") + QString::fromLatin1(token), QUrl::StrictMode);
        }
        socket.open(url); // Keep Qt's default certificate verification.
    };
    auto recover = [&](const std::string &cause) {
        if (ending || stop || retry.isActive())
            return;
        const auto reason = endpoint_host + ": " + cause;
        send(Envelope::Kind::Gap, reason);
        socket.blockSignals(true);
        socket.abort();
        socket.blockSignals(false);
        if (stop)
            return;
        if (++attempts > 5) {
            send(Envelope::Kind::Rejected, "Reconnect attempts exhausted. Last failure: " + reason);
            stop = true;
            loop.quit();
            return;
        }
        retry.start(std::min(16000, 1000 * (1 << (attempts - 1))));
    };
    QObject::connect(&retry, &QTimer::timeout, &loop, open);
    QObject::connect(&socket, &QWebSocket::connected, &loop, [&] {
        last_activity = std::chrono::steady_clock::now();
        QJsonObject request;
        if (config.provider == Provider::Binance) {
            request = {
                {"method", "SUBSCRIBE"},
                {"params", QJsonArray{stream_symbol + QStringLiteral("@bookTicker")}},
                {"id", 1}};
        } else
            request = {{"type", "subscribe"}, {"symbol", QString::fromStdString(config.symbol)}};
        socket.sendTextMessage(QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact)));
        send(Envelope::Kind::Connected, "transport connected; awaiting subscription evidence");
        if (config.provider == Provider::Finnhub && config.bidask_enabled)
            bidask.start(config.bidask_poll_ms);
        if (config.provider == Provider::Finnhub && !rest_requested) {
            rest_requested = true;
            QUrl url("https://finnhub.io/api/v1/quote");
            QUrlQuery query;
            query.addQueryItem("symbol", QString::fromStdString(config.symbol));
            url.setQuery(query);
            QNetworkRequest req(url);
            req.setTransferTimeout(10000);
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            req.setRawHeader("X-Finnhub-Token", QByteArray::fromStdString(config.api_key));
            auto *reply = rest.get(req);
            reply->setReadBufferSize(65536);
            const auto request_generation = generation;
            QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply, request_generation] {
                if (request_generation != generation || socket.state() != QAbstractSocket::ConnectedState) {
                    reply->deleteLater();
                    return;
                }
                // REST is a reference last price, never a bid/ask or trade-volume input.
                auto doc = QJsonDocument::fromJson(reply->read(65536));
                if (reply->error() == QNetworkReply::NoError && doc.isObject()) {
                    auto o = doc.object();
                    QJsonObject safe{{"c", o.value("c")}, {"t", o.value("t")}};
                    send(Envelope::Kind::Reference,
                         QJsonDocument(safe).toJson(QJsonDocument::Compact).toStdString());
                } else
                    send(Envelope::Kind::Reference, "{}");
                reply->deleteLater();
            });
        }
    });
    QObject::connect(&bidask, &QTimer::timeout, &loop, [&] {
        if (stop || bidask_pending || socket.state() != QAbstractSocket::ConnectedState)
            return;
        QUrl url("https://finnhub.io/api/v1/stock/bidask");
        QUrlQuery query;
        query.addQueryItem("symbol", QString::fromStdString(config.symbol));
        url.setQuery(query);
        QNetworkRequest req(url);
        req.setTransferTimeout(10000);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setRawHeader("X-Finnhub-Token", QByteArray::fromStdString(config.api_key));
        bidask_pending = true;
        auto *reply = rest.get(req);
        reply->setReadBufferSize(65536);
        const auto request_generation = generation;
        QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply, request_generation] {
            bidask_pending = false;
            if (request_generation != generation || socket.state() != QAbstractSocket::ConnectedState) {
                reply->deleteLater();
                return;
            }
            auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            auto doc = QJsonDocument::fromJson(reply->read(65536));
            if (reply->error() == QNetworkReply::NoError && status == 200 && doc.isObject()) {
                const auto o = doc.object();
                QJsonObject safe{{"type", "bidask"},
                                 {"s", QString::fromStdString(config.symbol)},
                                 {"b", o.value("b")},
                                 {"a", o.value("a")},
                                 {"t", o.value("t")}};
                send(Envelope::Kind::Raw, QJsonDocument(safe).toJson(QJsonDocument::Compact).toStdString());
            } else {
                send(Envelope::Kind::Raw, "{\"type\":\"quote_unavailable\"}");
                if (status == 401 || status == 403)
                    bidask.stop();
                else if (status == 429)
                    bidask.setInterval(60000);
            }
            reply->deleteLater();
        });
    });
    QObject::connect(&socket, &QWebSocket::textMessageReceived, &loop, [&](const QString &message) {
        last_activity = std::chrono::steady_clock::now();
        send(Envelope::Kind::Raw, message.toStdString());
    });
    QObject::connect(&socket, &QWebSocket::pong, &loop, [&](quint64, const QByteArray &) {
        waiting_pong = false;
        last_activity = std::chrono::steady_clock::now();
        send(Envelope::Kind::Heartbeat);
    });
    QObject::connect(&socket, &QWebSocket::disconnected, &loop, [&] {
        // Let the socket error signal supply its details before handling a plain close.
        QTimer::singleShot(0, &loop, [&, disconnected_generation = generation] {
            if (disconnected_generation == generation)
                recover("WebSocket disconnected");
        });
    });
    auto onSocketError = [&](QAbstractSocket::SocketError error) {
        auto cause = std::string(socketFailure(error)) + " (Qt error " + std::to_string(error) + ")";
        // The public Binance endpoint has no credentials. Finnhub URLs carry a token.
        if (config.provider == Provider::Binance) {
            QString detail = socket.errorString().simplified().left(300);
            if (!detail.isEmpty())
                cause += ": " + detail.toStdString();
        } else {
            // Qt can assign ConnectionRefusedError to an HTTP upgrade rejection.
            // Extract only the numeric status, never the authenticated URL or response text.
            static const QRegularExpression http_status(
                QStringLiteral(R"(Unhandled http status code:\s*([1-5][0-9]{2})\b)"),
                QRegularExpression::CaseInsensitiveOption);
            const auto match = http_status.match(socket.errorString());
            if (match.hasMatch())
                cause = "Finnhub rejected the WebSocket upgrade (HTTP " + match.captured(1).toStdString() + ")";
        }
        recover(cause);
    };
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(&socket, &QWebSocket::errorOccurred, &loop, onSocketError);
#else
    QObject::connect(&socket, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), &loop,
                     onSocketError);
#endif
    QObject::connect(&clock, &QTimer::timeout, &loop, [&] {
        if (stop) {
            loop.quit();
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (socket.state() != QAbstractSocket::ConnectedState && !retry.isActive() &&
            now - opened > std::chrono::seconds(15)) {
            recover("WebSocket connection timed out");
            return;
        }
        if (waiting_pong && now - last_ping > std::chrono::seconds(30)) {
            recover("heartbeat timed out");
            return;
        }
        if (socket.state() == QAbstractSocket::ConnectedState && !waiting_pong &&
            now - last_ping > std::chrono::seconds(10)) {
            waiting_pong = true;
            last_ping = now;
            socket.ping("health");
        }
        // Time decisions share the same FIFO order as data, not the GUI clock.
        if (!retry.isActive())
            send(Envelope::Kind::Timer);
    });
    if (loopback_test_endpoint.empty() && !QSslSocket::supportsSsl()) {
        send(Envelope::Kind::Rejected, "Qt has no usable TLS backend for secure WebSocket connections");
        queue.close();
        return;
    }
    clock.start(100);
    open();
    loop.exec();
    ending = true;
    retry.stop();
    clock.stop();
    bidask.stop();
    socket.abort();
    // The producer closes only after its final event; queued observations drain.
    if (!overflow)
        send(Envelope::Kind::End, "user disconnect or transport failure");
    queue.close();
}
} // namespace trading_app_cli::live
