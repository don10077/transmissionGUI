#include "mockhttpserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

MockRpcServer::MockRpcServer(QObject* parent)
    : QTcpServer(parent)
{
    connect(this, &QTcpServer::newConnection, this, &MockRpcServer::onNewConnection);
}

int MockRpcServer::start()
{
    if (!listen(QHostAddress::LocalHost, 0))
        return 0;
    return serverPort();
}

QUrl MockRpcServer::url() const
{
    return QUrl(QString("http://127.0.0.1:%1").arg(serverPort()));
}

void MockRpcServer::enqueue(Response r)
{
    QMutexLocker locker(&m_mutex);
    m_queue.append(r);
}

void MockRpcServer::setDefault(Response r)
{
    QMutexLocker locker(&m_mutex);
    m_default = r;
    m_hasDefault = true;
}

void MockRpcServer::setExpectedSessionId(const QByteArray& id)
{
    QMutexLocker locker(&m_mutex);
    m_expectedSessionId = id;
}

void MockRpcServer::requireAuthorization(const QByteArray& value)
{
    QMutexLocker locker(&m_mutex);
    m_requiredAuthorization = value;
}

QList<MockRpcServer::RecordedRequest> MockRpcServer::requests() const
{
    QMutexLocker locker(&m_mutex);
    return m_requests;
}

int MockRpcServer::requestCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_requests.size();
}

void MockRpcServer::onNewConnection()
{
    while (QTcpSocket* socket = nextPendingConnection()) {
        if (socket == nullptr)
            continue;
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket] { handleSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void MockRpcServer::handleSocket(QTcpSocket* socket)
{
    if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState)
        return;

    QByteArray buffer = socket->property("buffer").toByteArray();
    buffer.append(socket->readAll());

    while (true) {
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            socket->setProperty("buffer", buffer);
            return;
        }

        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        if (lines.isEmpty() || !lines.first().startsWith("POST ")) {
            socket->setProperty("buffer", buffer);
            return;
        }

        QMap<QString, QByteArray> headers;
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            if (line.isEmpty())
                continue;
            const int sep = line.indexOf(':');
            if (sep <= 0)
                continue;
            headers.insert(line.left(sep).trimmed().toLower(), line.mid(sep + 1).trimmed());
        }

        const int contentLength = headers.value("content-length").toInt();
        const int needed = headerEnd + 4 + contentLength;
        if (buffer.size() < needed) {
            socket->setProperty("buffer", buffer);
            return;
        }
        const QByteArray body = buffer.mid(headerEnd + 4, contentLength);
        buffer.remove(0, needed);
        socket->setProperty("buffer", buffer);
        // Путь — из request line: "POST /transmission/rpc HTTP/1.1".
        const QList<QByteArray> requestParts = lines.first().split(' ');
        const QByteArray path = requestParts.size() >= 2 ? requestParts.at(1) : QByteArray();
        dispatch(socket, headers, body, path);
    }
}

void MockRpcServer::dispatch(QTcpSocket* socket,
                             const QMap<QString, QByteArray>& headers, const QByteArray& body,
                              const QByteArray& path)
{
    RecordedRequest rec;
    rec.path = path;
    rec.headers = headers;
    rec.body = body;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        rec.json = doc.object();
        rec.rpcMethod = rec.json.value("method").toString();
        rec.arguments = rec.json.value("arguments").toObject();
    }
    {
        QMutexLocker locker(&m_mutex);
        m_requests.append(rec);
    }

    QByteArray expectedSessionId;
    QByteArray requiredAuth;
    {
        QMutexLocker locker(&m_mutex);
        expectedSessionId = m_expectedSessionId;
        requiredAuth = m_requiredAuthorization;
    }

    if (!requiredAuth.isEmpty() && headers.value("authorization") != requiredAuth) {
        sendResponse(socket, 401,
                     "{\"result\": \"failure\", \"error\": \"unauthorized\"}", {});
        return;
    }
    if (!expectedSessionId.isEmpty() &&
        headers.value("x-transmission-session-id") != expectedSessionId) {
        QMap<QByteArray, QByteArray> h;
        h.insert("x-transmission-session-id", expectedSessionId);
        sendResponse(socket, 409, "{\"result\": \"failure\", \"error\": \"409 Conflict\"}", h);
        return;
    }

    Response response;
    bool have = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_queue.isEmpty()) {
            response = m_queue.takeFirst();
            have = true;
        } else if (m_hasDefault) {
            response = m_default;
            have = true;
        }
    }
    if (!have)
        response = Response{500, "{\"result\": \"failure\", \"error\": \"no response queued\"}", {}};

    sendResponse(socket, response.code, response.body, response.headers);
}

void MockRpcServer::sendResponse(QTcpSocket* socket, int code, const QByteArray& body,
                                 const QMap<QByteArray, QByteArray>& extraHeaders)
{
    if (socket == nullptr)
        return;

    const char* reason = "OK";
    if (code == 401)
        reason = "Unauthorized";
    else if (code == 409)
        reason = "Conflict";
    else if (code == 404)
        reason = "Not Found";

    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    for (auto it = extraHeaders.constBegin(); it != extraHeaders.constEnd(); ++it)
        out += it.key() + ": " + it.value() + "\r\n";
    out += "Content-Type: application/json\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Connection: keep-alive\r\n";
    out += "\r\n";
    out += body;
    socket->write(out);
}