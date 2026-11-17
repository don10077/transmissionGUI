#include <gtest/gtest.h>

#include "tgui/models.h"
#include "tgui/util.h"

using namespace tgui;

TEST(ModelTorrent, ParseFull)
{
    const QJsonObject j = {
        {"id", 42},
        {"name", "ubuntu-24.04.iso"},
        {"status", 4},
        {"errorString", ""},
        {"recheckProgress", 0.5},
        {"sizeWhenDone", qint64{4600000000}},
        {"totalSize", qint64{5800000000}},
        {"leftUntilDone", 1200000000},
        {"downloadedEver", qint64{3400000000}},
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
        {"labels", QJsonArray{"linux", "iso"}},
        {"downloadDir", "/downloads"},
        {"trackerStats",
         QJsonArray{QJsonObject{{"announce", "http://tracker/announce"},
                                {"announceState", 2},
                                {"hasAnnounced", true},
                                {"lastAnnounceSucceeded", true},
                                {"lastAnnounceResult", "Success"},
                                {"lastAnnounceTime", 1730003000},
                                {"seederCount", 7},
                                {"leecherCount", 31},
                                {"torrentId", 42}}}},
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

TEST(ModelTorrentsTable, ParseTable)
{
    // Ответ torrent-get с format=table (rpc >= 16): [0] — поля, [1..] — строки.
    const QJsonObject body = {
        {"result", "success"},
        {"arguments",
         QJsonObject{{"torrents",
           QJsonArray{QJsonArray{"id", 123, "name", "status", "sequential_download"},
                        QJsonArray{1, "ignored", "one", 4, true},
                        QJsonArray{2, "ignored", "two", 0, false},
                        QJsonArray{3, "ignored", "three", 6, true}}}}},
    };
    const QVector<Torrent> list = parseTorrentsTable(body.value("arguments").toObject());
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
        {"files", QJsonArray{QJsonObject{{"name", "dir/part1"}, {"length", 1000}},
                             QJsonObject{{"name", "dir/part2"}, {"length", 2000}},
                             QJsonObject{{"name", "dir/skip.bin"}, {"length", 3000}}}},
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

TEST(ModelPeers, ParseKebabCase)
{
    // Нативный формат Transmission 4.x (kebab-case).
    QJsonArray peers;
    peers.append(QJsonObject{
        {"ip", "10.1.2.3"}, {"port", 51413},
        {"client-name", "transmission/4.2.0"}, {"flag-str", "EFH"},
        {"progress", 12.5}, {"rate-to-client", 2048}, {"rate-to-peer", 4096},
    });
    const QVector<Peer> list = parsePeers(peers);
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list.first().address, "10.1.2.3");
    EXPECT_EQ(list.first().port, 51413);
    EXPECT_EQ(list.first().clientName, "transmission/4.2.0");
    EXPECT_EQ(list.first().flagStr, "EFH");
    EXPECT_DOUBLE_EQ(list.first().progress, 12.5);
    EXPECT_EQ(list.first().rateToClient, 2048);
    EXPECT_EQ(list.first().rateToPeer, 4096);
}

TEST(ModelFiles, ParseCompletion)
{
    const QJsonObject torrent = {
        {"id", 7},
        {"files",
         QJsonArray{QJsonObject{{"name", "a"}, {"length", 1000}, {"completed", 0.5}},
                    QJsonObject{{"name", "b"}, {"length", 2000}, {"bytesCompleted", 500}},
                    QJsonObject{{"name", "c"}, {"length", 4000}, {"bytes_completed", 800}}}},
    };
    const QVector<FileStat> files = parseFiles(torrent);
    ASSERT_EQ(files.size(), 3);
    EXPECT_DOUBLE_EQ(files.first().completion, 0.5);
    EXPECT_DOUBLE_EQ(files.at(1).completion, 0.25);
    EXPECT_DOUBLE_EQ(files.last().completion, 0.2);
}

TEST(ModelTrackerStats, ParseWithTier)
{
    QJsonArray stats;
    stats.append(QJsonObject{
        {"announce", "http://bt.t-ru.org/ann"}, {"announceState", 0},
        {"hasAnnounced", true}, {"lastAnnounceSucceeded", true},
        {"lastAnnounceResult", "Success"}, {"lastAnnounceTime", 1792807908},
        {"seederCount", -1}, {"leecherCount", -1}, {"tier", 0}, {"torrentId", 1},
    });
    stats.append(QJsonObject{
        {"announce", "http://retracker.local/announce"}, {"tier", 1},
        {"lastAnnounceResult", "Could not connect to tracker"},
    });
    const QVector<TrackerStat> list = parseTrackerStats(stats);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list.first().announce, "http://bt.t-ru.org/ann");
    EXPECT_EQ(list.first().tier, 0);
    EXPECT_EQ(list.first().seederCount, -1);
    EXPECT_TRUE(list.first().lastAnnounceSucceeded);
    EXPECT_EQ(list.last().tier, 1);
    EXPECT_EQ(list.last().lastAnnounceResult, "Could not connect to tracker");
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

TEST(ModelSessionInfo, ParseLegacySeedRatioAndExtendedFields)
{
    const QJsonObject j = {
        {"rpc-version", 14},
        {"version", "4.0.6"},
        {"seedRatioLimit", 1.75},
        {"seedRatioLimited", true},
        {"download-queue-enabled", true},
        {"download-queue-size", 5},
        {"seed-queue-enabled", true},
        {"seed-queue-size", 8},
        {"queue-stalled-enabled", true},
        {"queue-stalled-minutes", 25},
        {"incomplete-dir", "/incomplete"},
        {"incomplete-dir-enabled", true},
        {"rename-partial-files", true},
        {"idle-seeding-limit-enabled", true},
        {"idle-seeding-limit", 90},
        {"encryption", "tolerated"},
        {"peer-limit-global", 240},
        {"peer-limit-per-torrent", 60},
        {"port-forwarding-enabled", true},
        {"peer-port-random-on-start", true},
        {"cache-size-mb", 96},
    };
    SessionInfo info;
    ASSERT_TRUE(parseSessionInfo(j, info));
    EXPECT_DOUBLE_EQ(info.ratioLimit, 1.75);
    EXPECT_TRUE(info.ratioLimitEnabled);
    EXPECT_TRUE(info.downloadQueueEnabled);
    EXPECT_EQ(info.downloadQueueSize, 5);
    EXPECT_TRUE(info.queueEnabled); // совместимость
    EXPECT_EQ(info.queueSize, 5);
    EXPECT_TRUE(info.seedQueueEnabled);
    EXPECT_EQ(info.seedQueueSize, 8);
    EXPECT_TRUE(info.queueStalledEnabled);
    EXPECT_EQ(info.queueStalledMinutes, 25);
    EXPECT_EQ(info.incompleteDir, "/incomplete");
    EXPECT_TRUE(info.incompleteDirEnabled);
    EXPECT_TRUE(info.renamePartialFiles);
    EXPECT_TRUE(info.idleSeedingLimitEnabled);
    EXPECT_EQ(info.idleSeedingLimit, 90);
    EXPECT_EQ(info.encryption, "tolerated");
    EXPECT_EQ(info.peerLimitGlobal, 240);
    EXPECT_EQ(info.peerLimitPerTorrent, 60);
    EXPECT_TRUE(info.portForwardingEnabled);
    EXPECT_TRUE(info.peerPortRandomOnStart);
    EXPECT_EQ(info.cacheSizeMb, 96);
    EXPECT_TRUE(info.hasCacheSize);
}

TEST(ModelSessionInfo, NormalizesModernEncryptionAllowed)
{
    const QJsonObject j = {{"rpc-version", 19}, {"version", "6.0.0"}, {"encryption", "allowed"}};
    SessionInfo info;
    ASSERT_TRUE(parseSessionInfo(j, info));
    EXPECT_EQ(info.encryption, "tolerated");
    EXPECT_FALSE(info.hasCacheSize);
}

TEST(ModelSessionStats, ParseValid)
{
    const QJsonObject j = {
        {"current-stats",
         QJsonObject{{"downloadedBytes", 1000}, {"uploadedBytes", 500},
                     {"filesAdded", 3}, {"secondsActive", 120}}},
        {"cumulative-stats",
         QJsonObject{{"downloadedBytes", 9000}, {"uploadedBytes", 4500},
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
    // session-stats: отрицательные/невалидные значения → invalid.
    QJsonObject bad = {
        {"current-stats",
         QJsonObject{{"downloadedBytes", -1}, {"uploadedBytes", 500},
                     {"filesAdded", 3}, {"secondsActive", 120}}},
        {"cumulative-stats",
         QJsonObject{{"downloadedBytes", 1}, {"uploadedBytes", 500},
                     {"filesAdded", 3}, {"secondsActive", 120}}},
    };
    SessionStats stats;
    EXPECT_FALSE(parseSessionStats(bad, stats));

    bad["current-stats"] = QJsonValue(123); // не объект
    EXPECT_FALSE(parseSessionStats(bad, stats));

    EXPECT_FALSE(parseSessionStats(QJsonObject{}, stats));
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

#include <QCoreApplication>
#include <QAbstractItemModelTester>
#include <QVector>

#include "torrentmodel.h"

TEST(TorrentModel, IncrementalRowsKeepPersistentIndexes)
{
    TorrentModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    tgui::Torrent removed;
    removed.id = 1;
    tgui::Torrent retained;
    retained.id = 2;
    retained.name = "retained";
    tgui::Torrent inserted;
    inserted.id = 3;
    int resets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&] { ++resets; });
    ASSERT_TRUE(model.setTorrents({removed, retained}));
    const QPersistentModelIndex index = model.index(1, TorrentModel::ColName);
    ASSERT_TRUE(model.setTorrents({inserted, retained}));
    EXPECT_TRUE(index.isValid());
    EXPECT_EQ(index.row(), 0);
    EXPECT_EQ(index.data().toString(), QStringLiteral("retained"));
    EXPECT_EQ(model.rowForId(1), -1);
    EXPECT_EQ(model.rowForId(3), 1);
    retained.name = "renamed";
    EXPECT_FALSE(model.setTorrents({retained, inserted}));
    EXPECT_EQ(index.data().toString(), QStringLiteral("renamed"));
    EXPECT_EQ(resets, 0);
}

namespace {

tgui::Torrent makeUiTorrent(int id, const QString& dir, const QString& name, int status = 0)
{
    tgui::Torrent t;
    t.id = id;
    t.name = name;
    t.status = status;
    t.downloadDir = dir;
    t.sizeWhenDone = 100.0;
    t.leftUntilDone = 50.0;
    return t;
}

} // namespace

TEST(GuiListChain, ListVisibleAfterModelReset)
{
    TorrentModel model;
    TorrentFilterProxy filter;
    filter.setSourceModel(&model);

    // До получения списка — пусто.
    EXPECT_EQ(filter.rowCount(), 0);

    // Имитация получения списка из демона: полный сброс модели.
    QVector<tgui::Torrent> torrents = {
        makeUiTorrent(1, "/Shares/A", "alpha", 0),
        makeUiTorrent(2, "/Shares/B", "beta", 6),
        makeUiTorrent(3, "/Shares/A", "gamma", 4),
    };
    model.setTorrents(torrents);
    qApp->processEvents();

    EXPECT_EQ(model.rowCount(), 3);
    EXPECT_EQ(filter.rowCount(), 3)
        << "прокси не видит строки после modelReset → таблица пустая";

    // Данные проходят через цепочку.
    const QModelIndex idx = filter.index(0, TorrentModel::ColName, QModelIndex());
    EXPECT_TRUE(idx.isValid());
    EXPECT_FALSE(idx.data(Qt::DisplayRole).toString().isEmpty());

    // Обновление списка (автообновление) тоже отражается.
    torrents.append(makeUiTorrent(4, "/Shares/C", "delta"));
    model.setTorrents(torrents);
    qApp->processEvents();
    EXPECT_EQ(filter.rowCount(), 4);
}

#include <QPixmap>
#include <QTreeView>

// Рендер QTreeView с полной цепочкой (regress: пустая таблица после подключения).
// Известные грабли: hasChildren(root) == false ломает QTreeView::layout(),
// поэтому у плоского прокси корень обязан иметь строки.
TEST(GuiListChain, OffscreenTreeViewRendersRows)
{
    TorrentModel model;
    TorrentFilterProxy filter;
    filter.setSourceModel(&model);

    QTreeView view;
    view.setModel(&filter);
    view.setSortingEnabled(true); // как в приложении
    view.setRootIsDecorated(false);
    view.resize(900, 400);
    view.show();
    qApp->processEvents();

    QVector<tgui::Torrent> torrents = {
        makeUiTorrent(1, "/A", "alpha", 0),
        makeUiTorrent(2, "/B", "beta", 6),
        makeUiTorrent(3, "/A", "gamma", 4),
    };
    model.setTorrents(torrents);
    qApp->processEvents();

    EXPECT_EQ(filter.rowCount(), 3) << "прокси не видит строки после modelReset";
    const QModelIndex first = filter.index(0, TorrentModel::ColName, QModelIndex());
    EXPECT_TRUE(first.isValid());
    EXPECT_FALSE(filter.data(first, Qt::DisplayRole).toString().isEmpty());
    EXPECT_TRUE(filter.hasChildren(QModelIndex())) << "у корня должны быть строки";
    EXPECT_FALSE(filter.hasChildren(first)) << "строки не имеют детей";
    EXPECT_EQ(filter.headerData(0, Qt::Horizontal, Qt::DisplayRole).toString(),
              QObject::tr("Name"));

    view.viewport()->update();
    qApp->processEvents();
    const QPixmap pm = view.grab();
    EXPECT_TRUE(pm.width() > 0 && pm.height() > 0);
}

TEST(GuiListChain, SortBySizeNumeric)
{
    TorrentModel model;
    TorrentFilterProxy filter;
    filter.setSourceModel(&model);
    filter.setDynamicSortFilter(true);

    QVector<tgui::Torrent> torrents;
    // 500 KB, 9 MB, 10 GB — как строки: "9 MB" > "10 GB" в лексикографическом порядке
    tgui::Torrent t1; t1.id = 1; t1.name = "small"; t1.totalSize = 512000.0;
    tgui::Torrent t2; t2.id = 2; t2.name = "medium"; t2.totalSize = 9437184.0;
    tgui::Torrent t3; t3.id = 3; t3.name = "large"; t3.totalSize = 10737418240.0;
    torrents = {t3, t1, t2}; // намеренно не отсортированы
    model.setTorrents(torrents);
    qApp->processEvents();

    // Сортируем по размеру — числовое сравнение
    filter.sort(TorrentModel::ColSize, Qt::AscendingOrder);
    qApp->processEvents();

    // Ожидаемый порядок: 500 KB (id=1), 9 MB (id=2), 10 GB (id=3)
    ASSERT_EQ(filter.rowCount(), 3);
    EXPECT_EQ(filter.torrentAt(0).id, 1) << "500 KB должен быть первым";
    EXPECT_EQ(filter.torrentAt(1).id, 2) << "9 MB должен быть вторым";
    EXPECT_EQ(filter.torrentAt(2).id, 3) << "10 GB должен быть третьим";
}

TEST(GuiListChain, SortByEtaNumeric)
{
    TorrentModel model;
    TorrentFilterProxy filter;
    filter.setSourceModel(&model);
    filter.setDynamicSortFilter(true);

    tgui::Torrent t1; t1.id = 1; t1.name = "no-eta"; t1.eta = -1;
    tgui::Torrent t2; t2.id = 2; t2.name = "short"; t2.eta = 30;
    tgui::Torrent t3; t3.id = 3; t3.name = "long"; t3.eta = 3600;
    model.setTorrents({t3, t1, t2});
    qApp->processEvents();

    filter.sort(TorrentModel::ColEta, Qt::AscendingOrder);
    qApp->processEvents();

    ASSERT_EQ(filter.rowCount(), 3);
    EXPECT_EQ(filter.torrentAt(0).id, 2) << "30s должен быть первым";
    EXPECT_EQ(filter.torrentAt(1).id, 3) << "3600s должен быть вторым";
    EXPECT_EQ(filter.torrentAt(2).id, 1) << "no-eta должен быть последним";

    filter.sort(TorrentModel::ColEta, Qt::DescendingOrder);
    EXPECT_EQ(filter.torrentAt(0).id, 3);
    EXPECT_EQ(filter.torrentAt(1).id, 2);
    EXPECT_EQ(filter.torrentAt(2).id, 1);
    EXPECT_EQ(filter.index(2, TorrentModel::ColEta).data().toString(), QStringLiteral("—"));

    t1.eta = -2;
    model.setTorrents({t3, t1, t2});
    EXPECT_EQ(filter.index(2, TorrentModel::ColEta).data().toString(), QStringLiteral("∞"));
}

TEST(GuiListChain, SortByAddedDateChronologically)
{
    TorrentModel model;
    TorrentFilterProxy filter;
    filter.setSourceModel(&model);

    tgui::Torrent december;
    december.id = 1;
    december.addedDate = 1735603200; // 31.12.2024 UTC
    tgui::Torrent january;
    january.id = 2;
    january.addedDate = 1735776000; // 02.01.2025 UTC
    model.setTorrents({january, december});

    filter.sort(TorrentModel::ColAddedOn, Qt::AscendingOrder);
    ASSERT_EQ(filter.rowCount(), 2);
    EXPECT_EQ(filter.torrentAt(0).id, december.id);
    EXPECT_EQ(filter.torrentAt(1).id, january.id);
}

TEST(GuiListChain, PrivateColumnDistinguishesPublicAndUnknown)
{
    TorrentModel model;
    tgui::Torrent publicTorrent;
    publicTorrent.id = 1;
    publicTorrent.isPrivate = false;
    tgui::Torrent privateTorrent;
    privateTorrent.id = 2;
    privateTorrent.isPrivate = true;
    tgui::Torrent unknown;
    unknown.id = 3;
    model.setTorrents({publicTorrent, privateTorrent, unknown});

    EXPECT_EQ(model.index(0, TorrentModel::ColPrivate).data().toString(), QStringLiteral("no"));
    EXPECT_EQ(model.index(1, TorrentModel::ColPrivate).data().toString(), QStringLiteral("yes"));
    EXPECT_EQ(model.index(2, TorrentModel::ColPrivate).data().toString(), QStringLiteral("—"));
}

// TST-03: sortKey по всем колонкам — у «меньшего» торрента ключ строго меньше (регрессия BUG-04).
TEST(TorrentModelSort, SortKeyOrdersEveryColumn)
{
    using tgui::Torrent;
    Torrent small;
    small.id = 1;
    small.name = "a";
    small.status = 4; // Downloading < Seeding
    small.sizeWhenDone = 100.0;
    small.totalSize = 100.0;
    small.leftUntilDone = 99.0;
    small.peersSendingToUs = 1;
    small.peersGettingFromUs = 1;
    small.rateDownload = 1;
    small.rateUpload = 1;
    small.eta = 45;
    small.downloadedEver = 1.0;
    small.uploadedEver = 1.0;
    small.addedDate = 100;
    small.doneDate = 200;
    small.activityDate = 300;
    small.downloadDir = "/a";
    small.bandwidthPriority = 0;
    small.queuePosition = 0;
    small.secondsSeeding = 0;
    small.isPrivate = false;
    tgui::TrackerStat smallTracker;
    smallTracker.announce = "https://a.example/announce";
    smallTracker.hasAnnounced = true;
    smallTracker.lastAnnounceResult = "A";
    small.trackerStats = {smallTracker};

    Torrent large;
    large.id = 2;
    large.name = "b";
    large.status = 6;
    large.sizeWhenDone = 1000.0;
    large.totalSize = 1000.0;
    large.leftUntilDone = 100.0;
    large.peersSendingToUs = 2;
    large.peersGettingFromUs = 2;
    large.rateDownload = 2;
    large.rateUpload = 2;
    large.eta = 90;
    large.uploadRatio = 1.5;
    large.downloadedEver = 2.0;
    large.uploadedEver = 2.0;
    tgui::TrackerStat tracker;
    tracker.announce = "https://b.example/announce";
    tracker.hasAnnounced = true;
    tracker.lastAnnounceSucceeded = true;
    large.trackerStats = {tracker};
    large.addedDate = 200;
    large.doneDate = 300;
    large.activityDate = 400;
    large.downloadDir = "/b";
    large.bandwidthPriority = 1;
    large.queuePosition = 1;
    large.secondsSeeding = 1;
    large.isPrivate = true;
    large.labels = QStringList{"x"};

    for (int column = 0; column < TorrentModel::ColumnCount; ++column) {
        const QVariant keySmall = TorrentModel::sortKey(small, column);
        const QVariant keyLarge = TorrentModel::sortKey(large, column);
        // QVariant не имеет operator< — сравниваем по типу (строка/число).
        const bool ordered = keySmall.typeId() == QMetaType::QString && keyLarge.typeId() == QMetaType::QString
            ? keySmall.toString() < keyLarge.toString()
            : keySmall.toDouble() < keyLarge.toDouble();
        EXPECT_TRUE(ordered) << "колонка " << column;
    }
}
