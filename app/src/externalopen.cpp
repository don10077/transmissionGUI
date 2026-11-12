#include "externalopen.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr int maxFrameSize = 1024 * 1024;
constexpr int timeoutMs = 2500;
}

QString externalTorrentInput(const QString& input)
{
    if (input.startsWith("magnet:", Qt::CaseInsensitive)) {
        const QUrl url(input, QUrl::StrictMode);
        const auto identifiers = QUrlQuery(url).allQueryItemValues("xt");
        for (const auto& identifier : identifiers)
            if (url.isValid() && (identifier.startsWith("urn:btih:", Qt::CaseInsensitive) ||
                                 identifier.startsWith("urn:btmh:", Qt::CaseInsensitive)) &&
                identifier.size() > 9)
                return url.toString(QUrl::FullyEncoded);
        return {};
    }
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
            const auto read = [socket, handler] {
                if (!socket->canReadLine()) {
                    if (socket->bytesAvailable() >= maxFrameSize)
                        socket->abort();
                    return;
                }
                const auto frame = socket->readLine(maxFrameSize + 1);
                const auto json = QJsonDocument::fromJson(frame);
                if (frame.size() > maxFrameSize || !frame.endsWith('\n') || !json.isArray()) {
                    socket->abort();
                    return;
                }
                QStringList inputs;
                for (const auto& item : json.array()) {
                    if (!item.isString()) {
                        socket->abort();
                        return;
                    }
                    const auto input = externalTorrentInput(item.toString());
                    if (!input.isEmpty())
                        inputs.append(input);
                }
                if (inputs.isEmpty()) {
                    socket->abort();
                    return;
                }
                handler(inputs);
                socket->write("OK\n");
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket, read);
            read();
        }
    });
    return Result::Listening;
}

bool ExternalOpen::forward(const QStringList& inputs) const
{
    auto frame = QJsonDocument(QJsonArray::fromStringList(inputs)).toJson(QJsonDocument::Compact);
    frame.append('\n');
    if (inputs.isEmpty() || frame.size() > maxFrameSize)
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
