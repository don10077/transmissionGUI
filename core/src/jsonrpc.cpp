#include "tgui/jsonrpc.h"
#include "rpcdialect.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QVersionNumber>

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

void JsonRpcClient::setProxy(const QNetworkProxy& proxy)
{
    m_nam->setProxy(proxy);
}

void JsonRpcClient::setTimeout(std::chrono::milliseconds timeout)
{
    if (timeout.count() > 0)
        m_timeout = timeout;
}

void JsonRpcClient::call(const QString& method, const QJsonObject& arguments, Callback cb)
{
    doRequest({method, arguments, ++m_nextId, m_modern}, std::move(cb));
}

JsonRpcResult JsonRpcClient::callSync(const QString& method, const QJsonObject& arguments,
                                      std::chrono::milliseconds timeout)
{
    JsonRpcResult out;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    call(method, arguments, [loop = QPointer<QEventLoop>(&loop), &out](const JsonRpcResult& r) {
        if (!loop)
            return;
        out = r;
        loop->quit();
    });
    timer.start(timeout);
    loop.exec();
    return out;
}

namespace {

/** Проверяет объявленную сервером версию RPC, начиная с Tr5 (6.0.0). */
bool supportsJsonRpc(const QString& version)
{
    return QVersionNumber::fromString(version).majorVersion() >= 6;
}

/** Разбирает ответ JSON-RPC 2.0, сохраняя оригинальный объект в body. */
void parseModern(JsonRpcResult& result, const QString& method, qint64 id)
{
    const auto& body = result.body;
    if (body.value("jsonrpc").toString() != QLatin1String("2.0") ||
        body.value("id").toInteger(-1) != id ||
        body.contains("error") == body.contains("result")) {
        result.error = QStringLiteral("Invalid server response.");
        return;
    }
    if (body.value("error").isObject()) {
        const auto error = body.value("error").toObject();
        const auto data = error.value("data").toObject();
        if (!error.value("code").isDouble() || !error.value("message").isString()) {
            result.error = QStringLiteral("Invalid server response.");
            return;
        }
        result.errorCode = error.value("code").toInt();
        result.status = error.value("message").toString();
        result.error = data.value("error_string").toString(result.status);
        result.arguments = rpcdialect::legacyResult(method, data.value("result").toObject());
        return;
    }
    if (!body.value("result").isObject() || result.httpCode != 200) {
        result.error = QStringLiteral("Invalid server response.");
        return;
    }
    result.ok = true;
    result.status = QStringLiteral("success");
    result.arguments = rpcdialect::legacyResult(method, body.value("result").toObject());
}

} // namespace

void JsonRpcClient::doRequest(Request rpc, Callback cb)
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

    QJsonObject payload;
    if (rpc.modern) {
        payload.insert("jsonrpc", QStringLiteral("2.0"));
        payload.insert("id", rpc.id);
        payload.insert("method", rpcdialect::modernKey(rpc.method));
        if (!rpc.arguments.isEmpty())
            payload.insert("params", rpcdialect::modernParams(rpc.method, rpc.arguments));
    } else {
        payload.insert("method", rpc.method);
        if (!rpc.arguments.isEmpty())
            payload.insert("arguments", rpcdialect::legacyParams(rpc.method, rpc.arguments));
    }

    QNetworkReply* reply =
        m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, reply,
            [this, reply, rpc = std::move(rpc), cb = std::move(cb)]() mutable {
                JsonRpcResult res;
                res.httpCode =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                // 409: нет/протух session id — взять новый из заголовка ответа
                // и повторить 1 раз; повторный 409 — "Session ID error.";
                // 409 без заголовка — обычная HTTP-ошибка.
                if (res.httpCode == 409) {
                    const QByteArray header = reply->rawHeader("x-transmission-session-id");
                    if (!header.isEmpty() && !rpc.sessionRetried) {
                        m_sessionId = QString::fromUtf8(header);
                        if (supportsJsonRpc(QString::fromLatin1(reply->rawHeader("X-Transmission-Rpc-Version"))))
                            m_modern = true;
                        rpc.modern = m_modern;
                        rpc.sessionRetried = true;
                        reply->deleteLater();
                        doRequest(std::move(rpc), std::move(cb));
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
                if (res.httpCode == 301 && !rpc.redirected) {
                    const QString location = QString::fromUtf8(reply->rawHeader("Location")).trimmed();
                    if (location.startsWith('/') && !location.startsWith("//")) {
                        QString path;
                        if (location.endsWith("/web/"))
                            path = location.left(location.size() - 4) + "rpc";
                        else if (location.endsWith("/web"))
                            path = location.left(location.size() - 3) + "rpc";
                        if (!path.isEmpty()) {
                            m_rpcPath = path;
                            rpc.redirected = true;
                            reply->deleteLater();
                            doRequest(std::move(rpc), std::move(cb));
                            return;
                        }
                    }
                }

                // Остальные HTTP-статусы/ошибки: сначала пробуем разобрать
                // тело (демон отвечает JSON даже при 401/500), иначе —
                // текст ошибки Qt при HTTP-ошибке.
                const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                res.body = doc.object();
                const bool modernResponse = res.body.value("jsonrpc").toString() == QLatin1String("2.0");
                if (!rpc.modern && rpc.method == QLatin1String("session-get") && res.httpCode == 200 &&
                    modernResponse && res.body.value("error").toObject().value("code").toInt() == -32600) {
                    // Через прокси заголовок версии может отсутствовать. Повторяем только чтение сессии.
                    rpc.modern = true;
                    reply->deleteLater();
                    doRequest(std::move(rpc), std::move(cb));
                    return;
                }
                if (rpc.modern && modernResponse) {
                    reply->deleteLater();
                    parseModern(res, rpc.method, rpc.id);
                    if (res.ok)
                        m_modern = true;
                    cb(res);
                    return;
                }
                if (!doc.isObject() || (rpc.modern && res.httpCode == 200) || !res.body.contains("result")) {
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
                if (rpc.method == QLatin1String("session-get") &&
                    supportsJsonRpc(res.arguments.value("rpc-version-semver").toString()))
                    m_modern = true;
                cb(res);
            });
}

} // namespace tgui
