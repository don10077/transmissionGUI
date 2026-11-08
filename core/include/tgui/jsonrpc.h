#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>

namespace tgui {

constexpr const char* kDefaultRpcPath = "/transmission/rpc";

// Результат JSON-RPC вызова к Transmission.
struct JsonRpcResult {
    bool ok = false; // HTTP 200 + валидный JSON + result == "success"
    int httpCode = 0; // 0 — соединения не было (сеть/TLS/таймаут)
    QString status;   // "result" из ответа ("success" / "failure")
    QString error;    // человекочитаемая ошибка (транспортная или RPC), пусто при ok
    QJsonObject body; // разобранный ответ
    QJsonObject arguments; // body["arguments"], когда ok
};

// Низкоуровневый JSON-RPC транспорт (аналог TRpc.SendRequest из transgui):
// POST application/json на url+rpcPath, заголовок x-transmission-session-id
// (409 → новый id из ответа, повтор; макс. 2 попытки), HTTP Basic auth,
// перенос rpcPath при 301 (Transmission 4.x: "/web/" → "/web/rpc").
//
// Клиент асинхронный (QNetworkAccessManager) и должен жить в потоке с
// event loop; callback вызывается в этом же потоке.
class JsonRpcClient {
public:
    using Callback = std::function<void(JsonRpcResult)>;

    explicit JsonRpcClient(const QUrl& url, const QString& rpcPath = QString());

    // HTTP Basic auth (не задан — без заголовка).
    void setUsernamePassword(const QString& user, const QString& password = QString());

    // Игнорировать ошибки TLS-сертификата (self-signed).
    void setIgnoreSslErrors(bool ignore);

    void setTimeout(std::chrono::milliseconds timeout);

    // Асинхронный вызов метода. Пустой arguments — без ключа "arguments"
    // (как в transgui).
    void call(const QString& method, const QJsonObject& arguments, Callback cb);

    // Синхронный вызов (блокирует локальный event loop до ответа или timeout).
    // Для тестов и dev-инструментов.
    JsonRpcResult callSync(const QString& method, const QJsonObject& arguments,
                           std::chrono::milliseconds timeout = std::chrono::seconds(10));

    const QUrl& url() const { return m_url; }
    QString rpcPath() const { return m_rpcPath; }
    QString sessionId() const { return m_sessionId; }

private:
    void doRequest(const QJsonObject& payload, int attempt, Callback cb);

    std::unique_ptr<QNetworkAccessManager> m_nam;
    QUrl m_url;
    QString m_rpcPath;
    QString m_user;
    QString m_password;
    QString m_sessionId; // x-transmission-session-id
    std::chrono::milliseconds m_timeout = std::chrono::seconds(30);
    bool m_ignoreSslErrors = false;
};

} // namespace tgui