#include "tgui/jsonrpc.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>

namespace tgui {

JsonRpcClient::JsonRpcClient(const QUrl& url, const QString& rpcPath)
    : m_url(url), m_rpcPath(rpcPath.isEmpty() ? kDefaultRpcPath : rpcPath)
{
    m_nam = std::make_unique<QNetworkAccessManager>();
}

void JsonRpcClient::setUsernamePassword(const QString& user, const QString& password)
{
    m_user = user;
    m_password = password;
}

void JsonRpcClient::setIgnoreSslErrors(bool ignore)
{
    m_ignoreSslErrors = ignore;
}

void JsonRpcClient::setTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() > 0)
        m_timeout = timeout;
}

void JsonRpcClient::call(const QString& method, const QJsonObject& arguments, Callback cb)
{
    QJsonObject payload;
    payload.insert("method", method);
    if (!arguments.isEmpty())
        payload.insert("arguments", arguments);
    doRequest(payload, 1, std::move(cb));
}

JsonRpcResult JsonRpcClient::callSync(const QString& method, const QJsonObject& arguments,
                                      std::chrono::milliseconds timeout)
{
    JsonRpcResult out;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    call(method, arguments, [&loop, &out](const JsonRpcResult& r) {
        out = r;
        loop.quit();
    });
    timer.start(timeout);
    loop.exec();
    return out;
}

namespace {

QJsonObject makePayload(const QString& method, const QJsonObject& arguments)
{
    QJsonObject payload;
    payload.insert("method", method);
    if (!arguments.isEmpty())
        payload.insert("arguments", arguments);
    return payload;
}

} // namespace

void JsonRpcClient::doRequest(const QJsonObject& payload, int attempt, Callback cb)
{
    const QString fullUrl = m_url.toString() + m_rpcPath;
    QNetworkRequest request(fullUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    // Редиректы обрабатываем сами (301 → новый RPC-путь, Transmission 4.x).
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    if (!m_sessionId.isEmpty())
        request.setRawHeader("x-transmission-session-id", m_sessionId.toUtf8());
    if (!m_user.isEmpty()) {
        // HTTP Basic auth.
        const QByteArray credentials = (m_user + ":" + m_password).toUtf8();
        request.setRawHeader("Authorization",
                             "Basic " + credentials.toBase64());
    }
    if (m_timeout.count() > 0)
        request.setTransferTimeout(static_cast<int>(m_timeout.count()));
    if (m_ignoreSslErrors) {
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::PeerVerifyMode::VerifyNone);
        request.setSslConfiguration(ssl);
    }

    const QString method = payload.value("method").toString();
    const QJsonObject arguments = payload.value("arguments").toObject();

    QNetworkReply* reply =
        m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply,
            [this, reply, attempt, method, arguments, cb = std::move(cb)]() mutable {
                JsonRpcResult res;
                res.httpCode =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                // 409: нет/протух session id — взять новый из заголовка ответа
                // и повторить 1 раз; повторный 409 — "Session ID error.";
                // 409 без заголовка — обычная HTTP-ошибка.
                if (res.httpCode == 409) {
                    const QByteArray header = reply->rawHeader("x-transmission-session-id");
                    if (!header.isEmpty() && attempt < 2) {
                        m_sessionId.clear();
                        m_sessionId = QString::fromUtf8(header);
                        reply->deleteLater();
                        doRequest(makePayload(method, arguments), attempt + 1, std::move(cb));
                        return;
                    }
                    if (!header.isEmpty()) {
                        res.error = QStringLiteral("Session ID error.");
                        reply->deleteLater();
                        cb(res);
                        return;
                    }
                    // Без заголовка — обработаем ниже как HTTP-ошибку (тело/статус).
                }

                // 301: демон перенёс RPC (Location «/web/» → «/web/rpc»).
                if (res.httpCode == 301 && attempt == 1) {
                    const QString location = QString::fromUtf8(reply->rawHeader("Location")).trimmed();
                    if (location.startsWith('/') && !location.startsWith("//")) {
                        QString path;
                        if (location.endsWith("/web/"))
                            path = location.left(location.size() - 4) + "rpc";
                        else if (location.endsWith("/web"))
                            path = location.left(location.size() - 3) + "rpc";
                        if (!path.isEmpty()) {
                            m_rpcPath = path;
                            reply->deleteLater();
                            doRequest(makePayload(method, arguments), attempt + 1, std::move(cb));
                            return;
                        }
                    }
                }

                // Остальные HTTP-статусы/ошибки: сначала пробуем разобрать
                // тело (демон отвечает JSON даже при 401/500), иначе —
                // текст ошибки Qt при HTTP-ошибке.
                const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (!doc.isObject() || !doc.object().contains("result")) {
                    if (reply->error() != QNetworkReply::NoError)
                        res.error = res.httpCode == 0
                                       ? reply->errorString()
                                       : QStringLiteral("HTTP error: %1 — %2")
                                             .arg(res.httpCode)
                                             .arg(reply->errorString());
                    else
                        res.error = QStringLiteral("Invalid server response.");
                    reply->deleteLater();
                    cb(res);
                    return;
                }
                reply->deleteLater();

                res.body = doc.object();
                const QJsonValue resultValue = res.body.value("result");
                if (!res.body.contains("result") || !resultValue.isString()) {
                    res.error = QStringLiteral("Invalid server response.");
                    cb(res);
                    return;
                }
                res.status = resultValue.toString();
                if (res.status.compare("success", Qt::CaseInsensitive) != 0) {
                    const QString message = res.body.value("error").toString();
                    res.error = message.isEmpty() ? res.status : message;
                    cb(res);
                    return;
                }
                res.ok = true;
                res.arguments = res.body.value("arguments").toObject();
                cb(res);
            });
}

} // namespace tgui