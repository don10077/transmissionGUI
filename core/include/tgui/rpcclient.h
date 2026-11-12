#pragma once

#include "tgui/jsonrpc.h"
#include "tgui/models.h"

#include <QJsonObject>
#include <QObject>
#include <QNetworkProxy>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace tgui {

// Высокоуровневый асинхронный RPC-клиент Transmission (QObject, сигналы в
// потоке-владельце).
class RpcClient : public QObject {
    Q_OBJECT
public:
    struct Options {
        QUrl url;
        QString rpcPath; // пусто → "/transmission/rpc"
        QString username;
        QString password;
        int timeoutMs = 30000;
        bool ignoreSslErrors = false;
        QString clientCertificatePath;
        QString clientKeyPath;
        std::optional<QByteArray> clientKeyPassphrase; // Только в памяти на время подключения.
        QNetworkProxy proxy{ QNetworkProxy::NoProxy }; // по умолчанию — без прокси
    };

    // Параметры torrent-add.
    struct AddTorrent {
        enum class Source { File, Base64, Url };
        Source source = Source::File;
        QString file;         // Source::File — путь к .torrent
        QString metainfoBase64; // Source::Base64 — base64 метаданных
        QString url;          // Source::Url — magnet/URL (идёт в "filename")
        QString downloadDir;
        int peerLimit = 0;    // 0 — не устанавливать
        bool start = false;   // true → paused=false сразу
        bool sequential = false; // пошлется только при rpc-version >= 18
        QStringList labels;    // метки выбранные при добавлении, RPC >= 16
    };

    using DetailsCallback = std::function<void(const std::optional<TorrentDetails>&)>;
    using RequestCallback = JsonRpcClient::Callback;
    using AddCallback = std::function<void(const JsonRpcResult&, const QByteArray&)>;
    using TrackerUpdateCallback = std::function<void(const QString& error, bool updated)>;

    explicit RpcClient(const Options& options, QObject* parent = nullptr);

    const Options& options() const { return m_options; }
    bool isConnected() const { return m_connected; }
    int rpcVersion() const { return m_rpcVersion; }
    const SessionInfo& sessionInfo() const { return m_sessionInfo; }

    // Поддержка возможности по версии RPC (labels ≥ 16, sequential ≥ 18 …).
    bool supports(int minRpcVersion) const { return m_rpcVersion >= minRpcVersion; }

    // --- Соединение (session-get) ---
    void connectToDaemon();
    void disconnectFromDaemon();

    // --- Чтение ---
    void getTorrents(const QStringList& extraFields = {});
    void getTorrentDetails(int id, DetailsCallback callback = {});
    void getPeers(int id);
    void getTrackers(int id);
    void getSessionStats();
    void getSession(); // Refresh confirmed session settings without reconnecting.
    void getFreeSpace(const QString& path);
    void portTest();

    // --- Действия с торрентами ---
    void startTorrent(int id, bool force = false); // torrent-start / torrent-start-now
    void stopTorrent(int id);
    /** Отправляет одну команду для указанных торрентов. Пустой выбор пропускается. */
    void torrentAction(const QString& method, const QVector<int>& ids,
                       const QJsonObject& params = {}, RequestCallback callback = {});
    void startAll(bool force = false, RequestCallback callback = {}); // без ids — все торренты
    void stopAll(RequestCallback callback = {});
    void removeTorrent(int id, bool deleteLocalData = false, RequestCallback callback = {});
    void reannounce(int id);
    void verify(int id);
    // Очередь (legacy-методы queue-move-*, RPC ≥ 14).
    void queueMoveTop(int id);
    void queueMoveUp(int id);
    void queueMoveDown(int id);
    void queueMoveBottom(int id);
    void setStarted(int id, bool started);
    void setSequential(int id, bool enabled); // sequential_download, требует rpc >= 18
    void renamePath(int id, const QString& path, const QString& newName, RequestCallback callback = {});
    void setLocation(int id, const QString& path, bool moveData);
    // Пакетный перенос: один torrent-set-location для всех ID (RPC ≥ 6).
    void setLocation(const QVector<int>& ids, const QString& path, bool moveData,
                     RequestCallback callback = {});
    void setTorrent(int id, const QJsonObject& params); // общий torrent-set
    void setLabels(int id, const QStringList& labels);  // требует rpc >= 16
    /** Заменяет имя метки у торрентов текущего демона, сохраняя их остальные метки. */
    void renameLabel(const QString& from, const QString& to, RequestCallback callback);
    void setPriority(int id, int priority);             // -1 / 0 / 1 (весь торрент)
    void setFilesWanted(int id, const QVector<int>& wanted, const QVector<int>& unwanted, RequestCallback callback = {});
    // Per-file приоритет (legacy): files-wanted + priority-{low|normal|high}.
    void setFilePriority(int id, const QVector<int>& files, int priority, RequestCallback callback = {});
    // Трекеры (legacy): trackerAdd / trackerRemove / trackerReplace; требует RPC >= 10.
    void addTracker(int id, const QString& url, RequestCallback callback = {});
    void removeTracker(int id, int trackerId, RequestCallback callback = {});
    void replaceTracker(int id, int trackerId, const QString& newUrl, RequestCallback callback = {});
    void addTorrent(const AddTorrent& params, AddCallback callback = {});
    void updateDuplicateTrackers(int id, const QString& magnet, const QByteArray& metainfo,
                                 TrackerUpdateCallback callback, const QString& expectedHash = {});
    void configureAddedTorrent(const TorrentDetails& details, const AddTorrent& options,
                               const QString& name, const QVector<int>& unwanted,
                               RequestCallback callback);

    // --- Демон (session-set и прочее) ---
    void setSession(const QJsonObject& params); // общий session-set
    void setAltSpeed(bool enabled, int downKb = -1, int upKb = -1);
    void setBlocklist(bool enabled, const QString& url);
    void updateBlocklist();

signals:
    // Соединение
    void connected(const SessionInfo& info);
    void connectionFailed(const QString& error);
    void connectionInterrupted(const QString& error);
    // HTTP 401 (неверные/протухшие учётные данные) — предложить повторную авторизацию
    void authenticationFailed(const QString& error);
    void clientTlsPassphraseRequired();

    // Чтение
    void torrentsReceived(const QVector<Torrent>& torrents);
    void torrentDetailsReceived(const TorrentDetails& details);
    void peersReceived(int id, const QVector<Peer>& peers);
    void trackersReceived(int id, const QVector<TrackerStat>& trackers);
    void sessionInfoReceived(const SessionInfo& info);
    void sessionStatsReceived(const SessionStats& stats);
    void freeSpaceReceived(const QString& path, qint64 bytes);
    void readError(const QString& what, const QString& error);

    // Любая изменяющая операция (method — имя RPC-метода, arguments — ответ)
    void requestCompleted(const QString& method, bool ok, const QString& error,
                          const QJsonObject& arguments);

private:
    bool configureClientTls();
    void call(const QString& method, const QJsonObject& arguments, JsonRpcClient::Callback callback);
    void request(const QString& method, const QJsonObject& arguments);
    void requestReport(const QString& method, const QJsonObject& arguments,
                       RequestCallback callback = {});
    void reportResult(const QString& method, const JsonRpcResult& result,
                      const RequestCallback& callback);
    void applyRequests(QList<QPair<QString, QJsonObject>> requests, RequestCallback callback);
    void torrentIds(QJsonObject& args, int id) const;
    static QString describeError(const JsonRpcResult& r);

    JsonRpcClient m_rpc;
    quint64 m_readSequence = 0;
    quint64 m_listApplied = 0;
    quint64 m_filesApplied = 0;
    quint64 m_trackersApplied = 0;
    Options m_options;
    bool m_connected = false;
    bool m_connectionProblemReported = false;
    int m_rpcVersion = 0;
    SessionInfo m_sessionInfo;
};

} // namespace tgui
