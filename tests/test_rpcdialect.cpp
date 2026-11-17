// TST-03: табличные тесты диалектов RPC (Tr4 legacy ↔ Tr5 modern).
#include "../core/src/rpcdialect.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

namespace {
using tgui::rpcdialect::legacyParams;
using tgui::rpcdialect::legacyResult;
using tgui::rpcdialect::modernKey;
using tgui::rpcdialect::modernParams;
} // namespace

TEST(RpcDialectModernKey, ConvertsNames)
{
    EXPECT_EQ(modernKey("downloadDir"), "download_dir");
    EXPECT_EQ(modernKey("fileStats"), "file_stats");
    EXPECT_EQ(modernKey("alt-speed-enabled"), "alt_speed_enabled");
    EXPECT_EQ(modernKey("peersSendingToUs"), "peers_sending_to_us");
    EXPECT_EQ(modernKey("torrent-get"), "torrent_get");
    // Специальное имя кеша.
    EXPECT_EQ(modernKey("cache-size-mb"), "cache_size_mib");
    // Уже «modern» имя — неизменно.
    EXPECT_EQ(modernKey("download_dir"), "download_dir");
}

TEST(RpcDialectModernParams, ConvertsFieldsArray)
{
    const QJsonObject params{{"fields", QJsonArray{"id", "name", "fileStats", "peersSendingToUs"}}};
    const auto modern = modernParams("torrent-get", params);
    const QJsonArray expected{"id", "name", "file_stats", "peers_sending_to_us"};
    EXPECT_EQ(modern.value("fields").toArray(), expected);
}

TEST(RpcDialectModernParams, ConvertsIdsRecentActive)
{
    const QJsonObject params{{"ids", "recently-active"}};
    EXPECT_EQ(modernParams("torrent-get", params).value("ids"), "recently_active");
    // Остальные значения ids не трогаем.
    EXPECT_EQ(modernParams("torrent-get", QJsonObject{{"ids", "1,2"}}).value("ids"), "1,2");
}

TEST(RpcDialectModernParams, SessionSetRenaming)
{
    const QJsonObject params{{"ratio_limit", 2.5},
                             {"ratio_limit_enabled", true},
                             {"encryption", "tolerated"},
                             {"download_limit", 10}};
    const auto modern = modernParams("session-set", params);
    EXPECT_EQ(modern.value("seed_ratio_limit").toDouble(), 2.5);
    EXPECT_EQ(modern.value("seed_ratio_limited").toBool(), true);
    EXPECT_EQ(modern.value("encryption").toString(), "allowed");
    EXPECT_EQ(modern.value("download_limit").toInt(), 10);
    // Старые имена не остаются.
    EXPECT_FALSE(modern.contains("ratio_limit"));
    EXPECT_FALSE(modern.contains("ratio_limit_enabled"));
}

TEST(RpcDialectModernParams, SessionGetIsNotRenamed)
{
    const QJsonObject params{{"ratio_limit", 2.5}};
    // Правило переименования — только для session-set.
    EXPECT_EQ(modernParams("session-get", params).value("ratio_limit").toDouble(), 2.5);
}

TEST(RpcDialectLegacyParams, SessionSetCamelCase)
{
    const QJsonObject params{{"ratio-limit", 5.0},
                             {"ratio-limit-enabled", true},
                             {"download-dir", "/downloads"}};
    const auto legacy = legacyParams("session-set", params);
    EXPECT_EQ(legacy.value("seedRatioLimit").toDouble(), 5.0);
    EXPECT_EQ(legacy.value("seedRatioLimited").toBool(), true);
    EXPECT_EQ(legacy.value("download-dir").toString(), "/downloads");
    EXPECT_FALSE(legacy.contains("ratio-limit"));
    EXPECT_FALSE(legacy.contains("ratio-limit-enabled"));
}

TEST(RpcDialectLegacyParams, OtherMethodsUntouched)
{
    const QJsonObject params{{"ratio-limit", 5.0}};
    EXPECT_EQ(legacyParams("session-get", params), params);
}

TEST(RpcDialectLegacyResult, TorrentGetTableHeader)
{
    const QJsonObject result{{"torrents", QJsonArray{
        QJsonArray{"id", "name", "download_dir", "file_stats", "peers_sending_to_us"},
        QJsonArray{1, "alpha", "/downloads", QJsonArray{}, 7}}}};
    const auto legacy = legacyResult("torrent-get", result);
    const auto rows = legacy.value("torrents").toArray();
    ASSERT_EQ(rows.size(), 2);
    const QJsonArray expectedHeader{"id", "name", "downloadDir", "fileStats", "peersSendingToUs"};
    const QJsonArray expectedRow{1, "alpha", "/downloads", QJsonArray{}, 7};
    EXPECT_EQ(rows.at(0).toArray(), expectedHeader);
    // Значения строк не трогаем.
    EXPECT_EQ(rows.at(1).toArray(), expectedRow);
}

TEST(RpcDialectLegacyResult, NonTableTorrentsUntouched)
{
    // Не табличное значение (массив скалярных) остаётся как есть.
    const QJsonObject result{{"torrents", QJsonArray{1, 2, 3}}};
    const QJsonArray expected{1, 2, 3};
    EXPECT_EQ(legacyResult("torrent-get", result).value("torrents").toArray(), expected);
}

TEST(RpcDialectLegacyResult, SessionGetMethodSpecificNames)
{
    const QJsonObject result{{"torrent_count", 4},
                             {"seed_ratio_limit", 3.0},
                             {"seed_ratio_limited", true},
                             {"download_dir", "/downloads"},
                             {"blocklist_enabled", false}};
    const auto legacy = legacyResult("session-get", result);
    EXPECT_EQ(legacy.value("torrent-count").toInt(), 4);
    EXPECT_EQ(legacy.value("ratio-limit").toDouble(), 3.0);
    EXPECT_EQ(legacy.value("ratio-limit-enabled").toBool(), true);
    EXPECT_EQ(legacy.value("download-dir").toString(), "/downloads");
    EXPECT_EQ(legacy.value("blocklist-enabled").toBool(), false);
}

TEST(RpcDialectLegacyResult, FreeSpaceKeepsNames)
{
    const QJsonObject result{{"total_size", 100.0}, {"size-bytes", 200.0}};
    const auto legacy = legacyResult("free-space", result);
    EXPECT_EQ(legacy.value("total_size").toDouble(), 100.0);
    EXPECT_EQ(legacy.value("size-bytes").toDouble(), 200.0);
}

TEST(RpcDialectLegacyResult, NestedObjectsAndUnknownKeys)
{
    const QJsonObject result{{"data", QJsonObject{{"download_dir", "/downloads"}}},
                             {"unknown_key", 1}};
    const auto legacy = legacyResult("torrent-get", result);
    EXPECT_EQ(legacy.value("data").toObject().value("downloadDir").toString(), "/downloads");
    // Неизвестные имена проходим как есть.
    EXPECT_EQ(legacy.value("unknown_key").toInt(), 1);
}

TEST(RpcDialect, RoundTripModernToLegacy)
{
    // modernKey → legacyKey даёт исходное имя (torrent-get).
    const char* const names[] = {"downloadDir", "fileStats", "torrentCount",
                                 "downloadLimit", "blocklist-enabled", "queuePosition"};
    for (const auto* name : names) {
        const auto modern = modernKey(name);
        // Обратный ход проверяем через нормализацию ответа.
        const QJsonObject legacy = legacyResult("torrent-get", QJsonObject{{modern, 1}});
        EXPECT_TRUE(legacy.contains(name)) << "round trip failed for " << name << " -> "
                                           << modern.toStdString();
    }
}