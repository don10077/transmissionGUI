// Unit-тесты моделей вкладок деталей (Файлы/Пиры/Трекеры).
#include <gtest/gtest.h>

#include <QTableView>
#include <QImage>
#include <QIcon>
#include <QTemporaryDir>

#include "detailmodels.h"
#include "torrentmodel.h"
#include "tgui/util.h"

using namespace tgui;

namespace {
QVector<FileStat> makeFiles()
{
    QVector<FileStat> files;
    FileStat a;
    a.index = 0;
    a.name = "dir/a.mp4";
    a.size = 1000;
    a.completion = 0.5;
    a.priority = 1;
    a.wanted = true;
    files.push_back(a);
    FileStat b;
    b.index = 1;
    b.name = "dir/b.txt";
    b.size = 2048;
    b.completion = 0.0;
    b.priority = 0;
    b.wanted = false;
    files.push_back(b);
    return files;
}
} // namespace

TEST(DetailFileModel, DataAndHeaders)
{
    FileListModel m;
    m.setFiles(makeFiles());
    ASSERT_EQ(m.rowCount(), 2);
    EXPECT_EQ(m.columnCount(), FileListModel::ColumnCount);
    EXPECT_EQ(m.data(m.index(0, FileListModel::ColName), Qt::DisplayRole).toString(),
              "dir/a.mp4");
    EXPECT_EQ(m.data(m.index(0, FileListModel::ColProgress), Qt::DisplayRole).toString(),
              "50%");
    EXPECT_EQ(m.data(m.index(0, FileListModel::ColWanted), Qt::CheckStateRole).toInt(),
              Qt::Checked);
    EXPECT_EQ(m.data(m.index(1, FileListModel::ColWanted), Qt::CheckStateRole).toInt(),
              Qt::Unchecked);
    EXPECT_EQ(m.headerData(FileListModel::ColWanted, Qt::Horizontal, Qt::DisplayRole)
                  .toString(),
              QStringLiteral("Wanted"));
}

TEST(DetailFileModel, ToggleWantEmits)
{
    FileListModel m;
    m.setFiles(makeFiles());

    bool emitted = false;
    int emittedIndex = -1;
    bool emittedWanted = true;
    QObject::connect(&m, &FileListModel::wantChanged,
                     [&](int i, bool w) { emitted = true; emittedIndex = i; emittedWanted = w; });

    // Строка 0: true → false.
    ASSERT_TRUE(m.setData(m.index(0, FileListModel::ColWanted),
                          static_cast<int>(Qt::Unchecked), Qt::CheckStateRole));
    EXPECT_TRUE(emitted);
    EXPECT_EQ(emittedIndex, 0);
    EXPECT_FALSE(emittedWanted);
    EXPECT_FALSE(m.wantedAt(0));
    EXPECT_EQ(m.data(m.index(0, FileListModel::ColWanted), Qt::CheckStateRole).toInt(),
              Qt::Unchecked);

    // Повторное одинаковое значение — без изменения.
    emitted = false;
    ASSERT_TRUE(m.setData(m.index(0, FileListModel::ColWanted),
                          static_cast<int>(Qt::Unchecked), Qt::CheckStateRole));
    EXPECT_FALSE(emitted);

    // Строка 1: false → true.
    emitted = false;
    ASSERT_TRUE(m.setData(m.index(1, FileListModel::ColWanted),
                          static_cast<int>(Qt::Checked), Qt::CheckStateRole));
    EXPECT_TRUE(emitted);
    EXPECT_EQ(emittedIndex, 1);
    EXPECT_TRUE(emittedWanted);
    EXPECT_TRUE(m.wantedAt(1));
}

TEST(DetailFileModel, WantedColumnEditableOnly)
{
    FileListModel m;
    m.setFiles(makeFiles());
    EXPECT_TRUE(m.flags(m.index(0, FileListModel::ColWanted)) & Qt::ItemIsUserCheckable);
    EXPECT_FALSE(m.flags(m.index(0, FileListModel::ColName)) & Qt::ItemIsUserCheckable);
    // Не-want-колонки не редактируются.
    EXPECT_FALSE(m.setData(m.index(0, FileListModel::ColName), QString("x"), Qt::EditRole));
}

TEST(DetailPeerModel, DataRendering)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer p;
    p.address = "10.0.0.5";
    p.port = 6891;
    p.clientName = "transmission/4.2.0";
    p.progress = 33.3;
    p.rateToClient = 2048;
    p.rateToPeer = 1024;
    peers.push_back(p);
    m.setPeers(peers);
    ASSERT_EQ(m.rowCount(), 1);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "10.0.0.5:6891");
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColClient), Qt::DisplayRole).toString(),
              "transmission/4.2.0");
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColProgress), Qt::DisplayRole).toString(),
              "33%");
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColRateDown), Qt::DisplayRole).toString(),
              QString::fromStdString(formatRate(2048)));
    // Без порта.
    QVector<Peer> p2;
    Peer q;
    q.address = "192.168.0.9";
    p2.push_back(q);
    m.setPeers(p2);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "192.168.0.9");
}

TEST(DetailPeerModel, CountryColumnWithoutGeoIp)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer p;
    p.address = "8.8.8.8";
    p.port = 0;
    p.clientName = "test/1.0";
    peers.push_back(p);
    m.setPeers(peers);
    ASSERT_EQ(m.rowCount(), 1);
    // Без GeoIP — пустая страна.
    EXPECT_TRUE(m.data(m.index(0, PeerListModel::ColCountry), Qt::DisplayRole).toString().isEmpty());
    // Заголовок.
    EXPECT_EQ(m.headerData(PeerListModel::ColCountry, Qt::Horizontal, Qt::DisplayRole).toString(),
              QStringLiteral("Country"));
}

TEST(DetailPeerModel, CountryColumnWithGeoIp)
{
    // Создаём минимальную GeoIP-базу: все указатели → country index 1 ("AP").
    // GeoIpDb читает указатели в little-endian (байт 0 = младший).
    GeoIpDb db;
    const auto size = GeoIpDb::kCountryBegin;
    std::vector<unsigned char> data(size, 0);
    // Корневой узел (offset 0): оба указателя = kCountryBegin + 1 (24 bit LE).
    const uint32_t countryOffset = static_cast<uint32_t>(GeoIpDb::kCountryBegin + 1);
    auto writeLE3 = [](unsigned char* dst, uint32_t val) {
        dst[0] = static_cast<unsigned char>(val & 0xFF);
        dst[1] = static_cast<unsigned char>((val >> 8) & 0xFF);
        dst[2] = static_cast<unsigned char>((val >> 16) & 0xFF);
    };
    writeLE3(data.data() + 0, countryOffset); // low pointer
    writeLE3(data.data() + 3, countryOffset); // high pointer
    ASSERT_TRUE(db.load(std::move(data)));
    EXPECT_TRUE(db.isLoaded());
    EXPECT_EQ(db.countryFor("1.2.3.4"), "AP");

    PeerListModel m;
    m.setGeoIp(&db);
    QVector<Peer> peers;
    Peer p;
    p.address = "1.2.3.4";
    p.port = 5000;
    peers.push_back(p);
    m.setPeers(peers);
    ASSERT_EQ(m.rowCount(), 1);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColCountry), Qt::DisplayRole).toString(),
              "Asia/Pacific Region");

    QTemporaryDir flagsDir;
    ASSERT_TRUE(flagsDir.isValid());
    QImage image(16, 11, QImage::Format_RGB32);
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(flagsDir.filePath("ap.png")));
    m.setFlagsDirectory(flagsDir.path());
    const QModelIndex country = m.index(0, PeerListModel::ColCountry);
    EXPECT_FALSE(m.data(country, Qt::DecorationRole).isValid());
    m.setShowFlags(true);
    const QIcon icon = m.data(country, Qt::DecorationRole).value<QIcon>();
    EXPECT_FALSE(icon.isNull());
    EXPECT_FALSE(icon.pixmap(16, 11).isNull());
    m.setShowFlags(false);
    EXPECT_FALSE(m.data(country, Qt::DecorationRole).isValid());
}

TEST(DetailPeerModel, CountryColumnInvalidIp)
{
    GeoIpDb db;
    const auto size = GeoIpDb::kCountryBegin;
    std::vector<unsigned char> data(size, 0);
    const uint32_t countryOffset = static_cast<uint32_t>(GeoIpDb::kCountryBegin + 1);
    auto writeLE3 = [](unsigned char* dst, uint32_t val) {
        dst[0] = static_cast<unsigned char>(val & 0xFF);
        dst[1] = static_cast<unsigned char>((val >> 8) & 0xFF);
        dst[2] = static_cast<unsigned char>((val >> 16) & 0xFF);
    };
    writeLE3(data.data() + 0, countryOffset);
    writeLE3(data.data() + 3, countryOffset);
    ASSERT_TRUE(db.load(std::move(data)));

    PeerListModel m;
    m.setGeoIp(&db);
    QVector<Peer> peers;
    Peer p;
    p.address = "not-a-valid-ip";
    p.port = 0;
    peers.push_back(p);
    m.setPeers(peers);
    // Невалидный IP → пустая страна.
    EXPECT_TRUE(m.data(m.index(0, PeerListModel::ColCountry), Qt::DisplayRole).toString().isEmpty());
}

TEST(DetailTrackerModel, DataRendering)
{
    TrackerListModel m;
    QVector<TrackerStat> ts;
    TrackerStat s;
    s.announce = "http://bt.t-ru.org/ann";
    s.tier = 0;
    s.seederCount = -1;
    s.leecherCount = 42;
    s.lastAnnounceResult = "Success";
    ts.push_back(s);
    m.setTrackers(ts);
    ASSERT_EQ(m.rowCount(), 1);
    EXPECT_EQ(m.data(m.index(0, TrackerListModel::ColAnnounce), Qt::DisplayRole).toString(),
              "http://bt.t-ru.org/ann");
    EXPECT_EQ(m.data(m.index(0, TrackerListModel::ColTier), Qt::DisplayRole).toInt(), 0);
    EXPECT_EQ(m.data(m.index(0, TrackerListModel::ColSeeders), Qt::DisplayRole).toString(),
              QStringLiteral("—"));
    EXPECT_EQ(m.data(m.index(0, TrackerListModel::ColLeechers), Qt::DisplayRole).toInt(), 42);
    EXPECT_EQ(m.data(m.index(0, TrackerListModel::ColLastResult), Qt::DisplayRole).toString(),
              "Success");
}

TEST(DetailModels, OffscreenTableViewRenders)
{
    // Регрессия: таблицы вкладок корректно отрисовывают данные offscreen.
    QTableView view;
    FileListModel fm;
    fm.setFiles(makeFiles());
    view.setModel(&fm);
    view.resize(800, 300);
    view.show();
    ASSERT_EQ(view.model()->rowCount(), 2);
    EXPECT_FALSE(view.model()->index(0, 0).data(Qt::DisplayRole).toString().isEmpty());
    view.hide();

    TrackerListModel tm;
    QVector<TrackerStat> ts;
    TrackerStat s;
    s.announce = "http://tracker.example/announce";
    ts.push_back(s);
    tm.setTrackers(ts);
    view.setModel(&tm);
    view.show();
    ASSERT_EQ(view.model()->rowCount(), 1);
    EXPECT_EQ(view.model()->index(0, 0).data(Qt::DisplayRole).toString(),
              "http://tracker.example/announce");
    view.hide();
}
TEST(FileTree, ShowsCheckProgressWhileVerifying)
{
    auto files = makeFiles();
    // a.mp4 проверен, b.txt проверен наполовину; completion у обоих — как
    // будто файлы уже скачаны (во время проверки демон отдаёт 100%).
    files[0].completion = 1.0;
    files[1].completion = 1.0;
    files[0].checkProgress = 1.0;
    files[1].checkProgress = 0.5;

    FileListModel list;
    list.setFiles(files);
    EXPECT_EQ(list.data(list.index(0, FileListModel::ColProgress), Qt::DisplayRole).toString(),
              "100.0%");
    EXPECT_EQ(list.data(list.index(1, FileListModel::ColProgress), Qt::DisplayRole).toString(),
              "50.0%");

    FileTreeModel model;
    model.setFiles(files);
    const auto folder = model.index(0, 0);
    EXPECT_EQ(folder.data().toString(), QStringLiteral("dir"));
    // Взвешенный по размеру прогресс проверки: (1000*1 + 2048*0.5) / 3048.
    EXPECT_NEAR(model.index(0, FileTreeModel::Progress).data().toDouble(),
                (1000.0 + 2048.0 * 0.5) * 100.0 / 3048.0, 0.01);
    // Колонка Done остаётся фактическим размером скачанного.
    EXPECT_EQ(model.index(0, FileTreeModel::Done).data().toString(),
              QString::fromStdString(formatSize(3048)));
}

TEST(FileTree, SingleFileVerificationMatchesTorrentWithoutPieceRanges)
{
    auto files = makeFiles().mid(0, 1);
    files[0].completion = 1.0;
    Torrent torrent;
    torrent.id = 7;
    torrent.status = static_cast<int>(TorrentStatus::Check);
    torrent.recheckProgress = 0.4321;
    TorrentModel torrents;
    torrents.setTorrents({torrent});

    applyCheckProgress(files, *torrent.status, torrent.recheckProgress, 0);
    FileTreeModel model;
    model.setFiles(files);
    const auto folder = model.index(0, 0);
    const auto torrentProgress = torrents.index(0, TorrentModel::ColDone)
                                     .data(TorrentModel::ProgressRole).toDouble();
    EXPECT_DOUBLE_EQ(model.index(0, FileTreeModel::Progress, folder).data().toDouble(),
                     torrentProgress);
    EXPECT_DOUBLE_EQ(model.index(0, FileTreeModel::Progress).data().toDouble(), torrentProgress);

    applyCheckProgress(files, static_cast<int>(TorrentStatus::Stopped), std::nullopt, 0);
    model.setFiles(files);
    EXPECT_DOUBLE_EQ(model.index(0, FileTreeModel::Progress, folder).data().toDouble(), 100.0);
}

TEST(FileTree, AggregatesFoldersAndPreservesItemsOnRefresh)
{
    FileTreeModel model;
    auto files = makeFiles();
    model.setFiles(files);
    ASSERT_EQ(model.rowCount(), 1);
    const auto folder = model.index(0, 0);
    EXPECT_EQ(folder.data().toString(), QStringLiteral("dir"));
    EXPECT_EQ(model.rowCount(folder), 2);
    EXPECT_EQ(model.sourceRows(folder), (QVector<int>{0, 1}));
    EXPECT_EQ(model.index(0, FileTreeModel::Done).data().toString(),
              QString::fromStdString(formatSize(500)));
    QPersistentModelIndex leaf(model.index(0, 0, folder));
    files[0].completion = 1.;
    model.setFiles(files);
    EXPECT_TRUE(leaf.isValid());
    EXPECT_EQ(leaf.data().toString(), QStringLiteral("a.mp4"));
    EXPECT_EQ(model.sourceRows(leaf), (QVector<int>{0}));
    EXPECT_DOUBLE_EQ(model.index(0, FileTreeModel::Progress, folder).data().toDouble(), 100.);
    model.setFiles({});
    EXPECT_FALSE(leaf.isValid());
    EXPECT_EQ(model.rowCount(), 0);
}

// W11: Resolve Host — reverse DNS: кеш + показ hostname / IP при ошибке.
TEST(DetailPeerModel, ResolveHostShowsCachedHostname)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer p;
    p.address = "1.2.3.4";
    p.port = 5678;
    peers.push_back(p);
    m.setPeers(peers);
    m.setResolveHost(true);
    // Без кеша — IP:порт.
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "1.2.3.4:5678");
    // Кеш hostname — показываем hostname:порт.
    m.setResolvedHost("1.2.3.4", "host.example.net");
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "host.example.net:5678");
}

TEST(DetailPeerModel, ResolveHostOffKeepsIp)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer p;
    p.address = "1.2.3.4";
    p.port = 5678;
    peers.push_back(p);
    m.setPeers(peers);
    m.setResolvedHost("1.2.3.4", "host.example.net");
    // Resolve Host выключен по умолчанию — показываем IP.
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "1.2.3.4:5678");
}

TEST(DetailPeerModel, ResolveHostFailedLookupKeepsIp)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer p;
    p.address = "1.2.3.4";
    p.port = 0;
    peers.push_back(p);
    m.setPeers(peers);
    m.setResolveHost(true);
    // Ошибка reverse DNS (пустой host) — остаётся IP.
    m.setResolvedHost("1.2.3.4", QString());
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "1.2.3.4");
}
