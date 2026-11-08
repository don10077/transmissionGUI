#include <gtest/gtest.h>

#include "tgui/models.h"

using namespace tgui;

TEST(ModelTorrent, ParseFull)
{
    const QJsonObject j = {
        {"id", 42},
        {"name", "ubuntu-24.04.iso"},
        {"status", 4},
        {"errorString", ""},
        {"announceResponse", "Success"},
        {"recheckProgress", 0.5},
        {"sizeWhenDone", 4600000000},
        {"totalSize", 5800000000},
        {"leftUntilDone", 1200000000},
        {"downloadedEver", 3400000000},
        {"uploadedEver", 900000000},
        {"uploadRatio", 0.2647},
        {"metadataPercentComplete", 1.0},
        {"rateDownload", 125000},
        {"rateUpload", 25000},
        {"peersSendingToUs", 3},
        {"peersGettingFromUs", 12},
        {"eta", 9600},
        {"addedDate", 1730000000},
        {"doneDate", 0},
        {"activityDate", 1730003600},
        {"secondsSeeding", 1000},
        {"secondsDownloading", 30000},
        {"bandwidthPriority", 1},
        {"queuePosition", 2},
        {"isPrivate", true},
        {"isFinished", false},
        {"isStalled", false},
        {"sequential_download", true},
        {"labels", QJsonArray({"linux", "iso"})},
        {"downloadDir", "/downloads"},
        {"trackerStats",
         QJsonArray({QJsonObject{{"announce", "http://tracker/announce"},
                                 {"announceState", 2},
                                 {"hasAnnounced", true},
                                 {"lastAnnounceSucceeded", true},
                                 {"lastAnnounceResult", "Success"},
                                 {"lastAnnounceTime", 1730003000},
                                 {"seederCount", 7},
                                 {"leecherCount", 31},
                                 {"torrentId", 42}})}},
    };
    Torrent t;
    ASSERT_TRUE(parseTorrent(j, t));
    EXPECT_EQ(t.id, 42);
    EXPECT_EQ(*t.name, "ubuntu-24.04.iso");
    EXPECT_EQ(*t.status, 4);
    EXPECT_DOUBLE_EQ(*t.sizeWhenDone, 4600000000);
    EXPECT_EQ(*t.rateDownload, 125000);
    EXPECT_TRUE(*t.isPrivate);
    EXPECT_TRUE(*t.sequentialDownload);
    ASSERT_EQ(t.labels->size(), 2);
    EXPECT_EQ(t.labels->at(1), "iso");
    EXPECT_EQ(*t.downloadDir, "/downloads");
    ASSERT_EQ(t.trackerStats.size(), 1);
    EXPECT_EQ(t.trackerStats.first().announce, "http://tracker/announce");
    EXPECT_EQ(t.trackerStats.first().seederCount, 7);
    EXPECT_DOUBLE_EQ(percentComplete(5800000000, 1200000000),
                     (5800000000.0 - 1200000000.0) * 100.0 / 5800000000.0);
}

TEST(ModelTorrent, OptionalFieldsAbsent)
{
    const QJsonObject j = {{"id", 1}, {"status", 0}};
    Torrent t;
    ASSERT_TRUE(parseTorrent(j, t));
    EXPECT_EQ(t.id, 1);
    EXPECT_TRUE(t.name == std::nullopt);
    EXPECT_TRUE(t.labels == std::nullopt);
    EXPECT_TRUE(t.sequentialDownload == std::nullopt);
    EXPECT_TRUE(t.trackerStats.isEmpty());
}

TEST(ModelTorrent, RequiresId)
{
    Torrent t;
    EXPECT_FALSE(parseTorrent(QJsonObject{{"name", "x"}}, t));
}

TEST(ModelTorrents, ParseArraySkipsNonObjects)
{
    QJsonArray arr;
    arr.append(QJsonObject{{"id", 1}});
    arr.append(QJsonValue("junk"));
    arr.append(QJsonObject{{"id", 2}});
    const QVector<Torrent> list = parseTorrents(arr);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list.last().id, 2);
}

TEST(ModelTorrentsTable, ParseTransguiTranslate)
{
    // Ответ torrent-get с format=table (rpc >= 16): [0] — поля, [1..] — строки.
    const QJsonObject body = {
        {"result", "success"},
        {"arguments",
         {{"torrents",
           QJsonArray({QJsonArray({"id", "name", "status", "sequential_download"}),
                        QJsonArray({1, "one", 4, true}),
                        QJsonArray({2, "two", 0, false}),
                        QJsonArray({3, "three", 6, true})})}}},
    };
    const QVector<Torrent> list = parseTorrentsTable(body.arguments());
    ASSERT_EQ(list.size(), 3);
    EXPECT_EQ(list.first().id, 1);
    EXPECT_EQ(*list.first().name, "one");
    EXPECT_EQ(*list.first().status, 4);
    EXPECT_TRUE(*list.first().sequentialDownload);
    EXPECT_FALSE(*list.at(1).sequentialDownload);
    EXPECT_EQ(list.last().id, 3);
}

TEST(ModelPeers, Parse)
{
    QJsonArray peers;
    peers.append(QJsonObject{
        {"address", "192.168.1.10"}, {"port", 6891},
        {"clientName", "transmission/4.1.0"}, {"flagStr", "EFH"},
        {"progress", 66.7}, {"rateToClient", 1000}, {"rateToPeer", 0},
    });
    peers.append(QJsonObject{{"address", "10.0.0.2"}});
    const QVector<Peer> list = parsePeers(peers);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list.first().address, "192.168.1.10");
    EXPECT_EQ(list.first().port, 6891);
    EXPECT_DOUBLE_EQ(list.first().progress, 66.7);
    EXPECT_EQ(list.first().rateToClient, 1000);
    EXPECT_TRUE(list.last().clientName.isEmpty());
    EXPECT_EQ(list.last().port, 0);
}

TEST(ModelFiles, ParseWithPrioritiesAndWanted)
{
    const QJsonObject torrent = {
        {"id", 5},
        {"downloadDir", "/downloads/ubuntu"},
        {"files", QJsonArray({QJsonObject{{"name", "dir/part1"}, {"size", 1000}},
                              QJsonObject{{"name", "dir/part2"}, {"size", 2000}},
                              QJsonObject{{"name", "dir/skip.bin"}, {"size", 3000}}})},
        {"priorities", QJsonArray({1, 0, 0})},
        {"wanted", QJsonArray({true, true, false})},
    };
    const QVector<FileStat> files = parseFiles(torrent);
    ASSERT_EQ(files.size(), 3);
    EXPECT_EQ(files.first().index, 0);
    EXPECT_EQ(files.first().name, "dir/part1");
    EXPECT_DOUBLE_EQ(files.first().size, 1000);
    EXPECT_EQ(files.first().priority, 1);
    EXPECT_TRUE(files.first().wanted);
    EXPECT_FALSE(files.last().wanted);
    EXPECT_EQ(files.last().priority, 0);
}

TEST(ModelSessionInfo, Parse)
{
    const QJsonObject j = {
        {"rpc-version", 18},
        {"version", "4.1.0"},
        {"download-dir", "/downloads"},
        {"alt-speed-enabled", true},
        {"alt-speed-down", 50},
        {"alt-speed-up", 20},
        {"alt-speed-time-enabled", true},
        {"alt-speed-time-begin", 2880},
        {"alt-speed-time-end", 1440},
        {"alt-speed-time-day", 127},
        {"speed-limit-down-enabled", false},
        {"speed-limit-down", 0},
        {"speed-limit-up-enabled", true},
        {"speed-limit-up", 100},
        {"blocklist-enabled", true},
        {"blocklist-url", "http://example.com/list.blist"},
        {"torrent-count", 3},
        {"dht-enabled", true},
        {"pex-enabled", true},
        {"lpd-enabled", true},
        {"utp-enabled", true},
        {"peer-port", 51413},
        {"start-added-torrents", false},
        {"queue-enabled", true},
        {"queue-size", 10},
        {"ratio-limit", 2.0},
        {"ratio-limit-enabled", true},
    };
    SessionInfo info;
    ASSERT_TRUE(parseSessionInfo(j, info));
    EXPECT_EQ(info.rpcVersion, 18);
    EXPECT_EQ(info.version, "4.1.0");
    EXPECT_EQ(info.downloadDir, "/downloads");
    EXPECT_TRUE(info.altSpeedEnabled);
    EXPECT_EQ(info.altSpeedDown, 50);
    EXPECT_TRUE(info.speedLimitUpEnabled);
    EXPECT_EQ(info.speedLimitUp, 100);
    EXPECT_FALSE(info.speedLimitDownEnabled);
    EXPECT_TRUE(info.blocklistEnabled);
    EXPECT_EQ(info.peerPort, 51413);
    EXPECT_FALSE(info.startAddedTorrents);
    EXPECT_TRUE(info.queueEnabled);
    EXPECT_DOUBLE_EQ(info.ratioLimit, 2.0);
    EXPECT_TRUE(info.downloadDirFreeSpace == std::nullopt);
}

TEST(ModelSessionStats, ParseValid)
{
    const QJsonObject j = {
        {"current-stats",
         {{"downloadedBytes", 1000}, {"uploadedBytes", 500},
          {"filesAdded", 3}, {"secondsActive", 120}}},
        {"cumulative-stats",
         {{"downloadedBytes", 9000}, {"uploadedBytes", 4500},
          {"filesAdded", 10}, {"secondsActive", 5000}}},
    };
    SessionStats stats;
    ASSERT_TRUE(parseSessionStats(j, stats));
    EXPECT_DOUBLE_EQ(stats.current.downloadedBytes, 1000);
    EXPECT_EQ(stats.current.filesAdded, 3);
    EXPECT_DOUBLE_EQ(stats.cumulative.uploadedBytes, 4500);
    EXPECT_EQ(stats.cumulative.secondsActive, 5000);
}

TEST(ModelSessionStats, RejectInvalid)
{
    // transgui FillStatistics: отрицательные/невалидные значения → invalid.
    QJsonObject bad = {
        {"current-stats",
         {{"downloadedBytes", -1}, {"uploadedBytes", 500},
          {"filesAdded", 3}, {"secondsActive", 120}}},
        {"cumulative-stats",
         {{"downloadedBytes", 1}, {"uploadedBytes", 500},
          {"filesAdded", 3}, {"secondsActive", 120}}},
    };
    EXPECT_FALSE(parseSessionStats(bad, SessionStats{}));

    bad["current-stats"] = QJsonValue(123); // не объект
    EXPECT_FALSE(parseSessionStats(bad, SessionStats{}));

    EXPECT_FALSE(parseSessionStats(QJsonObject{}, SessionStats{}));
}

TEST(ModelStatus, Helpers)
{
    EXPECT_TRUE(statusIsDownloading(4));
    EXPECT_FALSE(statusIsDownloading(6));
    EXPECT_TRUE(statusIsSeeding(6));
    EXPECT_TRUE(statusIsWaiting(1));
    EXPECT_TRUE(statusIsWaiting(2));
    EXPECT_TRUE(statusIsWaiting(3));
    EXPECT_FALSE(statusIsWaiting(4));
    EXPECT_TRUE(statusIsStopped(0));
    EXPECT_TRUE(statusIsFinished(6, 100, 0));
    EXPECT_FALSE(statusIsFinished(6, 0, 0)); // sizeWhenDone = 0 → stopped
    EXPECT_FALSE(statusIsFinished(6, 100, 5)); // осталось скачать → не finished
    EXPECT_TRUE(torrentIsActive(1, 0));
    EXPECT_TRUE(torrentIsActive(0, 1));
    EXPECT_FALSE(torrentIsActive(0, 0));
}