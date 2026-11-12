#include "rpcdialect.h"

#include <QHash>
#include <QJsonArray>
#include <QStringView>

namespace tgui::rpcdialect {

QString modernKey(const QString& key)
{
    if (key == QLatin1String("cache-size-mb"))
        return QStringLiteral("cache_size_mib");
    QString result;
    result.reserve(key.size() + 4);
    for (qsizetype i = 0; i < key.size(); ++i) {
        const QChar ch = key.at(i);
        if (ch.isUpper() && i > 0 && key.at(i - 1).isLower())
            result += QLatin1Char('_');
        result += ch == QLatin1Char('-') ? QLatin1Char('_') : ch.toLower();
    }
    return result;
}

QJsonObject modernParams(const QString& method, const QJsonObject& params)
{
    QJsonObject result;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        QString key = modernKey(it.key());
        QJsonValue value = it.value();
        if (it.key() == QLatin1String("fields")) {
            QJsonArray fields;
            for (const auto& field : value.toArray())
                fields.append(modernKey(field.toString()));
            value = fields;
        } else if (it.key() == QLatin1String("ids") && value.toString() == QLatin1String("recently-active")) {
            value = QStringLiteral("recently_active");
        } else if (method == QLatin1String("session-set")) {
            if (key == QLatin1String("ratio_limit"))
                key = QStringLiteral("seed_ratio_limit");
            else if (key == QLatin1String("ratio_limit_enabled"))
                key = QStringLiteral("seed_ratio_limited");
            else if (key == QLatin1String("encryption") && value.toString() == QLatin1String("tolerated"))
                value = QStringLiteral("allowed");
        }
        result.insert(key, value);
    }
    return result;
}

/** Преобразует параметры Tr5-стиля обратно в имена legacy RPC (Tr4). */
QJsonObject legacyParams(const QString& method, const QJsonObject& params)
{
    if (method != QLatin1String("session-set"))
        return params;
    QJsonObject result;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        QString key = it.key();
        // В legacy RPC ratio называется camelCase, а не kebab-case.
        if (key == QLatin1String("ratio-limit"))
            key = QStringLiteral("seedRatioLimit");
        else if (key == QLatin1String("ratio-limit-enabled"))
            key = QStringLiteral("seedRatioLimited");
        result.insert(key, it.value());
    }
    return result;
}

namespace {

/** Имена, которые читают модели приложения. Написание Tr4 сверено с api-compat.cc Transmission. */
const QHash<QString, QString>& legacyKeys()
{
    static const auto keys = [] {
        const char* const names[] = {
            "activeTorrentCount", "activityDate", "addedDate", "alt-speed-down", "alt-speed-enabled",
            "alt-speed-time-begin", "alt-speed-time-day", "alt-speed-time-enabled",
            "alt-speed-time-end", "alt-speed-up", "announceState", "bandwidthPriority",
            "blocklist-enabled", "blocklist-size", "blocklist-url", "bytesCompleted", "cache-size-mb",
            "clientName", "corruptEver", "cumulative-stats", "current-stats", "dateCreated",
            "delete-local-data", "dht-enabled", "doneDate", "download-queue-enabled",
            "download-queue-size", "downloadLimit", "downloadLimited", "downloadSpeed",
            "downloadedBytes", "downloadedEver", "encryption", "errorString", "fileStats",
            "files-unwanted",
            "files-wanted", "filesAdded", "flagStr", "hasAnnounced", "hashString", "haveUnchecked",
            "haveValid", "honorsSessionLimits", "idle-seeding-limit", "idle-seeding-limit-enabled",
            "incomplete-dir", "incomplete-dir-enabled",
            "isEncrypted", "isFinished", "isIncoming", "isPrivate",
            "isStalled", "isUTP", "lastAnnounceResult", "lastAnnounceSucceeded", "lastAnnounceTime",
            "lastScrapeResult", "lastScrapeSucceeded", "lastScrapeTime", "leecherCount",
            "leftUntilDone", "lpd-enabled", "magnetLink", "maxConnectedPeers",
            "metadataPercentComplete", "nextAnnounceTime", "pausedTorrentCount", "peerIsChoked",
            "peer-limit", "peer-limit-global", "peer-limit-per-torrent", "peer-port",
            "peer-port-random-high", "peer-port-random-low", "peer-port-random-on-start",
            "peersGettingFromUs", "peersSendingToUs", "pex-enabled", "pieceCount", "pieceSize",
            "port-forwarding-enabled", "port-is-open", "priority-high", "priority-low",
            "priority-normal", "queue-stalled-enabled", "queue-stalled-minutes", "queuePosition",
            "rateDownload", "rateToClient", "rateToPeer",
            "rateUpload", "recheckProgress", "rename-partial-files", "rpc-version",
            "rpc-version-minimum",
            "rpc-version-semver", "secondsActive", "secondsDownloading", "secondsSeeding",
            "seed-queue-enabled", "seed-queue-size",
            "seedIdleLimit", "seedIdleMode", "seedRatioLimit", "seedRatioLimited", "seedRatioMode",
            "seederCount", "size-bytes", "sizeWhenDone", "speed-limit-down",
            "speed-limit-down-enabled", "speed-limit-up", "speed-limit-up-enabled",
            "start-added-torrents", "torrent-added", "torrent-duplicate", "torrentCount", "torrentId",
            "totalSize", "trackerAdd", "trackerList", "trackerRemove", "trackerReplace",
            "trackerStats", "uploadLimit", "uploadLimited", "uploadRatio", "uploadSpeed",
            "uploadedBytes", "uploadedEver", "utp-enabled",
        };
        QHash<QString, QString> result;
        for (const auto* name : names) {
            const auto legacy = QString::fromLatin1(name);
            result.insert(modernKey(legacy), legacy);
        }
        return result;
    }();
    return keys;
}

/** Некоторые имена в Tr4 зависят от метода. */
QString legacyKey(const QString& method, const QString& key)
{
    if (key == QLatin1String("download_dir"))
        return method == QLatin1String("torrent-get") ? QStringLiteral("downloadDir") :
                                                        QStringLiteral("download-dir");
    if (method == QLatin1String("free-space") && key == QLatin1String("total_size"))
        return key;
    if (method == QLatin1String("session-get")) {
        if (key == QLatin1String("torrent_count"))
            return QStringLiteral("torrent-count");
        if (key == QLatin1String("seed_ratio_limit"))
            return QStringLiteral("ratio-limit");
        if (key == QLatin1String("seed_ratio_limited"))
            return QStringLiteral("ratio-limit-enabled");
    }
    return legacyKeys().value(key, key);
}

/** Преобразует ключи и заголовок табличного ответа, сохраняя значения пользователя. */
QJsonValue normalize(const QString& method, const QJsonValue& value)
{
    if (value.isObject()) {
        QJsonObject result;
        const auto object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key() == QLatin1String("torrents")) {
                auto rows = it.value().toArray();
                if (!rows.isEmpty() && rows.first().isArray()) {
                    QJsonArray header;
                    for (const auto& field : rows.first().toArray())
                        header.append(legacyKey(method, field.toString()));
                    rows.replace(0, header);
                }
                result.insert(it.key(), normalize(method, rows));
            } else {
                result.insert(legacyKey(method, it.key()), normalize(method, it.value()));
            }
        }
        return result;
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const auto& element : value.toArray())
            result.append(normalize(method, element));
        return result;
    }
    return value;
}

} // namespace

QJsonObject legacyResult(const QString& method, const QJsonObject& result)
{
    return normalize(method, result).toObject();
}

} // namespace tgui::rpcdialect
