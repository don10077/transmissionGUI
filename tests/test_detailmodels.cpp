// Unit-тесты моделей вкладок деталей (Файлы/Пиры/Трекеры).
#include <gtest/gtest.h>

#include <QApplication>
#include <QTableView>
#include <QImage>
#include <QIcon>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTimer>
#include <QTreeView>
#include <QAbstractItemModelTester>

#include <functional>

#include "delegates.h"
#include "detailmodels.h"
#include "flagarchive.h"
#include "torrentmodel.h"
#include "tgui/util.h"

using namespace tgui;

TEST(FlagArchive, PublishesValidFlagsAndRejectsPathsOutsideDestination)
{
    QImage image(16, 11, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer data(&png);
    ASSERT_TRUE(data.open(QIODevice::WriteOnly));
    ASSERT_TRUE(image.save(&data, "PNG"));
    const auto makeZip = [&](const char* name) -> QByteArray {
        QByteArray bytes(4096, Qt::Uninitialized);
        size_t size = 0;
        std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        if (!writer || !entry || archive_write_set_format_zip(writer.get()) != ARCHIVE_OK ||
            archive_write_open_memory(writer.get(), bytes.data(), static_cast<size_t>(bytes.size()), &size) != ARCHIVE_OK)
            return {};
        archive_entry_set_pathname(entry.get(), name);
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_size(entry.get(), png.size());
        if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK ||
            archive_write_data(writer.get(), png.constData(), static_cast<size_t>(png.size())) != png.size() ||
            archive_write_close(writer.get()) != ARCHIVE_OK)
            return {};
        bytes.resize(static_cast<qsizetype>(size));
        return bytes;
    };

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto valid = makeZip("us.png");
    ASSERT_FALSE(valid.isEmpty());
    ASSERT_TRUE(flagarchive::extract(valid, directory.filePath("flags")));
    EXPECT_FALSE(QImage(directory.filePath("flags/us.png")).isNull());
    const auto invalid = makeZip("../us.png");
    ASSERT_FALSE(invalid.isEmpty());
    EXPECT_FALSE(flagarchive::extract(invalid, directory.filePath("invalid")));
    EXPECT_FALSE(QFileInfo::exists(directory.filePath("invalid")));
    EXPECT_FALSE(QFileInfo::exists(directory.filePath("us.png")));
}

TEST(FlagArchive, ReplacesNonEmptyExistingDirectory)
{
    // Пустой rmdir не удаляет каталог с «осколками»: extract должен снести
    // старую директорию и извлечь заново (ARCH-08).
    QImage image(16, 11, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer data(&png);
    ASSERT_TRUE(data.open(QIODevice::WriteOnly));
    ASSERT_TRUE(image.save(&data, "PNG"));
    auto makeZip = [](const char* name, const QByteArray& payload) -> QByteArray {
        QByteArray bytes(4096, Qt::Uninitialized);
        size_t size = 0;
        std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), archive_entry_free);
        if (!writer || !entry || archive_write_set_format_zip(writer.get()) != ARCHIVE_OK ||
            archive_write_open_memory(writer.get(), bytes.data(), static_cast<size_t>(bytes.size()), &size) != ARCHIVE_OK)
            return {};
        archive_entry_set_pathname(entry.get(), name);
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_size(entry.get(), static_cast<size_t>(payload.size()));
        if (archive_write_header(writer.get(), entry.get()) != ARCHIVE_OK ||
            archive_write_data(writer.get(), payload.constData(), static_cast<size_t>(payload.size())) != payload.size() ||
            archive_write_close(writer.get()) != ARCHIVE_OK)
            return {};
        bytes.resize(static_cast<qsizetype>(size));
        return bytes;
    };

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString destination = directory.filePath("flags");
    ASSERT_TRUE(QDir().mkpath(destination));
    {
        QFile stray(QDir(destination).filePath("stray.txt"));
        ASSERT_TRUE(stray.open(QIODevice::WriteOnly));
        stray.write("junk");
    }
    const auto valid = makeZip("us.png", png);
    ASSERT_FALSE(valid.isEmpty());
    ASSERT_TRUE(flagarchive::extract(valid, destination));
    EXPECT_FALSE(QImage(QDir(destination).filePath("us.png")).isNull());
    EXPECT_FALSE(QFile::exists(QDir(destination).filePath("stray.txt")));
    // Временно переименованный каталог не должен остаться в parent.
    const auto leftovers = QDir(directory.path()).entryList({QStringLiteral("flags-old-*")}, QDir::AllEntries);
    EXPECT_TRUE(leftovers.isEmpty());
}

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

TEST(DetailPeerModel, SortingByProgressAndClient)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer a, b, c;
    a.address = "10.0.0.1"; a.clientName = "qBittorrent/4.6"; a.progress = 80;
    b.address = "10.0.0.2"; b.clientName = "Transmission/4.0"; b.progress = 10;
    c.address = "10.0.0.3"; c.clientName = "aria2/1.36";     c.progress = 45;
    peers << a << b << c;
    m.setPeers(peers);

    m.sort(PeerListModel::ColProgress, Qt::AscendingOrder);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.2");
    EXPECT_EQ(m.data(m.index(1, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.3");
    EXPECT_EQ(m.data(m.index(2, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.1");

    m.sort(PeerListModel::ColProgress, Qt::DescendingOrder);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.1");

    // Регистронезависимая сортировка по клиенту.
    m.sort(PeerListModel::ColClient, Qt::AscendingOrder);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColClient), Qt::DisplayRole).toString(), "aria2/1.36");
    EXPECT_EQ(m.data(m.index(1, PeerListModel::ColClient), Qt::DisplayRole).toString(), "qBittorrent/4.6");
    EXPECT_EQ(m.data(m.index(2, PeerListModel::ColClient), Qt::DisplayRole).toString(), "Transmission/4.0");
}

TEST(DetailPeerModel, SortingSurvivesPeersRefresh)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer a, b;
    a.address = "10.0.0.1"; a.progress = 80;
    b.address = "10.0.0.2"; b.progress = 10;
    peers << a << b;
    m.setPeers(peers);
    m.sort(PeerListModel::ColRateDown, Qt::AscendingOrder); // обе скорости 0 — порядок стабилен
    m.sort(PeerListModel::ColProgress, Qt::DescendingOrder);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.1");
    // Обновление списка пиров (periodic refresh) сохраняет выбранный порядок.
    m.setPeers(peers);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.1");
    EXPECT_EQ(m.data(m.index(1, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.2");
    // Без сортировки — как прислал демон.
    PeerListModel fresh;
    fresh.setPeers(peers);
    EXPECT_EQ(fresh.data(fresh.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(), "10.0.0.1");
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
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColCountry), Qt::DisplayRole).toString(), QStringLiteral("—"));
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
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColCountry), Qt::DisplayRole).toString(), QStringLiteral("—"));
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
    EXPECT_EQ(model.index(0, FileTreeModel::Name).data().toString(), QStringLiteral("dir"));
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
    EXPECT_EQ(model.index(0, FileTreeModel::Name).data().toString(), QStringLiteral("dir"));
    EXPECT_EQ(model.rowCount(folder), 2);
    EXPECT_EQ(model.sourceRows(folder), (QVector<int>{0, 1}));
    EXPECT_EQ(model.index(0, FileTreeModel::Done).data().toString(),
              QString::fromStdString(formatSize(500)));
    QPersistentModelIndex leaf(model.index(0, FileTreeModel::Name, folder));
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

// G02: чекбокс Want — отображение состояния, каскад по папке, сигнал wantToggled.
TEST(FileTree, WantCheckboxTogglesAndCascades)
{
    FileTreeModel model;
    model.setFiles(makeFiles()); // dir/a.mp4 (wanted), dir/b.txt (skip)

    const auto colWant = FileTreeModel::Want;
    // Смешанная папка (один wanted, один skip) → PartiallyChecked.
    EXPECT_EQ(model.index(0, colWant).data(Qt::CheckStateRole).toInt(),
              Qt::PartiallyChecked);
    // Листы: a.mp4 → Checked, b.txt → Unchecked.
    const auto a = model.index(0, colWant, model.index(0, 0));
    const auto b = model.index(1, colWant, model.index(0, 0));
    EXPECT_EQ(a.data(Qt::CheckStateRole).toInt(), Qt::Checked);
    EXPECT_EQ(b.data(Qt::CheckStateRole).toInt(), Qt::Unchecked);
    // У Want-ячеек флаг редактируемости (чекбокс кликабелен).
    EXPECT_TRUE(model.flags(a) & Qt::ItemIsUserCheckable);

    QVector<int> rows;
    bool wanted = false;
    int count = 0;
    QObject::connect(&model, &FileTreeModel::wantToggled,
                     [&](const QVector<int>& r, bool w) { rows = r; wanted = w; ++count; });

    // Включаем b.txt (unchecked → checked): сигнал с одной строкой, папка → Checked.
    model.toggleWant(b);
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(wanted);
    EXPECT_EQ(rows, (QVector<int>{1}));
    EXPECT_EQ(b.data(Qt::CheckStateRole).toInt(), Qt::Checked);
    EXPECT_EQ(model.index(0, colWant).data(Qt::CheckStateRole).toInt(), Qt::Checked);

    // Каскад: папка (checked → unchecked) → оба файла unchecked, сигнал со всеми.
    model.toggleWant(model.index(0, colWant));
    EXPECT_EQ(count, 2);
    EXPECT_FALSE(wanted);
    EXPECT_EQ(rows, (QVector<int>{0, 1}));
    EXPECT_EQ(a.data(Qt::CheckStateRole).toInt(), Qt::Unchecked);
    EXPECT_EQ(b.data(Qt::CheckStateRole).toInt(), Qt::Unchecked);
    EXPECT_EQ(model.index(0, colWant).data(Qt::CheckStateRole).toInt(), Qt::Unchecked);
}

TEST(FileTree, WantClickWaitsForServerState)
{
    auto files = makeFiles();
    FileTreeModel model;
    model.setFiles(files);
    QTreeView view;
    view.setModel(&model);
    view.setTreePosition(FileTreeModel::Name);
    view.setEditTriggers(QAbstractItemView::NoEditTriggers);
    view.setItemDelegateForColumn(FileTreeModel::Want, new WantCheckDelegate(&view));
    view.setColumnWidth(FileTreeModel::Want, 36);
    view.expandAll();
    view.show();
    QCoreApplication::processEvents();

    const auto file = model.index(0, FileTreeModel::Want, model.index(0, 0));
    const QRect cell = view.visualRect(file);
    ASSERT_TRUE(cell.isValid());
    const QPoint pos = cell.center();
    const QPoint global = view.viewport()->mapToGlobal(pos);
    int requests = 0;
    QObject::connect(&model, &FileTreeModel::wantToggled, &view,
                     [&](const QVector<int>& rows, bool wanted) {
        ++requests;
        EXPECT_EQ(rows, (QVector<int>{0}));
        EXPECT_FALSE(wanted);
    });
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    const QPoint outside(cell.left() + 1, pos.y());
    const QPoint outsideGlobal = view.viewport()->mapToGlobal(outside);
    QMouseEvent outsidePress(QEvent::MouseButtonPress, outside, outsideGlobal, Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
    QMouseEvent outsideRelease(QEvent::MouseButtonRelease, outside, outsideGlobal, Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &outsidePress);
    QApplication::sendEvent(view.viewport(), &release);
    QApplication::sendEvent(view.viewport(), &press);
    QApplication::sendEvent(view.viewport(), &outsideRelease);
    EXPECT_EQ(requests, 0);

    QApplication::sendEvent(view.viewport(), &press);
    EXPECT_EQ(requests, 0);
    QApplication::sendEvent(view.viewport(), &release);
    EXPECT_EQ(requests, 1);
    EXPECT_EQ(model.wantState(file), Qt::Checked);

    files[0].wanted = false;
    model.setFiles(files);
    EXPECT_EQ(model.wantState(file), Qt::Unchecked);
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

// W12/G11 репро (TST-02): уничтожение модели при незавершённом reverse DNS не
// должно ронять процесс. Резолвер — фейк: «ответ» приходит строго после
// delete, без реального DNS и фиксированных ожиданий.
TEST(DetailPeerModel, ResolveHostModelDestroyedWhileLookupPending)
{
    QVector<Peer> peers;
    Peer p;
    p.address = "192.0.2.1"; // TEST-NET; реального lookup не будет (резолвер — фейк)
    p.port = 6881;
    peers.push_back(p);
    PeerListModel* m = new PeerListModel; // heap: уничтожаем, пока lookup «в работе»
    std::function<void(QString)> lateCallback;
    m->setResolver([&lateCallback](const QString&, std::function<void(QString)> done) {
        lateCallback = std::move(done); // ответим только после уничтожения модели
    });
    m->setPeers(peers);
    m->setResolveHost(true);
    ASSERT_TRUE(static_cast<bool>(lateCallback));
    delete m; // lookup ещё «pending»
    lateCallback(QStringLiteral("late.example.net")); // колбэк на уничтоженной модели
    QCoreApplication::processEvents();
    SUCCEED();
}

// TST-02: полный путь startReverseLookups → резолвер → кеш → колонка «Адрес»
// с детерминированным внедрённым резолвером (без реальной сети).
TEST(DetailPeerModel, ResolveHostUsesInjectedResolver)
{
    PeerListModel m;
    QVector<Peer> peers;
    Peer p;
    p.address = "1.2.3.4";
    p.port = 5678;
    peers.push_back(p);
    QStringList resolved;
    m.setResolver([&resolved](const QString& ip, std::function<void(QString)> done) {
        resolved.append(ip);
        done(QStringLiteral("injected.example.net"));
    });
    m.setPeers(peers);
    m.setResolveHost(true);
    EXPECT_EQ(resolved, QStringList{"1.2.3.4"});
    ASSERT_TRUE(m.hasHostLookupResult("1.2.3.4"));
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "injected.example.net:5678");
    // Повторное включение не дублирует запрос: результат уже в кеше.
    m.setResolveHost(false);
    m.setResolveHost(true);
    EXPECT_EQ(resolved.size(), 1);
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
    // Ошибка reverse DNS (пустой host) — остаётся IP.
    EXPECT_FALSE(m.hasHostLookupResult(p.address));
    m.setResolvedHost("1.2.3.4", QString());
    ASSERT_TRUE(m.hasHostLookupResult(p.address));
    m.setResolveHost(true);
    EXPECT_EQ(m.data(m.index(0, PeerListModel::ColAddress), Qt::DisplayRole).toString(),
              "1.2.3.4");
}

// TST-03: QAbstractItemModelTester для моделей вкладок (Файлы/Пиры/Трекеры/Дерево).
TEST(DetailModels, PassItemModelTester)
{
    FileListModel files;
    files.setFiles(makeFiles());
    {
        QAbstractItemModelTester tester(&files, QAbstractItemModelTester::FailureReportingMode::Fatal);
        (void) files.data(files.index(0, FileListModel::ColName), Qt::DisplayRole);
    }
    PeerListModel peers;
    Peer peer;
    peer.address = "1.2.3.4";
    peer.port = 6881;
    peers.setPeers({peer});
    {
        QAbstractItemModelTester tester(&peers, QAbstractItemModelTester::FailureReportingMode::Fatal);
        (void) peers.data(peers.index(0, PeerListModel::ColAddress), Qt::DisplayRole);
    }
    TrackerListModel trackers;
    tgui::TrackerStat stat;
    stat.announce = "https://tracker.example/announce";
    stat.seederCount = 3;
    stat.leecherCount = 4;
    trackers.setTrackers({stat});
    {
        QAbstractItemModelTester tester(&trackers, QAbstractItemModelTester::FailureReportingMode::Fatal);
        (void) trackers.data(trackers.index(0, TrackerListModel::ColAnnounce), Qt::DisplayRole);
    }
    FileTreeModel tree;
    tree.setFiles(makeFiles());
    {
        QAbstractItemModelTester tester(&tree, QAbstractItemModelTester::FailureReportingMode::Fatal);
        (void) tree.data(tree.index(0, 0), Qt::DisplayRole);
    }
}
