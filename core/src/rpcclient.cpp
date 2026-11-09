#include "tgui/rpcclient.h"

#include <algorithm>

#include <QFile>

namespace tgui {

RpcClient::RpcClient(const Options& options, QObject* parent)
    : QObject(parent)
    , m_rpc(options.url, options.rpcPath)
    , m_options(options)
{
    if (!options.username.isEmpty())
        m_rpc.setUsernamePassword(options.username, options.password);
    if (options.ignoreSslErrors)
        m_rpc.setIgnoreSslErrors(true);
    if (options.timeoutMs > 0)
        m_rpc.setTimeout(std::chrono::milliseconds(options.timeoutMs));
}

QString RpcClient::describeError(const JsonRpcResult& r)
{
    if (!r.error.isEmpty())
        return r.error;
    if (r.status.isEmpty())
        return QStringLiteral("Invalid server response.");
    return r.status;
}

void RpcClient::disconnectFromDaemon()
{
    m_connected = false;
    m_rpcVersion = 0;
    m_sessionInfo = SessionInfo{};
}

void RpcClient::connectToDaemon()
{
    disconnectFromDaemon();
    m_rpc.call("session-get", QJsonObject{}, [this](const JsonRpcResult& r) {
        if (!r.ok) {
            emit connectionFailed(describeError(r));
            return;
        }
        if (!parseSessionInfo(r.arguments, m_sessionInfo)) {
            emit connectionFailed(QStringLiteral("Invalid server response."));
            return;
        }
        m_rpcVersion = m_sessionInfo.rpcVersion;
        m_connected = true;

        auto finish = [this] { emit connected(m_sessionInfo); };
        // При rpc >= 15 → free-space для download-dir.
        if (m_rpcVersion >= 15 && !m_sessionInfo.downloadDir.isEmpty()) {
            m_rpc.call("free-space", QJsonObject{{"path", m_sessionInfo.downloadDir}},
                       [this, finish](const JsonRpcResult& fs) {
                           if (fs.ok)
                               m_sessionInfo.downloadDirFreeSpace = static_cast<qint64>(
                                   fs.arguments.value("size-bytes").toDouble());
                           finish();
                       });
        } else {
            finish();
        }
    });
}

void RpcClient::request(const QString& method, const QJsonObject& arguments)
{
    m_rpc.call(method, arguments, [this, method](const JsonRpcResult& r) {
        if (!r.ok)
            emit readError(method, describeError(r));
    });
}

void RpcClient::requestReport(const QString& method, const QJsonObject& arguments)
{
    m_rpc.call(method, arguments, [this, method](const JsonRpcResult& r) {
        emit requestCompleted(method, r.ok, r.ok ? QString() : describeError(r), r.arguments);
    });
}

void RpcClient::torrentIds(QJsonObject& args, int id) const
{
    QJsonArray ids;
    ids.append(id);
    args.insert("ids", ids);
}

namespace {

QJsonArray toJsonArray(const QStringList& list)
{
    QJsonArray array;
    for (const QString& s : list)
        array.append(s);
    return array;
}

// Убрать поле из списка, если версия RPC ниже нужной.
void removeFieldIfUnsupported(QStringList& fields, const QString& field,
                              int currentVersion, int minVersion)
{
    if (currentVersion < minVersion)
        fields.removeAll(field);
}

} // namespace

void RpcClient::getTorrents(const QStringList& extraFields)
{
    // Базовый список полей torrent-get.
    QStringList fields = {
        "id", "name", "status", "errorString", "announceResponse", "recheckProgress",
        "sizeWhenDone", "leftUntilDone", "rateDownload", "rateUpload", "trackerStats",
        "metadataPercentComplete", "sequential_download", "downloadDir",
        // Расширенные поля для списка и диалога «Свойства».
        "addedDate", "doneDate", "downloadedEver", "uploadedEver", "isPrivate",
        "isStalled", "queuePosition", "uploadRatio",
        // 26-колоночный грид (референс раскладки — см. WORKFLOW.md).
        "totalSize", "activityDate", "secondsSeeding", "bandwidthPriority",
        // Вкладка «General» (референс — tabGeneral): hash, чанки, комментарий,
        // magnet-ссылка, автор/дата создания, лимит пиров.
        "hashString", "comment", "creator", "dateCreated", "pieceCount",
        "pieceSize", "haveValid", "magnetLink", "maxConnectedPeers",
    };
    fields += extraFields;
    removeFieldIfUnsupported(fields, "labels", m_rpcVersion, 16);
    removeFieldIfUnsupported(fields, "sequential_download", m_rpcVersion, 18);
    fields.removeDuplicates();
    std::sort(fields.begin(), fields.end());

    QJsonObject args;
    args.insert("fields", toJsonArray(fields));
    if (m_rpcVersion >= 16)
        args.insert("format", "table");

    m_rpc.call("torrent-get", args, [this](const JsonRpcResult& r) {
        if (!r.ok) {
            emit readError("torrent-get", describeError(r));
            return;
        }
        const QJsonArray arr = r.arguments.value("torrents").toArray();
        QVector<Torrent> torrents;
        if (!arr.isEmpty() && arr.first().isArray())
            torrents = parseTorrentsTable(r.arguments); // format=table
        else
            torrents = parseTorrents(arr);
        emit torrentsReceived(torrents);
    });
}

void RpcClient::getTorrentDetails(int id)
{
    // torrent-get: файлы + приоритеты + wanted + downloadDir.
    QJsonObject args;
    torrentIds(args, id);
    args.insert("fields", toJsonArray({"id", "files", "priorities", "wanted", "downloadDir"}));
    if (m_rpcVersion >= 16)
        args.insert("format", "table");

    m_rpc.call("torrent-get", args, [this](const JsonRpcResult& r) {
        if (!r.ok) {
            emit readError("torrent-get", describeError(r));
            return;
        }
        const QJsonArray arr = r.arguments.value("torrents").toArray();
        QJsonObject torrent;
        if (!arr.isEmpty() && arr.first().isArray()) {
            if (arr.size() < 2) {
                emit readError("torrent-get", QStringLiteral("Torrent not found."));
                return;
            }
            // format=table: [0] — имена полей, [1] — строка значений.
            const QJsonArray fields = arr.first().toArray();
            const QJsonArray row = arr.at(1).toArray();
            for (int i = 0; i < fields.size() && i < row.size(); ++i)
                torrent.insert(fields.at(i).toString(), row.at(i));
        } else if (!arr.isEmpty() && arr.first().isObject()) {
            torrent = arr.first().toObject();
        }
        TorrentDetails details;
        details.id = torrent.value("id").toInt();
        details.downloadDir = torrent.value("downloadDir").toString();
        details.files = parseFiles(torrent);
        emit torrentDetailsReceived(details);
    });
}

void RpcClient::getPeers(int id)
{
    // torrent-get: пиры.
    QJsonObject args;
    torrentIds(args, id);
    args.insert("fields", toJsonArray({"peers"}));

    m_rpc.call("torrent-get", args, [this, id](const JsonRpcResult& r) {
        if (!r.ok) {
            emit readError("torrent-get", describeError(r));
            return;
        }
        const QJsonArray arr = r.arguments.value("torrents").toArray();
        QVector<Peer> peers;
        if (!arr.isEmpty() && arr.first().isObject())
            peers = parsePeers(arr.first().toObject().value("peers").toArray());
        emit peersReceived(id, peers);
    });
}

void RpcClient::getTrackers(int id)
{
    // torrent-get: трекеры.
    QJsonObject args;
    torrentIds(args, id);
    args.insert("fields",
                toJsonArray({"id", "trackers", "trackerStats", "nextAnnounceTime"}));

    m_rpc.call("torrent-get", args, [this, id](const JsonRpcResult& r) {
        if (!r.ok) {
            emit readError("torrent-get", describeError(r));
            return;
        }
        const QJsonArray arr = r.arguments.value("torrents").toArray();
        QVector<TrackerStat> trackers;
        if (!arr.isEmpty() && arr.first().isObject())
            trackers = parseTrackerStats(
                arr.first().toObject().value("trackerStats").toArray());
        emit trackersReceived(id, trackers);
    });
}

void RpcClient::getSessionStats()
{
    // session-stats.
    m_rpc.call("session-stats", QJsonObject{}, [this](const JsonRpcResult& r) {
        if (!r.ok) {
            emit readError("session-stats", describeError(r));
            return;
        }
        SessionStats stats;
        if (!parseSessionStats(r.arguments, stats)) {
            emit readError("session-stats", QStringLiteral("Invalid server response."));
            return;
        }
        emit sessionStatsReceived(stats);
    });
}

void RpcClient::getFreeSpace(const QString& path)
{
    m_rpc.call("free-space", QJsonObject{{"path", path}}, [this, path](const JsonRpcResult& r) {
        if (!r.ok) {
            emit readError("free-space", describeError(r));
            return;
        }
        emit freeSpaceReceived(path, r.arguments.value("size-bytes").toVariant().toLongLong());
    });
}

void RpcClient::portTest()
{
    requestReport("port-test", QJsonObject{});
}

void RpcClient::startTorrent(int id, bool force)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport(force ? "torrent-start-now" : "torrent-start", args);
}

void RpcClient::stopTorrent(int id)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport("torrent-stop", args);
}

void RpcClient::startAll(bool force)
{
    // Без ids → все торренты.
    requestReport(force ? "torrent-start-now" : "torrent-start", QJsonObject{});
}

void RpcClient::stopAll()
{
    requestReport("torrent-stop", QJsonObject{});
}

void RpcClient::removeTorrent(int id, bool deleteLocalData)
{
    QJsonObject args;
    torrentIds(args, id);
    if (deleteLocalData)
        args.insert("delete-local-data", true);
    requestReport("torrent-remove", args);
}

void RpcClient::reannounce(int id)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport("torrent-reannounce", args);
}

void RpcClient::verify(int id)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport("torrent-verify", args);
}

void RpcClient::queueMoveTop(int id)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport("queue-move-top", args);
}

void RpcClient::queueMoveUp(int id)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport("queue-move-up", args);
}

void RpcClient::queueMoveDown(int id)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport("queue-move-down", args);
}

void RpcClient::setStarted(int id, bool started)
{
    QJsonObject args;
    torrentIds(args, id);
    args.insert("started", started);
    requestReport("torrent-set", args);
}

void RpcClient::setTorrent(int id, const QJsonObject& params)
{
    QJsonObject args = params;
    if (!args.contains("ids"))
        torrentIds(args, id);
    requestReport("torrent-set", args);
}

void RpcClient::setLabels(int id, const QStringList& labels)
{
    // labels доступны с RPC 16 (Transmission 3.0+).
    if (!supports(16)) {
        emit requestCompleted("torrent-set", false,
                              QStringLiteral("labels require Transmission RPC version 16 or newer"),
                              QJsonObject{});
        return;
    }
    QJsonObject args;
    torrentIds(args, id);
    args.insert("labels", toJsonArray(labels));
    requestReport("torrent-set", args);
}

void RpcClient::setPriority(int id, int priority)
{
    QJsonObject args;
    torrentIds(args, id);
    args.insert("bandwidthpriority", priority);
    requestReport("torrent-set", args);
}

void RpcClient::setFilesWanted(int id, const QVector<int>& wanted, const QVector<int>& unwanted)
{
    // files-wanted + files-unwanted (индексы файлов).
    QJsonObject args;
    torrentIds(args, id);
    QJsonArray w;
    for (int index : wanted)
        w.append(index);
    QJsonArray u;
    for (int index : unwanted)
        u.append(index);
    args.insert("files-wanted", w);
    args.insert("files-unwanted", u);
    requestReport("torrent-set", args);
}

void RpcClient::addTorrent(const AddTorrent& params)
{
    // paused + peer-limit + sequential_download (>= 18).
    QJsonObject args;
    switch (params.source) {
        case AddTorrent::Source::File: {
            // Файл доступен локально → читаем метаданные и шлём base64:
            // удалённому демону путь клиента недоступен («unrecognized info»).
            QFile f(params.file);
            if (f.open(QIODevice::ReadOnly)) {
                args.insert("metainfo", QString::fromLatin1(f.readAll().toBase64()));
            } else {
                // Файла локально нет — пусть демон попробует свой путь.
                args.insert("filename", params.file);
            }
            break;
        }
        case AddTorrent::Source::Base64:
            args.insert("metainfo", params.metainfoBase64);
            break;
        case AddTorrent::Source::Url:
            // magnet/URL добавляются полем "filename".
            args.insert("filename", params.url);
            break;
    }
    if (!params.downloadDir.isEmpty())
        args.insert("path", params.downloadDir);
    args.insert("paused", !params.start);
    if (params.peerLimit > 0)
        args.insert("peer-limit", params.peerLimit);
    if (params.sequential && supports(18))
        args.insert("sequential_download", true);
    requestReport("torrent-add", args);
}

void RpcClient::setSession(const QJsonObject& params)
{
    requestReport("session-set", params);
}

void RpcClient::setAltSpeed(bool enabled, int downKb, int upKb)
{
    QJsonObject args;
    args.insert("alt-speed-enabled", enabled);
    if (downKb > 0)
        args.insert("alt-speed-down", downKb);
    if (upKb > 0)
        args.insert("alt-speed-up", upKb);
    requestReport("session-set", args);
}

void RpcClient::setBlocklist(bool enabled, const QString& url)
{
    QJsonObject args;
    args.insert("blocklist-enabled", enabled);
    if (!url.isEmpty())
        args.insert("blocklist-url", url);
    requestReport("session-set", args);
}

void RpcClient::updateBlocklist()
{
    requestReport("blocklist-update", QJsonObject{});
}

} // namespace tgui