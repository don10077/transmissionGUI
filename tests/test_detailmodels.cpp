// Unit-тесты моделей вкладок деталей (Файлы/Пиры/Трекеры).
#include <gtest/gtest.h>

#include <QTableView>

#include "detailmodels.h"
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
