#include "externalopen.h"
#include <QRegularExpression>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr int maxFrameSize = 1024 * 1024;
constexpr int timeoutMs = 2500;
}

/** Идентификатор magnet для отсева повторов с другими dn/tr. */
QString magnetIdentifier(const QUrl& url)
{
    if (url.scheme() != QLatin1String("magnet"))
        return {};
    static const QRegularExpression identifier(
        QStringLiteral("^(urn:btih:(?:[a-f0-9]{40}|[a-z2-7]{32})|urn:btmh:1220[a-f0-9]{64})$"),
        QRegularExpression::CaseInsensitiveOption);
    for (const auto& value : QUrlQuery(url).allQueryItemValues("xt", QUrl::FullyDecoded))
        if (identifier.match(value).hasMatch())
            return value.toLower();
    return {};
}

QString torrentLinkInput(const QString& input)
{
    const QUrl url(input.trimmed(), QUrl::StrictMode);
    if (!url.isValid())
        return {};
    if (url.scheme() == "magnet") {
        const auto identifiers = QUrlQuery(url).allQueryItemValues("xt");
        for (const auto& identifier : identifiers)
            if ((identifier.startsWith("urn:btih:", Qt::CaseInsensitive) ||
                 identifier.startsWith("urn:btmh:", Qt::CaseInsensitive)) &&
                identifier.size() > 9)
                return url.toString(QUrl::FullyEncoded);
        return {};
    }
    if ((url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty())
        return url.toString(QUrl::FullyEncoded);
    return {};
}

QString externalTorrentInput(const QString& input)
{
    if (input.startsWith("magnet:", Qt::CaseInsensitive))
        return torrentLinkInput(input);
    auto path = input;
    if (input.startsWith("file:", Qt::CaseInsensitive))
        path = QUrl(input).toLocalFile();
    if (!path.endsWith(".torrent", Qt::CaseInsensitive) || path.contains("://"))
        return {};
    return QFileInfo(path).absoluteFilePath();
}

ExternalOpen::ExternalOpen(const QString& dataDirectory)
    : m_directory(dataDirectory)
    , m_name("transmissiongui-open-" + QString::fromLatin1(
          QCryptographicHash::hash(dataDirectory.toUtf8(), QCryptographicHash::Sha256).toHex().left(24)))
    , m_lock(QDir(dataDirectory).filePath("external-open.lock"))
{
    m_lock.setStaleLockTime(0);
#ifdef Q_OS_UNIX
    m_name = QDir(m_directory).filePath("open-ipc/socket");
#endif
}

ExternalOpen::Result ExternalOpen::listen(Handler handler)
{
    if (m_directory.isEmpty() || !QDir().mkpath(m_directory))
        return Result::Unavailable;
    if (!m_lock.tryLock())
        return m_lock.error() == QLockFile::LockFailedError ? Result::AlreadyRunning : Result::Unavailable;

#ifdef Q_OS_UNIX
    // Доступ ограничен каталогом даже на системах, где режим сокета не учитывается.
    const auto socketDirectory = QFileInfo(m_name).absolutePath();
    if (!QDir().mkpath(socketDirectory) ||
        !QFile::setPermissions(socketDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ExeOwner)) {
        m_lock.unlock();
        return Result::Unavailable;
    }
#endif
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(m_name);
    if (!m_server.listen(m_name)) {
        m_lock.unlock();
        return Result::Unavailable;
    }
    QObject::connect(&m_server, &QLocalServer::newConnection, &m_server, [this, handler = std::move(handler)] {
        while (auto* socket = m_server.nextPendingConnection()) {
            socket->setReadBufferSize(maxFrameSize + 1);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(timeoutMs, socket, [socket] { socket->abort(); socket->deleteLater(); });
            const auto read = [this, socket, handler] {
                if (socket->property("requestAccepted").toBool())
                    return;
                if (!socket->canReadLine()) {
                    if (socket->bytesAvailable() >= maxFrameSize)
                        socket->abort();
                    return;
                }
                const auto frame = socket->readLine(maxFrameSize + 1);
                const auto json = QJsonDocument::fromJson(frame);
                if (frame.size() > maxFrameSize || !frame.endsWith('\n') ||
                    (!json.isArray() && !json.isObject())) {
                    socket->abort();
                    return;
                }
                const auto object = json.object();
                const bool activate = json.isObject() && object.value("activate").toBool();
                if (json.isObject() && (!activate || !object.value("inputs").isArray())) {
                    socket->abort();
                    return;
                }
                QStringList inputs;
                const auto items = json.isArray() ? json.array() : object.value("inputs").toArray();
                for (const auto& item : items) {
                    if (!item.isString()) {
                        socket->abort();
                        return;
                    }
                    const auto input = externalTorrentInput(item.toString());
                    if (!input.isEmpty())
                        inputs.append(input);
                }
                if (inputs.isEmpty() && (!activate || !items.isEmpty())) {
                    socket->abort();
                    return;
                }
                socket->setProperty("requestAccepted", true);
                socket->write("OK\n");
                socket->flush();
                socket->disconnectFromServer();
                QMetaObject::invokeMethod(&m_server, [handler, inputs] { handler(inputs); },
                                          Qt::QueuedConnection);
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket, read);
            read();
        }
    });
    return Result::Listening;
}

bool ExternalOpen::forward(const QStringList& inputs) const
{
    const auto payload = inputs.isEmpty()
        ? QJsonDocument(QJsonObject{{"activate", true}, {"inputs", QJsonArray{}}})
        : QJsonDocument(QJsonArray::fromStringList(inputs));
    auto frame = payload.toJson(QJsonDocument::Compact);
    frame.append('\n');
    if (frame.size() > maxFrameSize)
        return false;

    QLocalSocket socket;
    QElapsedTimer timer;
    timer.start();
    do {
        socket.abort();
        socket.connectToServer(m_name);
        if (socket.waitForConnected(100))
            break;
        QThread::msleep(25);
    } while (timer.elapsed() < timeoutMs);
    if (socket.state() != QLocalSocket::ConnectedState || socket.write(frame) != frame.size())
        return false;
    while (socket.bytesToWrite() > 0)
        if (!socket.waitForBytesWritten(timeoutMs))
            return false;
    while (!socket.canReadLine()) {
        const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
        if (remaining <= 0 || !socket.waitForReadyRead(remaining))
            return false;
    }
    return socket.readLine() == "OK\n";
}
