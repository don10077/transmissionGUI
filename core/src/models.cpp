#include "tgui/models.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <QObject>

namespace tgui {
namespace {

std::optional<int> optInt(const QJsonObject& j, const char* key)
{
    const QJsonValue v = j.value(key);
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (d < INT_MIN || d > INT_MAX)
        return std::nullopt;
    return static_cast<int>(d);
}

std::optional<double> optNum(const QJsonObject& j, const char* key)
{
    const QJsonValue v = j.value(key);
    return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt;
}

std::optional<QString> optStr(const QJsonObject& j, const char* key)
{
    const QJsonValue v = j.value(key);
    return v.isString() ? std::optional<QString>(v.toString()) : std::nullopt;
}

std::optional<bool> optBool(const QJsonObject& j, const char* key)
{
    const QJsonValue v = j.value(key);
    return v.isBool() ? std::optional<bool>(v.toBool()) : std::nullopt;
}

QStringList optStringList(const QJsonArray& array)
{
    QStringList list;
    for (const QJsonValue& e : array)
        if (e.isString())
            list << e.toString();
    return list;
}

bool statusIn(int status, std::initializer_list<int> values)
{
    for (int v : values)
        if (v == status)
            return true;
    return false;
}

} // namespace

bool statusIsDownloading(int status)
{
    return status == static_cast<int>(TorrentStatus::Download);
}

bool statusIsSeeding(int status)
{
    return status == static_cast<int>(TorrentStatus::Seed);
}

bool statusIsWaiting(int status)
{
    return statusIn(status, {
        static_cast<int>(TorrentStatus::CheckWait),
        static_cast<int>(TorrentStatus::Check),
        static_cast<int>(TorrentStatus::DownloadWait),
    });
}

bool statusIsStopped(int status)
{
    return status == static_cast<int>(TorrentStatus::Stopped);
}

bool statusIsFinished(int status, double sizeWhenDone, double leftUntilDone)
{
    // «Завершено»: раздача либо остановленный торрент, у которого не осталось
    // данных. transgui помечает такой остановленный торрент как FINISHED
    // (см. FillTorrentsList: StateImg = imgDone + leftUntilDone = 0).
    if (sizeWhenDone <= 0.0 || leftUntilDone != 0.0)
        return false;
    return status == static_cast<int>(TorrentStatus::Seed) ||
           status == static_cast<int>(TorrentStatus::Stopped);
}

bool torrentIsActive(int rateDownload, int rateUpload)
{
    return rateDownload != 0 || rateUpload != 0;
}

bool parseTorrent(const QJsonObject& j, Torrent& t)
{
    if (!j.contains("id"))
        return false;

    t.id = optInt(j, "id").value_or(0);
    t.name = optStr(j, "name");
    t.status = optInt(j, "status");
    t.errorString = optStr(j, "errorString");
    t.announceResponse = optStr(j, "announceResponse");
    t.recheckProgress = optNum(j, "recheckProgress");
    t.sizeWhenDone = optNum(j, "sizeWhenDone");
    t.totalSize = optNum(j, "totalSize");
    t.leftUntilDone = optNum(j, "leftUntilDone");
    t.downloadedEver = optNum(j, "downloadedEver");
    t.uploadedEver = optNum(j, "uploadedEver");
    t.corruptEver = optNum(j, "corruptEver");
    t.uploadRatio = optNum(j, "uploadRatio");
    t.metadataPercentComplete = optNum(j, "metadataPercentComplete");
    t.rateDownload = optInt(j, "rateDownload");
    t.rateUpload = optInt(j, "rateUpload");
    t.peersSendingToUs = optInt(j, "peersSendingToUs");
    t.peersGettingFromUs = optInt(j, "peersGettingFromUs");
    t.eta = optInt(j, "eta");
    t.addedDate = optInt(j, "addedDate");
    t.doneDate = optInt(j, "doneDate");
    t.activityDate = optInt(j, "activityDate");
    t.secondsSeeding = optInt(j, "secondsSeeding");
    t.secondsDownloading = optInt(j, "secondsDownloading");
    t.bandwidthPriority = optInt(j, "bandwidthPriority");
    t.queuePosition = optInt(j, "queuePosition");
    t.isPrivate = optBool(j, "isPrivate");
    t.isFinished = optBool(j, "isFinished");
    t.isStalled = optBool(j, "isStalled");
    t.sequentialDownload = optBool(j, "sequential_download");
    t.downloadDir = optStr(j, "downloadDir");
    t.hashString = optStr(j, "hashString");
    t.comment = optStr(j, "comment");
    t.creator = optStr(j, "creator");
    t.dateCreated = optInt(j, "dateCreated");
    t.pieceCount = optInt(j, "pieceCount");
    t.pieceSize = optNum(j, "pieceSize");
    t.haveValid = optNum(j, "haveValid");
    t.pieces = optStr(j, "pieces");
    t.magnetLink = optStr(j, "magnetLink");
    t.maxConnectedPeers = optInt(j, "maxConnectedPeers");
    t.editableSettings = QJsonObject{};
    for (const auto& key : QStringList{"downloadLimited","downloadLimit","uploadLimited","uploadLimit","seedRatioMode","seedRatioLimit","seedIdleMode","seedIdleLimit"})
        if (j.contains(key)) t.editableSettings.insert(key, j.value(key));

    const QJsonValue labels = j.value("labels");
    if (labels.isArray())
        t.labels = optStringList(labels.toArray());

    const QJsonValue ts = j.value("trackerStats");
    if (ts.isArray())
        t.trackerStats = parseTrackerStats(ts.toArray());

    return true;
}

QVector<Torrent> parseTorrents(const QJsonArray& array)
{
    QVector<Torrent> result;
    for (const QJsonValue& e : array) {
        if (!e.isObject())
            continue;
        Torrent t;
        if (parseTorrent(e.toObject(), t))
            result.push_back(t);
    }
    return result;
}

QVector<Torrent> parseTorrentsTable(const QJsonObject& body)
{
    QVector<Torrent> result;
    const QJsonArray arr = body.value("torrents").toArray();
    if (arr.isEmpty() || !arr.first().isArray())
        return result;

    const QStringList fields = optStringList(arr.first().toArray());
    for (int i = 1; i < arr.size(); ++i) {
        if (!arr.at(i).isArray())
            continue;
        const QJsonArray row = arr.at(i).toArray();
        QJsonObject obj;
        const int n = std::min(fields.size(), row.size());
        for (int f = 0; f < n; ++f)
            obj.insert(fields.at(f), row.at(f));
        Torrent t;
        if (parseTorrent(obj, t))
            result.push_back(t);
    }
    return result;
}

QVector<TrackerStat> parseTrackerStats(const QJsonArray& array)
{
    QVector<TrackerStat> result;
    for (const QJsonValue& e : array) {
        if (!e.isObject())
            continue;
        const QJsonObject j = e.toObject();
        TrackerStat s;
        s.id = j.contains("id") ? j.value("id").toInt() : -1;
        s.announce = j.value("announce").toString();
        s.announceState = j.value("announceState").toInt();
        s.hasAnnounced = j.value("hasAnnounced").toBool();
        s.lastAnnounceSucceeded = j.value("lastAnnounceSucceeded").toBool();
        s.lastAnnounceResult = j.value("lastAnnounceResult").toString();
        s.lastAnnounceTime = j.value("lastAnnounceTime").toInt();
        s.nextAnnounceTime = j.value("nextAnnounceTime").toInt();
        s.seederCount = j.value("seederCount").isDouble() ? j.value("seederCount").toInt() : -1;
        s.leecherCount =
            j.value("leecherCount").isDouble() ? j.value("leecherCount").toInt() : -1;
        s.tier = j.value("tier").isDouble() ? j.value("tier").toInt() : 0;
        s.torrentId = j.value("torrentId").toInt();
        result.push_back(s);
    }
    return result;
}

QVector<Peer> parsePeers(const QJsonArray& array)
{
    QVector<Peer> result;
    for (const QJsonValue& e : array) {
        if (!e.isObject())
            continue;
        const QJsonObject j = e.toObject();
        // Legacy (Tr4) — camelCase; в Transmission 4.x нативный ответ —
        // kebab-case (client-name, rate-to-client, …). Компат-слой демона
        // возвращает legacy-имена, но поддерживаем оба варианта.
        auto num = [&j](const char* camel, const char* kebab) -> double {
            if (j.contains(camel) && j.value(camel).isDouble())
                return j.value(camel).toDouble();
            if (j.contains(kebab) && j.value(kebab).isDouble())
                return j.value(kebab).toDouble();
            return -1.0;
        };
        auto str = [&j](const char* camel, const char* kebab) -> QString {
            if (j.contains(camel))
                return j.value(camel).toString();
            if (j.contains(kebab))
                return j.value(kebab).toString();
            return QString();
        };
        Peer p;
        p.address = str("address", "ip");
        const double port = num("port", "port");
        p.port = port >= 0 ? static_cast<int>(port) : 0;
        p.clientName = str("clientName", "client-name");
        p.flagStr = str("flagStr", "flag-str");
        const double progress = num("progress", "progress");
        if (progress >= 0.0)
            p.progress = progress;
        const double rtc = num("rateToClient", "rate-to-client");
        if (rtc >= 0.0)
            p.rateToClient = static_cast<int>(rtc);
        const double rtp = num("rateToPeer", "rate-to-peer");
        if (rtp >= 0.0)
            p.rateToPeer = static_cast<int>(rtp);
        result.push_back(p);
    }
    return result;
}

QVector<FileStat> parseFiles(const QJsonObject& torrent)
{
    QVector<FileStat> result;
    const QJsonArray files = torrent.value("files").toArray();
    const QJsonArray priorities = torrent.value("priorities").toArray();
    const QJsonArray wanted = torrent.value("wanted").toArray();

    for (int i = 0; i < files.size(); ++i) {
        if (!files.at(i).isObject())
            continue;
        const QJsonObject f = files.at(i).toObject();
        FileStat fs;
        fs.index = i;
        fs.name = f.value("name").toString();
        // В torrent-get размер файла — в поле "length" (не "size").
        fs.size = f.value("length").toDouble();
        if (fs.size <= 0.0)
            fs.size = f.value("size").toDouble();
        // Прогресс файла: legacy "completed" (0..1); иначе — по байтам
        // (демоны 4.x возвращают camelCase "bytesCompleted").
        if (f.value("completed").isDouble())
            fs.completion = f.value("completed").toDouble();
        else if (f.value("bytesCompleted").isDouble() && fs.size > 0.0)
            fs.completion = f.value("bytesCompleted").toDouble() / fs.size;
        else if (f.value("bytes_completed").isDouble() && fs.size > 0.0)
            fs.completion = f.value("bytes_completed").toDouble() / fs.size;
        if (i < priorities.size() && priorities.at(i).isDouble())
            fs.priority = static_cast<int>(priorities.at(i).toDouble());
        if (i < wanted.size())
            fs.wanted = wanted.at(i).isBool() ? wanted.at(i).toBool()
                                              : wanted.at(i).toInt() != 0;
        result.push_back(fs);
    }
    return result;
}

namespace {

// session-stats: число >= 0 и конечное.
bool getNumber(const QJsonObject& s, const char* key, double& out)
{
    const QJsonValue v = s.value(key);
    if (!v.isDouble())
        return false;
    out = v.toDouble();
    return out >= 0.0 && std::isfinite(out);
}

// session-stats: целое >= 0 и <= maxValue.
bool getInt(const QJsonObject& s, const char* key, long long& out, long long maxValue)
{
    const QJsonValue v = s.value(key);
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d < 0.0 || d > static_cast<double>(maxValue) || d != std::trunc(d))
        return false;
    out = static_cast<long long>(d);
    return true;
}

bool getStatsBlock(const QJsonObject& json, const char* key, StatsBlock& out)
{
    const QJsonValue v = json.value(key);
    if (!v.isObject())
        return false;
    const QJsonObject s = v.toObject();
    double downloaded = 0.0;
    double uploaded = 0.0;
    long long files = 0;
    long long seconds = 0;
    if (!getNumber(s, "downloadedBytes", downloaded) || !getNumber(s, "uploadedBytes", uploaded) ||
        !getInt(s, "filesAdded", files, static_cast<long long>(INT_MAX)) ||
        !getInt(s, "secondsActive", seconds, static_cast<long long>(INT_MAX) - 30))
        return false;
    out.downloadedBytes = downloaded;
    out.uploadedBytes = uploaded;
    out.filesAdded = files;
    out.secondsActive = seconds;
    return true;
}

} // namespace

bool parseSessionInfo(const QJsonObject& j, SessionInfo& out)
{
    if (!j.contains("rpc-version") && !j.contains("version"))
        return false;

    out.rpcVersion = j.value("rpc-version").toInt();
    out.version = j.value("version").toString();
    out.downloadDir = j.value("download-dir").toString();

    out.altSpeedEnabled = j.value("alt-speed-enabled").toBool();
    out.altSpeedDown = j.value("alt-speed-down").toInt();
    out.altSpeedUp = j.value("alt-speed-up").toInt();
    out.altSpeedTimeEnabled = j.value("alt-speed-time-enabled").toBool();
    out.altSpeedTimeBegin = j.value("alt-speed-time-begin").toInt();
    out.altSpeedTimeEnd = j.value("alt-speed-time-end").toInt();
    out.altSpeedTimeDay = j.value("alt-speed-time-day").toInt();

    out.speedLimitDownEnabled = j.value("speed-limit-down-enabled").toBool();
    out.speedLimitDown = j.value("speed-limit-down").toInt();
    out.speedLimitUpEnabled = j.value("speed-limit-up-enabled").toBool();
    out.speedLimitUp = j.value("speed-limit-up").toInt();

    out.blocklistEnabled = j.value("blocklist-enabled").toBool();
    out.blocklistUrl = j.value("blocklist-url").toString();
    out.torrentCount = j.value("torrent-count").toInt();
    out.dhtEnabled = j.value("dht-enabled").toBool();
    out.pexEnabled = j.value("pex-enabled").toBool();
    out.lpdEnabled = j.value("lpd-enabled").toBool();
    out.utpEnabled = j.value("utp-enabled").toBool();
    out.peerPort = j.value("peer-port").toInt();
    out.startAddedTorrents = j.contains("start-added-torrents")
                                 ? j.value("start-added-torrents").toBool()
                                 : true;
    out.queueEnabled = j.value("queue-enabled").toBool();
    out.queueSize = j.value("queue-size").toInt();
    out.ratioLimit = j.value("ratio-limit").toDouble();
    out.ratioLimitEnabled = j.value("ratio-limit-enabled").toBool();
    out.raw = j;
    return true;
}

bool parseSessionStats(const QJsonObject& j, SessionStats& out)
{
    StatsBlock current;
    StatsBlock cumulative;
    if (!getStatsBlock(j, "current-stats", current) ||
        !getStatsBlock(j, "cumulative-stats", cumulative))
        return false;
    out.current = current;
    out.cumulative = cumulative;
    return true;
}

} // namespace tgui
namespace tgui {
QString torrentError(const Torrent& torrent)
{
    QString global = torrent.errorString.value_or(QString());
    QString result;
    bool working = false;
    for (const auto& tracker : torrent.trackerStats) {
        QString error;
        if (tracker.hasAnnounced && !tracker.lastAnnounceSucceeded)
            error = tracker.lastAnnounceResult;
        if (error == QStringLiteral("Success")) error.clear();
        if (error.isEmpty()) {
            working = true;
            result.clear();
        } else {
            if (!working && result.isEmpty()) result = QObject::tr("Tracker error: %1").arg(error);
            if (global == error) global.clear();
        }
    }
    if (torrent.trackerStats.isEmpty()) {
        const QString legacy = torrent.announceResponse.value_or(QString());
        if (!legacy.isEmpty() && legacy != QStringLiteral("Success") &&
            !legacy.contains(QStringLiteral("(200)")))
            result = QObject::tr("Tracker error: %1").arg(legacy);
    }
    // A stopped torrent does not announce; stale tracker errors are hidden,
    // but local failures (missing files, disk errors, etc.) remain visible.
    if (result.isEmpty() || torrent.status.value_or(-1) == 0) return global;
    return result;
}
} // namespace tgui
