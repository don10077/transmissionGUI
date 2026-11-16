#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSslCertificate>
#include <QSslKey>
#include <QString>
#include <QUrl>

namespace tgui {

constexpr const char* kDefaultRpcPath = "/transmission/rpc";

// Результат JSON-RPC вызова к Transmission.
struct JsonRpcResult {
    bool ok = false;
    int httpCode = 0; // 0 — соединения не было (сеть/TLS/таймаут)
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    QString status;   // legacy result или сообщение ошибки JSON-RPC 2.0
    QString error;    // человекочитаемая ошибка (транспортная или RPC), пусто при ok
    QJsonObject body; // разобранный ответ
    QJsonObject arguments; // результат в именах полей, используемых моделями
    std::optional<int> errorCode;
};

// Низкоуровневый JSON-RPC транспорт демона Transmission:
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
    void setClientIdentity(const QList<QSslCertificate>& chain, const QSslKey& key);

    // HTTP/SOCKS5-прокси (QNetworkProxy::NoProxy — выключить).
    void setProxy(const QNetworkProxy& proxy);

    void setTimeout(std::chrono::milliseconds timeout);

    // Асинхронный вызов метода. Пустой arguments — без ключа "arguments".
    void call(const QString& method, const QJsonObject& arguments, Callback cb);

    // Синхронный вызов (блокирует локальный event loop до ответа или timeout).
    // Для тестов и dev-инструментов.
    JsonRpcResult callSync(const QString& method, const QJsonObject& arguments,
                           std::chrono::milliseconds timeout = std::chrono::seconds(10));

    const QUrl& url() const { return m_url; }
    QString rpcPath() const { return m_rpcPath; }
    QString sessionId() const { return m_sessionId; }

private:
    struct Request {
        QString method;
        QJsonObject arguments;
        qint64 id;
        bool modern;
        bool sessionRetried = false;
        bool redirected = false;
    };
    void doRequest(Request rpc, Callback cb);

    std::unique_ptr<QNetworkAccessManager> m_nam;
    QUrl m_url;
    QString m_rpcPath;
    QString m_user;
    QString m_password;
    QString m_sessionId; // x-transmission-session-id
    std::chrono::milliseconds m_timeout = std::chrono::seconds(30);
    bool m_ignoreSslErrors = false;
    QList<QSslCertificate> m_clientChain;
    QSslKey m_clientKey;
    bool m_modern = false;
    qint64 m_nextId = 0;
};

} // namespace tgui
