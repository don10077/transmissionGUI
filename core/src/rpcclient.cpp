#include "tgui/rpcclient.h"
#include "tgui/bencode.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QFile>
#include <QUrlQuery>

namespace tgui {

namespace {
/** Отделяет временные сетевые ошибки от неверных настроек RPC и ошибок TLS. */
bool retryableConnectionError(const JsonRpcResult& result)
{
    if (result.httpCode == 502 || result.httpCode == 503 || result.httpCode == 504)
        return true;
    switch (result.networkError) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: // Таймаут передачи в Qt.
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyTimeoutError:
        return true;
    default:
        return false;
    }
}
}

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
    m_rpc.setProxy(options.proxy);
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

/** Загружает клиентские учётные данные перед отправкой первого HTTPS-запроса. */
bool RpcClient::configureClientTls()
{
    const auto& certPath = m_options.clientCertificatePath;
    const auto& keyPath = m_options.clientKeyPath;
    if (certPath.isEmpty() && keyPath.isEmpty())
        return true;
    const auto fail = [this](const QString& error) {
        emit connectionFailed(error);
        return false;
    };
    if (m_options.url.scheme() != QLatin1String("https") || m_options.ignoreSslErrors)
        return fail(tr("A client certificate requires HTTPS with server certificate verification."));

    const bool pkcs12 = certPath.endsWith(".p12", Qt::CaseInsensitive) ||
                        certPath.endsWith(".pfx", Qt::CaseInsensitive);
    if (certPath.isEmpty() || pkcs12 != keyPath.isEmpty())
        return fail(tr("Choose a certificate and private key, or a single PKCS#12 file."));

    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly))
        return fail(tr("Could not read the client certificate file: %1").arg(certPath));

    QList<QSslCertificate> chain;
    QSslKey key;
    const auto passphrase = m_options.clientKeyPassphrase.value_or(QByteArray());
    if (pkcs12) {
        QSslCertificate leaf;
        if (!QSslCertificate::importPkcs12(&certFile, &key, &leaf, &chain, passphrase) ||
            leaf.isNull() || key.isNull()) {
            if (!m_options.clientKeyPassphrase) {
                emit clientTlsPassphraseRequired();
                return false;
            }
            return fail(tr("Could not open the PKCS#12 file. Check the file and its passphrase."));
        }
        chain.prepend(leaf);
    } else {
        const auto certificates = certFile.readAll();
        chain = QSslCertificate::fromData(certificates, QSsl::Pem);
        if (chain.isEmpty())
            chain = QSslCertificate::fromData(certificates, QSsl::Der);
        if (chain.isEmpty())
            return fail(tr("The client certificate file contains no valid certificate."));

        QFile keyFile(keyPath);
        if (!keyFile.open(QIODevice::ReadOnly))
            return fail(tr("Could not read the client private key file: %1").arg(keyPath));
        const auto keyData = keyFile.readAll();
        if (!m_options.clientKeyPassphrase &&
            (keyData.contains("BEGIN ENCRYPTED PRIVATE KEY") || keyData.contains("Proc-Type: 4,ENCRYPTED"))) {
            emit clientTlsPassphraseRequired();
            return false;
        }
        for (const auto algorithm : {QSsl::Rsa, QSsl::Ec, QSsl::Dsa}) {
            key = QSslKey(keyData, algorithm, QSsl::Pem, QSsl::PrivateKey, passphrase);
            if (!key.isNull())
                break;
        }
        if (key.isNull())
            return fail(tr("Could not load the PEM private key. Check the file and its passphrase."));
    }
    m_rpc.setClientIdentity(chain, key);
    return true;
}

void RpcClient::connectToDaemon()
{
    disconnectFromDaemon();
    m_connectionProblemReported = false;
    if (!configureClientTls())
        return;
    call("session-get", QJsonObject{}, [this](const JsonRpcResult& r) {
        if (!r.ok) {
            if (!m_connectionProblemReported)
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
            call("free-space", QJsonObject{{"path", m_sessionInfo.downloadDir}},
                       [this, finish](const JsonRpcResult& fs) {
                           if (m_connectionProblemReported)
                               return;
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

/** Сообщает о сбое соединения один раз для всех параллельных запросов этого клиента. */
void RpcClient::call(const QString& method, const QJsonObject& arguments, JsonRpcClient::Callback callback)
{
    m_rpc.call(method, arguments, [this, callback = std::move(callback)](const JsonRpcResult& result) {
        if (!result.ok && !m_connectionProblemReported) {
            if (result.httpCode == 401) {
                m_connectionProblemReported = true;
                m_connected = false;
                emit authenticationFailed(describeError(result));
            } else if (retryableConnectionError(result)) {
                m_connectionProblemReported = true;
                m_connected = false;
                emit connectionInterrupted(describeError(result));
            } else if (m_connected && result.networkError == QNetworkReply::SslHandshakeFailedError) {
                m_connectionProblemReported = true;
                m_connected = false;
                emit connectionFailed(describeError(result));
            }
        }
        callback(result);
    });
}

void RpcClient::request(const QString& method, const QJsonObject& arguments)
{
    call(method, arguments, [this, method](const JsonRpcResult& r) {
        if (!r.ok && r.httpCode != 401)
            emit readError(method, describeError(r));
    });
}

void RpcClient::requestReport(const QString& method, const QJsonObject& arguments,
                              RequestCallback callback)
{
    call(method, arguments, [this, method, callback = std::move(callback)](const JsonRpcResult& r) {
        if (!r.ok && r.httpCode == 401 && !callback)
            return;
        reportResult(method, r, callback);
    });
}

/** Передаёт результат либо вызывающему обработчику, либо обычному сигналу RPC. */
void RpcClient::reportResult(const QString& method, const JsonRpcResult& result,
                             const RequestCallback& callback)
{
    if (callback)
        callback(result);
    else
        emit requestCompleted(method, result.ok, result.ok ? QString() : describeError(result),
                              result.arguments);
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
        "metadataPercentComplete", "sequential_download", "downloadDir", "labels",
        // Расширенные поля для списка и диалога «Свойства».
        "addedDate", "doneDate", "downloadedEver", "uploadedEver", "corruptEver",
        "isPrivate", "isStalled", "queuePosition", "uploadRatio",
        // 26-колоночный грид (референс раскладки — см. WORKFLOW.md).
        "totalSize", "activityDate", "secondsSeeding", "bandwidthPriority",
        // Вкладка «General» (референс — tabGeneral): hash, чанки, комментарий,
        // magnet-ссылка, автор/дата создания, лимит пиров.
        "hashString", "comment", "creator", "dateCreated", "pieceCount",
        "pieceSize", "haveValid", "pieces", "magnetLink", "maxConnectedPeers",
        "downloadLimited", "downloadLimit", "uploadLimited", "uploadLimit", "seedRatioMode", "seedRatioLimit", "seedIdleMode", "seedIdleLimit",
    };
    fields += extraFields;
    removeFieldIfUnsupported(fields, "labels", m_rpcVersion, 16);
    removeFieldIfUnsupported(fields, "sequential_download", m_rpcVersion, 18);
    removeFieldIfUnsupported(fields, "pieces", m_rpcVersion, 5);
    fields.removeDuplicates();
    std::sort(fields.begin(), fields.end());

    QJsonObject args;
    args.insert("fields", toJsonArray(fields));
    if (m_rpcVersion >= 16)
        args.insert("format", "table");

    const auto sequence = ++m_readSequence;
    call("torrent-get", args, [this, sequence](const JsonRpcResult& r) {
        if (sequence < m_listApplied)
            return;
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
        m_listApplied = sequence;
        emit torrentsReceived(torrents);
    });
}

void RpcClient::getTorrentDetails(int id, DetailsCallback callback)
{
    // torrent-get: файлы + приоритеты + wanted + downloadDir + состояние
    // проверки (recheckProgress) для прогресса вкладки «Файлы».
    QJsonObject args;
    torrentIds(args, id);
    args.insert("fields", toJsonArray({"id", "files", "priorities", "wanted", "downloadDir",
                                       "name", "maxConnectedPeers",
                                       "status", "recheckProgress", "pieceCount"}));
    if (m_rpcVersion >= 16)
        args.insert("format", "table");

    const auto sequence = ++m_readSequence;
    call("torrent-get", args, [this, id, callback = std::move(callback), sequence](const JsonRpcResult& r) {
        if (!callback && sequence < m_filesApplied)
            return;
        if (!r.ok) {
            emit readError("torrent-get", describeError(r));
            if (callback)
                callback(std::nullopt);
            return;
        }
        const QJsonArray arr = r.arguments.value("torrents").toArray();
        QJsonObject torrent;
        if (!arr.isEmpty() && arr.first().isArray()) {
            // format=table: [0] — имена полей, [1] — строка значений.
            const QJsonArray fields = arr.first().toArray();
            const QJsonArray row = arr.size() > 1 ? arr.at(1).toArray() : QJsonArray{};
            for (int i = 0; i < fields.size() && i < row.size(); ++i)
                torrent.insert(fields.at(i).toString(), row.at(i));
        } else if (!arr.isEmpty() && arr.first().isObject()) {
            torrent = arr.first().toObject();
        }
        if (torrent.value("id").toInt(-1) != id) {
            emit readError("torrent-get", QStringLiteral("Torrent not found."));
            if (callback)
                callback(std::nullopt);
            return;
        }
        TorrentDetails details;
        details.id = id;
        details.name = torrent.value("name").toString();
        details.peerLimit = torrent.value("maxConnectedPeers").toInt();
        details.downloadDir = torrent.value("downloadDir").toString();
        details.files = parseFiles(torrent);
        details.status = torrent.value("status").toInt(-1);
        details.pieceCount = torrent.value("pieceCount").toInt();
        if (torrent.value("recheckProgress").isDouble())
            details.recheckProgress = torrent.value("recheckProgress").toDouble();
        // При проверке демон не сбрасывает have-биты, поэтому bytesCompleted
        // уже скачанного торрента всё время проверки равен 100%. Реальный
        // прогресс — recheckProgress; раскладываем его по файлам.
        applyCheckProgress(details.files, details.status, details.recheckProgress,
                           details.pieceCount);
        if (callback)
            callback(details);
        else {
            m_filesApplied = sequence;
            emit torrentDetailsReceived(details);
        }
    });
}

void RpcClient::getPeers(int id)
{
    // torrent-get: пиры.
    QJsonObject args;
    torrentIds(args, id);
    args.insert("fields", toJsonArray({"peers"}));

    call("torrent-get", args, [this, id](const JsonRpcResult& r) {
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

    const auto sequence = ++m_readSequence;
    call("torrent-get", args, [this, id, sequence](const JsonRpcResult& r) {
        if (sequence < m_trackersApplied)
            return;
        if (!r.ok) {
            emit readError("torrent-get", describeError(r));
            return;
        }
        const QJsonArray arr = r.arguments.value("torrents").toArray();
        QVector<TrackerStat> trackers;
        if (!arr.isEmpty() && arr.first().isObject())
            trackers = parseTrackerStats(
                arr.first().toObject().value("trackerStats").toArray());
        m_trackersApplied = sequence;
        emit trackersReceived(id, trackers);
    });
}

void RpcClient::getSessionStats()
{
    // session-stats.
    call("session-stats", QJsonObject{}, [this](const JsonRpcResult& r) {
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
    call("free-space", QJsonObject{{"path", path}}, [this, path](const JsonRpcResult& r) {
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

void RpcClient::torrentAction(const QString& method, const QVector<int>& ids,
                              const QJsonObject& params, RequestCallback callback)
{
    if (ids.isEmpty())
        return;
    QJsonArray torrentIds;
    for (const int id : ids)
        torrentIds.append(id);
    auto args = params;
    args.insert("ids", torrentIds);
    requestReport(method, args, std::move(callback));
}

void RpcClient::startAll(bool force, RequestCallback callback)
{
    // Без ids → все торренты.
    requestReport(force ? "torrent-start-now" : "torrent-start", QJsonObject{}, std::move(callback));
}

void RpcClient::stopAll(RequestCallback callback)
{
    requestReport("torrent-stop", QJsonObject{}, std::move(callback));
}

void RpcClient::removeTorrent(int id, bool deleteLocalData, RequestCallback callback)
{
    QJsonObject args;
    torrentIds(args, id);
    if (deleteLocalData)
        args.insert("delete-local-data", true);
    requestReport("torrent-remove", args, std::move(callback));
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

void RpcClient::queueMoveBottom(int id)
{
    QJsonObject args;
    torrentIds(args, id);
    requestReport("queue-move-bottom", args);
}

void RpcClient::setStarted(int id, bool started)
{
    QJsonObject args;
    torrentIds(args, id);
    args.insert("started", started);
    requestReport("torrent-set", args);
}

void RpcClient::setSequential(int id, bool enabled)
{
    // sequential_download доступен с RPC 18 (Transmission 4.0+).
    if (!supports(18)) {
        emit requestCompleted("torrent-set", false,
                              QStringLiteral("sequential download requires Transmission RPC version 18 or newer"),
                              QJsonObject{});
        return;
    }
    QJsonObject args;
    torrentIds(args, id);
    args.insert("sequential_download", enabled);
    requestReport("torrent-set", args);
}

void RpcClient::renamePath(int id, const QString& path, const QString& newName, RequestCallback callback)
{
    // torrent-rename-path доступен с RPC 15 (Transmission 2.84+).
    if (!supports(15)) {
        JsonRpcResult result;
        result.error = QStringLiteral("rename path requires Transmission RPC version 15 or newer");
        reportResult("torrent-rename-path", result, callback);
        return;
    }
    QJsonObject args;
    torrentIds(args, id);
    args.insert("path", path);
    args.insert("name", newName);
    requestReport("torrent-rename-path", args, std::move(callback));
}

void RpcClient::setLocation(int id, const QString& path, bool moveData)
{
    setLocation(QVector<int>{id}, path, moveData);
}

void RpcClient::setLocation(const QVector<int>& ids, const QString& path, bool moveData,
                            RequestCallback callback)
{
    if (!supports(6)) {
        const QString error = QStringLiteral("RPC 6 or newer is required.");
        if (callback) {
            JsonRpcResult result;
            result.ok = false;
            result.error = error;
            callback(result);
        } else {
            emit requestCompleted("torrent-set-location", false, error, {});
        }
        return;
    }
    torrentAction("torrent-set-location", ids,
                  QJsonObject{{"location", path}, {"move", moveData}}, std::move(callback));
}

void RpcClient::setTorrent(int id, const QJsonObject& params)
{
    QJsonObject args = params;
    if (!args.contains("ids"))
        torrentIds(args, id);
    if (!params.contains("labels")) {
        requestReport("torrent-set", args);
        return;
    }
    requestReport("torrent-set", args, [this](const JsonRpcResult& result) {
        if (result.ok)
            getTorrents();
        emit requestCompleted("torrent-set", result.ok,
                              result.ok ? QString() : describeError(result), result.arguments);
    });
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
    setTorrent(id, {{"labels", toJsonArray(labels)}});
}

void RpcClient::renameLabel(const QString& from, const QString& to, RequestCallback callback)
{
    if (!supports(16)) {
        JsonRpcResult result;
        result.error = QStringLiteral("labels require Transmission RPC version 16 or newer");
        callback(result);
        return;
    }
    requestReport("torrent-get", {{"fields", QJsonArray{"id", "labels"}}},
        [this, from, to, callback = std::move(callback)](const JsonRpcResult& result) {
            const auto torrents = result.arguments.value("torrents");
            if (!result.ok || !torrents.isArray()) {
                auto failure = result;
                failure.ok = false;
                failure.error = describeError(result.ok ? JsonRpcResult{} : result);
                callback(failure);
                return;
            }
            QList<QPair<QString, QJsonObject>> requests;
            for (const auto& entry : torrents.toArray()) {
                const auto torrent = entry.toObject();
                const int id = torrent.value("id").toInt(-1);
                const auto labels = torrent.value("labels").toArray();
                if (id < 0 || !labels.contains(from))
                    continue;
                QStringList renamed;
                for (const auto& label : labels) {
                    const auto name = label.toString();
                    const auto replacement = name == from ? to : name;
                    if (!renamed.contains(replacement))
                        renamed.append(replacement);
                }
                requests.append({"torrent-set", {{"ids", QJsonArray{id}},
                                                 {"labels", toJsonArray(renamed)}}});
            }
            applyRequests(std::move(requests), [this, callback](const JsonRpcResult& update) {
                getTorrents();
                callback(update);
            });
        });
}

void RpcClient::setPriority(int id, int priority)
{
    // Ключ 'bandwidthPriority' (camelCase) — как в образце (SetTorrentPriority)
    // и в legacy-диалекте RPC, который мы говорим. Демон ищет ключ с учётом
    // регистра; lowercase-ключ он молча игнорирует, и приоритет не применялся.
    QJsonObject args;
    torrentIds(args, id);
    args.insert("bandwidthPriority", priority);
    requestReport("torrent-set", args);
}

void RpcClient::setFilesWanted(int id, const QVector<int>& wanted, const QVector<int>& unwanted,
                               RequestCallback callback)
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
    // Пустой массив в torrent-set означает «все файлы».
    if (!w.isEmpty())
        args.insert("files-wanted", w);
    if (!u.isEmpty())
        args.insert("files-unwanted", u);
    if (w.isEmpty() && u.isEmpty())
        return;
    requestReport("torrent-set", args, std::move(callback));
}

void RpcClient::setFilePriority(int id, const QVector<int>& files, int priority, RequestCallback callback)
{
    // Per-file приоритет (legacy-протокол): files-wanted + priority-{low|normal|high}
    // (массив индексов файлов). Гейт как в образце — RPC >= 5.
    if (!supports(5)) {
        JsonRpcResult result;
        result.error = QStringLiteral("file priority requires Transmission RPC version 5 or newer");
        reportResult("torrent-set", result, callback);
        return;
    }
    if (files.isEmpty())
        return;
    QJsonObject args;
    torrentIds(args, id);
    QJsonArray f;
    for (int index : files)
        f.append(index);
    args.insert("files-wanted", f);
    const QString key = priority < 0 ? QStringLiteral("priority-low")
                        : priority > 0 ? QStringLiteral("priority-high")
                                      : QStringLiteral("priority-normal");
    args.insert(key, f);
    requestReport("torrent-set", args, std::move(callback));
}

void RpcClient::addTracker(int id, const QString& url, RequestCallback callback)
{
    // Трекеры требуют RPC >= 10 (как в образце).
    if (!supports(10)) {
        JsonRpcResult result;
        result.error = QStringLiteral("tracker management requires Transmission RPC version 10 or newer");
        reportResult("torrent-set", result, callback);
        return;
    }
    if (url.trimmed().isEmpty())
        return;
    QJsonObject args;
    torrentIds(args, id);
    QJsonArray a;
    a.append(url);
    args.insert("trackerAdd", a);
    requestReport("torrent-set", args, std::move(callback));
}

void RpcClient::removeTracker(int id, int trackerId, RequestCallback callback)
{
    if (!supports(10)) {
        JsonRpcResult result;
        result.error = QStringLiteral("tracker management requires Transmission RPC version 10 or newer");
        reportResult("torrent-set", result, callback);
        return;
    }
    QJsonObject args;
    torrentIds(args, id);
    QJsonArray a;
    a.append(trackerId);
    args.insert("trackerRemove", a);
    requestReport("torrent-set", args, std::move(callback));
}

void RpcClient::replaceTracker(int id, int trackerId, const QString& newUrl, RequestCallback callback)
{
    if (!supports(10)) {
        JsonRpcResult result;
        result.error = QStringLiteral("tracker management requires Transmission RPC version 10 or newer");
        reportResult("torrent-set", result, callback);
        return;
    }
    if (newUrl.trimmed().isEmpty())
        return;
    QJsonObject args;
    torrentIds(args, id);
    QJsonArray a;
    a.append(trackerId);
    a.append(newUrl);
    args.insert("trackerReplace", a);
    requestReport("torrent-set", args, std::move(callback));
}

void RpcClient::addTorrent(const AddTorrent& params, AddCallback callback)
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
    // Общий путь для файлов и Base64: исправленная копия существует только в памяти.
    QByteArray raw;
    if (args.contains("metainfo")) {
        raw = QByteArray::fromBase64(args.value("metainfo").toString().toLatin1());
        const std::string bytes(raw.constData(), static_cast<size_t>(raw.size()));
        bencode::Value value;
        if (!bencode::decode(bytes, value)) {
            bencode::RepairResult repaired;
            if (bencode::repair(bytes, repaired)) {
                raw = QByteArray::fromStdString(repaired.fixedBytes);
                args.insert("metainfo", QString::fromLatin1(raw.toBase64()));
            }
            // Неизвестную порчу оценивает демон: его ошибка возвращается обычным путём.
        }
    }
    if (!params.downloadDir.isEmpty())
        args.insert("download-dir", params.downloadDir);
    args.insert("paused", !params.start);
    if (params.peerLimit > 0)
        args.insert("peer-limit", params.peerLimit);
    if (params.sequential && supports(18))
        args.insert("sequential_download", true);
    if (!callback) {
        requestReport("torrent-add", args);
        return;
    }
    requestReport("torrent-add", args,
        [callback = std::move(callback), raw = std::move(raw)](const JsonRpcResult& result) {
            callback(result, raw);
        });
}

namespace {

/** Read the v1 info hash from a hexadecimal or Base32 magnet identifier. */
QString magnetInfoHash(const QUrlQuery& query)
{
    for (const auto& identifier : query.allQueryItemValues("xt", QUrl::FullyDecoded)) {
        if (!identifier.startsWith("urn:btih:", Qt::CaseInsensitive))
            continue;
        const QByteArray encoded = identifier.mid(9).toLatin1().toUpper();
        if (encoded.size() == 40) {
            const auto decoded = QByteArray::fromHex(encoded);
            if (decoded.size() == 20)
                return QString::fromLatin1(decoded.toHex());
        } else if (encoded.size() == 32) {
            const QByteArray alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
            QByteArray decoded;
            unsigned int bits = 0;
            int count = 0;
            for (const char ch : encoded) {
                const auto digit = alphabet.indexOf(ch);
                if (digit < 0)
                    break;
                bits = (bits << 5) | static_cast<unsigned int>(digit);
                count += 5;
                if (count >= 8) {
                    count -= 8;
                    decoded.append(static_cast<char>((bits >> count) & 0xff));
                }
            }
            if (decoded.size() == 20)
                return QString::fromLatin1(decoded.toHex());
        }
    }
    return {};
}

} // namespace

void RpcClient::updateDuplicateTrackers(int id, const QString& magnet, const QByteArray& metainfo,
                                        TrackerUpdateCallback callback, const QString& expectedHash)
{
    if (!supports(10)) {
        callback(tr("This daemon does not support updating trackers."), false);
        return;
    }
    QStringList trackers;
    QString hash;
    if (!magnet.isEmpty()) {
        const QUrlQuery query{QUrl(magnet)};
        trackers = query.allQueryItemValues("tr", QUrl::FullyDecoded);
        if (id < 0)
            hash = magnetInfoHash(query);
    } else {
        bencode::Value root;
        const auto bytes = metainfo.toStdString();
        if (!bencode::decode(bytes, root)) {
            bencode::RepairResult repaired;
            if (!bencode::repair(bytes, repaired) || !bencode::decode(repaired.fixedBytes, root)) {
                callback(tr("Unable to read trackers from the torrent file."), false);
                return;
            }
        }
        const auto append = [&trackers](const bencode::Value& entry) {
            if (entry.type() == bencode::Value::Type::String)
                trackers.append(QString::fromStdString(entry.toString()).trimmed());
        };
        if (const auto* tiers = root.find("announce-list")) {
            for (const auto& tier : tiers->asList())
                for (const auto& entry : tier.asList())
                    append(entry);
        }
        if (trackers.isEmpty()) {
            if (const auto* announce = root.find("announce"))
                append(*announce);
        }
        if (id < 0 || !expectedHash.isEmpty()) {
            if (const auto* info = root.find("info")) {
                const auto encoded = QByteArray::fromStdString(bencode::encode(*info));
                hash = QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha1).toHex());
                if (!expectedHash.isEmpty()) {
                    const auto v2 = QString::fromLatin1(
                        QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex());
                    if (expectedHash.compare(hash, Qt::CaseInsensitive) != 0 &&
                        expectedHash.compare(v2, Qt::CaseInsensitive) != 0 &&
                        expectedHash.compare(v2.left(40), Qt::CaseInsensitive) != 0)
                        hash.clear();
                }
            }
            if (!expectedHash.isEmpty() && hash.isEmpty()) {
                callback(tr("The downloaded torrent file has a different info hash."), false);
                return;
            }
        }
    }
    trackers.removeAll(QString());
    trackers.removeDuplicates();
    if (trackers.isEmpty()) {
        callback({}, false);
        return;
    }
    if (id < 0 && hash.isEmpty()) {
        callback(tr("Unable to identify the existing torrent."), false);
        return;
    }

    const QJsonArray ids{id >= 0 ? QJsonValue(id) : QJsonValue(hash)};
    requestReport("torrent-get", {{"ids", ids}, {"fields", QJsonArray{"id", "trackers"}}},
        [this, trackers = std::move(trackers), callback = std::move(callback)](
            const JsonRpcResult& result) mutable {
            if (!result.ok) {
                callback(describeError(result), false);
                return;
            }
            const auto torrents = result.arguments.value("torrents").toArray();
            const auto existing = torrents.isEmpty() ? QJsonObject{} : torrents.first().toObject();
            const int existingId = existing.value("id").toInt(-1);
            if (existingId < 0 || !existing.value("trackers").isArray()) {
                callback(tr("Unable to read the existing torrent's trackers."), false);
                return;
            }
            for (const auto& tracker : existing.value("trackers").toArray())
                trackers.removeAll(tracker.toObject().value("announce").toString());
            if (trackers.isEmpty()) {
                callback({}, false);
                return;
            }
            requestReport("torrent-set", {{"ids", QJsonArray{existingId}},
                                          {"trackerAdd", toJsonArray(trackers)}},
                [callback = std::move(callback)](const JsonRpcResult& update) {
                    callback(update.ok ? QString() : describeError(update), update.ok);
                });
        });
}

void RpcClient::configureAddedTorrent(const TorrentDetails& details, const AddTorrent& options,
                                      const QString& name, const QVector<int>& unwanted,
                                      RequestCallback callback)
{
    QList<QPair<QString, QJsonObject>> requests;
    const QJsonArray ids{details.id};
    if (supports(6) && options.downloadDir != details.downloadDir)
        requests.append({"torrent-set-location", {{"ids", ids},
                                                   {"location", options.downloadDir}, {"move", false}}});

    QJsonObject settings;
    if (options.peerLimit > 0 && options.peerLimit != details.peerLimit)
        settings.insert("peer-limit", options.peerLimit);
    if (supports(18))
        settings.insert("sequential_download", options.sequential);
    if (supports(16) && !options.labels.isEmpty())
        settings.insert("labels", toJsonArray(options.labels));
    if (!unwanted.isEmpty()) {
        QJsonArray skipped;
        for (const int index : unwanted)
            skipped.append(index);
        settings.insert("files-unwanted", skipped);
    }
    if (!settings.isEmpty()) {
        settings.insert("ids", ids);
        requests.append({"torrent-set", settings});
    }
    if (supports(15) && !name.isEmpty() && name != details.name)
        requests.append({"torrent-rename-path", {{"ids", ids},
                                                  {"path", details.name}, {"name", name}}});
    if (options.start)
        requests.append({"torrent-start", {{"ids", ids}}});
    applyRequests(std::move(requests), std::move(callback));
}

/** Последовательно выполняет RPC-запросы до первой ошибки. */
void RpcClient::applyRequests(QList<QPair<QString, QJsonObject>> requests, RequestCallback callback)
{
    if (requests.isEmpty()) {
        JsonRpcResult result;
        result.ok = true;
        callback(result);
        return;
    }
    const auto [method, args] = requests.takeFirst();
    requestReport(method, args, [this, requests = std::move(requests),
                                 callback = std::move(callback)](JsonRpcResult r) mutable {
        if (r.ok) {
            applyRequests(std::move(requests), std::move(callback));
            return;
        }
        callback(std::move(r));
    });
}

void RpcClient::setSession(const QJsonObject& params)
{
    requestReport("session-set", params);
}

void RpcClient::setAltSpeed(bool enabled, int downKb, int upKb)
{
    QJsonObject args;
    args.insert("alt-speed-enabled", enabled);
    if (downKb >= 0)
        args.insert("alt-speed-down", downKb);
    if (upKb >= 0)
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
namespace tgui {
void RpcClient::getSession()
{
    call("session-get", QJsonObject{}, [this](const JsonRpcResult& result) {
        SessionInfo info;
        if (!result.ok || !parseSessionInfo(result.arguments, info)) {
            emit readError("session-get", result.ok ? QStringLiteral("Invalid server response.") : describeError(result));
            return;
        }
        m_sessionInfo = info;
        emit sessionInfoReceived(m_sessionInfo);
    });
}
} // namespace tgui
