#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPointer>
#include <QUrl>
#include <QTcpServer>

class QTcpSocket;

// Мок HTTP/1.1-сервер, имитирующий RPC-демон Transmission (для тестов).
//
// Возможности:
//  - очередь готовых ответов (FIFO) + дефолтный ответ;
//  - требовать x-transmission-session-id (иначе 409 + заголовок с id);
//  - требовать заголовок Authorization (иначе 401);
//  - запись всех принятых запросов (заголовки, тело, разобранный JSON) —
//    для проверок метода/параметров RPC.
class MockRpcServer : public QTcpServer {
    Q_OBJECT
public:
    struct RecordedRequest {
        QString path;
        QMap<QString, QByteArray> headers; // ключи в нижнем регистре
        QByteArray body;
        QJsonObject json; // тело, если валидный JSON-объект
        QString rpcMethod; // json["method"]
        QJsonObject arguments; // json["arguments"]
    };

    struct Response {
        int code = 200;
        QByteArray body = "{\"result\": \"success\", \"arguments\": {}}";
        QMap<QByteArray, QByteArray> headers; // например x-transmission-session-id
        int delayMs = 0;
    };

    explicit MockRpcServer(QObject* parent = nullptr);

    // Привязать к 127.0.0.1 на свободном порту. Возвращает порт (0 — ошибка).
    int start();
    QUrl url() const;

    void enqueue(Response r);
    void setDefault(Response r);

    // Ручное управление задержанным ответом (TST-02) — детерминированная
    // альтернатива delayMs для флаких тестов:
    // holdNext() — следующий ответ (из очереди или по умолчанию) не
    // отправляется, а удерживается; release() — все удержанные ответы
    // отправляются (пока живы их сокеты).
    void holdNext();
    void release();
    // Сколько ответов сейчас удерживается (ответы по delayMs не учитываются).
    int heldCount() const;

    // Если задан: запрос без такого x-transmission-session-id → 409 + заголовок.
    void setExpectedSessionId(const QByteArray& id);
    // Если задан: запрос без такого заголовка Authorization → 401.
    void requireAuthorization(const QByteArray& value);

    QList<RecordedRequest> requests() const;
    int requestCount() const;

private slots:
    void onNewConnection();

private:
    void handleSocket(QTcpSocket* socket);
    void dispatch(QTcpSocket* socket, const QMap<QString, QByteArray>& headers,
                  const QByteArray& body, const QByteArray& path);
    void sendResponse(QTcpSocket* socket, int code, const QByteArray& body,
                      const QMap<QByteArray, QByteArray>& extraHeaders);

    // Удержанный ответ: отправится при release().
    struct HeldResponse {
        QPointer<QTcpSocket> socket;
        int code = 0;
        QByteArray body;
        QMap<QByteArray, QByteArray> headers;
    };

    mutable QMutex m_mutex;
    QList<Response> m_queue;
    bool m_hasDefault = false;
    Response m_default;
    bool m_holdArmed = false; // holdNext(): следующий ответ не отправлять
    QList<HeldResponse> m_held;
    QList<RecordedRequest> m_requests;
    QByteArray m_expectedSessionId;
    QByteArray m_requiredAuthorization;
};
