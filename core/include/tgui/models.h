#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <optional>

namespace tgui {

// Статусы Transmission (rpc-version ≥ 14).
enum class TorrentStatus : int {
    Stopped = 0,
    CheckWait = 1,
    Check = 2,
    DownloadWait = 3,
    Download = 4,
    SeedWait = 5,
    Seed = 6,
};

// Псевдо-статус «Завершено»: Seed при
// sizeWhenDone > 0 и leftUntilDone == 0.
bool statusIsFinished(int status, double sizeWhenDone, double leftUntilDone);

// Приоритет полосы пропускания (bandwidthpriority: -1/0/1).
enum class BandwidthPriority : int { Low = -1, Normal = 0, High = 1 };

// trackerStats[] из torrent-get.
struct TrackerStat {
    QString announce;               // URL трекера
    int announceState = 0;          // 0 none, 1 queued, 2 active, 3 fetching
    bool hasAnnounced = false;
    bool lastAnnounceSucceeded = false;
    QString lastAnnounceResult;
    int lastAnnounceTime = 0;       // unix-время
    int seederCount = -1;
    int leecherCount = -1;
    int torrentId = 0;
};

// Торрент (torrent-get). Опциональные поля зависят от запрошенных "fields".
struct Torrent {
    int id = 0;

    std::optional<QString> name;
    std::optional<int> status;
    std::optional<QString> errorString;
    std::optional<QString> announceResponse;
    std::optional<double> recheckProgress;         // 0..1
    std::optional<double> sizeWhenDone;            // байты
    std::optional<double> totalSize;               // байты
    std::optional<double> leftUntilDone;           // байты
    std::optional<double> downloadedEver;          // байты
    std::optional<double> uploadedEver;            // байты
    std::optional<double> uploadRatio;
    std::optional<double> metadataPercentComplete; // 0..1
    std::optional<int> rateDownload;               // Б/с
    std::optional<int> rateUpload;                 // Б/с
    std::optional<int> peersSendingToUs;
    std::optional<int> peersGettingFromUs;
    std::optional<int> eta;                        // сек, -1 = ∞
    std::optional<int> addedDate;                  // unix
    std::optional<int> doneDate;                   // unix
    std::optional<int> activityDate;               // unix
    std::optional<int> secondsSeeding;
    std::optional<int> secondsDownloading;
    std::optional<int> bandwidthPriority;          // -1 / 0 / 1
    std::optional<int> queuePosition;
    std::optional<bool> isPrivate;
    std::optional<bool> isFinished;
    std::optional<bool> isStalled;
    std::optional<bool> sequentialDownload;
    std::optional<QStringList> labels;
    std::optional<QString> downloadDir;
    QVector<TrackerStat> trackerStats;
};

// Пир (torrent-get peers).
struct Peer {
    QString address;
    int port = 0;
    QString clientName;
    QString flagStr;
    double progress = 0.0; // 0..100
    int rateToClient = 0;  // Б/с
    int rateToPeer = 0;    // Б/с
};

// Файл торрента (torrent-get files + priorities + wanted).
struct FileStat {
    int index = 0;
    QString name;
    double size = 0.0; // байты
    int priority = 0;  // -1 / 0 / 1
    bool wanted = true;
};

// Детали одного торрента для вкладки «Файлы».
struct TorrentDetails {
    int id = 0;
    QString downloadDir;
    QVector<FileStat> files;
};

// session-get (информация о сессии + параметры демона).
struct SessionInfo {
    int rpcVersion = 0;
    QString version;
    QString downloadDir;
    std::optional<qint64> downloadDirFreeSpace; // устанавливается клиентом из free-space

    bool altSpeedEnabled = false;
    int altSpeedDown = 0; // КБ/с
    int altSpeedUp = 0;   // КБ/с
    bool altSpeedTimeEnabled = false;
    int altSpeedTimeBegin = 0; // минуты
    int altSpeedTimeEnd = 0;   // минуты
    int altSpeedTimeDay = 0;

    bool speedLimitDownEnabled = false;
    int speedLimitDown = 0; // КБ/с
    bool speedLimitUpEnabled = false;
    int speedLimitUp = 0;   // КБ/с

    bool blocklistEnabled = false;
    QString blocklistUrl;
    int torrentCount = 0;
    bool dhtEnabled = false;
    bool pexEnabled = false;
    bool lpdEnabled = false;
    bool utpEnabled = false;
    int peerPort = 0;
    bool startAddedTorrents = true;
    bool queueEnabled = false;
    int queueSize = 0;
    double ratioLimit = 0.0;
    bool ratioLimitEnabled = false;

    QJsonObject raw; // полный ответ session-get
};

struct StatsBlock {
    double downloadedBytes = 0.0;
    double uploadedBytes = 0.0;
    long long filesAdded = 0;
    long long secondsActive = 0;
};

// session-stats (current-stats / cumulative-stats).
struct SessionStats {
    StatsBlock current;
    StatsBlock cumulative;
};

// --- Состояние торрента (семантика FillTorrentsList) ---
bool statusIsDownloading(int status); // Download
bool statusIsSeeding(int status);     // Seed
bool statusIsWaiting(int status);     // CheckWait | Check | DownloadWait
bool statusIsStopped(int status);     // Stopped
bool torrentIsActive(int rateDownload, int rateUpload);

// --- Парсинг ответов демона ---
bool parseTorrent(const QJsonObject& torrent, Torrent& out);
QVector<Torrent> parseTorrents(const QJsonArray& array);
// torrent-get с format=table (rpc ≥ 16): torrents[0] — имена полей,
// остальные строки — значения (аналог TranslateTableToObjects).
QVector<Torrent> parseTorrentsTable(const QJsonObject& body);

QVector<TrackerStat> parseTrackerStats(const QJsonArray& array);
QVector<Peer> parsePeers(const QJsonArray& array);
QVector<FileStat> parseFiles(const QJsonObject& torrent);
bool parseSessionInfo(const QJsonObject& json, SessionInfo& out);
bool parseSessionStats(const QJsonObject& json, SessionStats& out);

} // namespace tgui