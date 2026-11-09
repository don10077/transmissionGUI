// Unit-тесты панелей «General»/«Статистика» и двухуровневой раскладки
// главного окна (грид сверху, вкладки снизу — раскладка по референсу,
// см. WORKFLOW.md).
#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QWheelEvent>

#include "mainwindow.h"
#include "filterpanel.h"
#include "propsform.h"
#include "statsform.h"
#include "tgui/models.h"

using namespace tgui;

TEST(FilterPanel, WheelAndRefreshKeepFolderSelection)
{
    FilterPanel panel;
    panel.resize(280, 240);
    panel.setEnabledUi(true);
    QVector<QPair<QString, int>> folders;
    for (int i = 0; i < 50; ++i)
        folders.push_back({QStringLiteral("/folder/%1").arg(i), 1});
    panel.setCounts(50, 0, 0, 0, 0, 0, 0, 0, folders, {});
    panel.show();
    QCoreApplication::processEvents();
    auto* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    tree->setCurrentItem(tree->topLevelItem(8)->child(0));
    auto* bar = tree->verticalScrollBar();
    const int before = bar->value();
    const QPoint pos = tree->viewport()->rect().center();
    QWheelEvent wheel(pos, tree->viewport()->mapToGlobal(pos), QPoint(),
                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(tree->viewport(), &wheel);
    ASSERT_GT(bar->value(), before);
    const int scrolled = bar->value();
    folders[0].second = 2;
    panel.setCounts(51, 0, 0, 0, 0, 0, 0, 0, folders, {});
    QCoreApplication::processEvents();
    EXPECT_EQ(bar->value(), scrolled);
    EXPECT_EQ(panel.selectedFolder(), QStringLiteral("/folder/0"));
    EXPECT_EQ(tree->currentItem(), tree->topLevelItem(8)->child(0));
}

namespace {

QString labelContaining(QWidget* w, const QString& needle)
{
    for (QLabel* l : w->findChildren<QLabel*>())
        if (l->text().contains(needle))
            return l->text();
    return {};
}

bool hasExactLabel(QWidget* w, const QString& text)
{
    for (QLabel* l : w->findChildren<QLabel*>())
        if (l->text() == text)
            return true;
    return false;
}

} // namespace

TEST(PropsForm, RendersTorrent)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));

    PropsForm f;
    Torrent t;
    t.id = 42;
    t.name = "movie-2160p";
    t.status = 4; // Download
    t.totalSize = 1073741824.0;
    t.sizeWhenDone = 536870912.0;
    t.leftUntilDone = 268435456.0;
    t.downloadDir = "/Shares/Video";
    t.addedDate = 1700000000;
    t.isPrivate = true;
    t.labels = {"hd", "video"};
    f.setTorrent(t);
    ASSERT_NE(f.torrent(), nullptr);
    EXPECT_EQ(f.torrent()->id, 42);

    EXPECT_FALSE(labelContaining(&f, "movie-2160p").isEmpty());
    EXPECT_FALSE(labelContaining(&f, "1.00 GB").isEmpty());
    EXPECT_FALSE(labelContaining(&f, "/Shares/Video").isEmpty());
    // Раздел «Downloaded:» + бар (процент внутри QProgressBar) — по референсу.
    EXPECT_FALSE(labelContaining(&f, "Downloaded:").isEmpty());
    auto* bar = f.findChild<QProgressBar*>();
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->format(), QStringLiteral("50.0%"));
    EXPECT_EQ(bar->value(), 500);
}

TEST(PropsForm, ScrollKeepsPositionAcrossRefresh)
{
    PropsForm f;
    f.resize(520, 220);
    f.show();

    Torrent t;
    t.id = 7;
    t.name = "scroll-test";
    t.status = 4;
    t.totalSize = 8.0 * 1024 * 1024 * 1024;
    t.sizeWhenDone = t.totalSize;
    t.leftUntilDone = 0;
    t.downloadDir = "/Shares/Video";
    t.addedDate = 1700000000;
    f.setTorrent(t);
    QCoreApplication::processEvents();

    auto* scroll = f.findChild<QScrollArea*>();
    ASSERT_NE(scroll, nullptr);
    auto* bar = scroll->verticalScrollBar();
    ASSERT_NE(bar, nullptr);
    ASSERT_GT(bar->maximum(), 0);

    const int target = bar->maximum() / 2;
    bar->setValue(target);
    ASSERT_EQ(bar->value(), target);

    // Автообновление пересобирает панель — позиция не должна сбрасываться.
    f.setTorrent(t);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    EXPECT_EQ(bar->value(), target);
}

TEST(PropsForm, ClearShowsPlaceholder)
{
    PropsForm f;
    Torrent t;
    t.id = 1;
    t.name = "x";
    f.setTorrent(t);
    f.clear();
    EXPECT_EQ(f.torrent(), nullptr);
    EXPECT_FALSE(labelContaining(&f, QStringLiteral("Torrent not selected")).isEmpty());
}

TEST(StatsForm, RendersStats)
{
    StatsForm s;
    SessionStats st;
    st.current.downloadedBytes = 1048576.0;
    st.current.filesAdded = 3;
    st.current.secondsActive = 3661; // 1ч 01м 01с
    st.cumulative.downloadedBytes = 2147483648.0;
    st.cumulative.filesAdded = 7;
    st.cumulative.secondsActive = 93782;
    s.setStats(st);
    ASSERT_NE(s.stats(), nullptr);

    EXPECT_FALSE(labelContaining(&s, QStringLiteral("Current")).isEmpty());
    EXPECT_FALSE(labelContaining(&s, QStringLiteral("Total")).isEmpty());
    EXPECT_FALSE(labelContaining(&s, "1 MB").isEmpty());
    EXPECT_FALSE(labelContaining(&s, "2.00 GB").isEmpty());
    EXPECT_TRUE(hasExactLabel(&s, QStringLiteral("3")));
    EXPECT_FALSE(labelContaining(&s, "01:01:01").isEmpty());

    s.clear();
    EXPECT_EQ(s.stats(), nullptr);
}

TEST(MainWindowLayout, RefreshKeepsSelectionAndDetails)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));

    MainWindow w;
    w.torrentView()->sortByColumn(TorrentModel::ColName, Qt::AscendingOrder);
    auto mk = [](int id, const char* name) {
        Torrent t;
        t.id = id;
        t.name = name;
        t.status = 4;
        return t;
    };
    QVector<Torrent> torrents = {mk(1, "alpha"), mk(2, "beta"), mk(3, "gamma")};

    // Первая загрузка: выбирается первая строка (как в эталоне).
    w.applyTorrentsUpdate(torrents);
    ASSERT_EQ(w.currentId(), 1);

    // Пользователь выбирает третью строку.
    auto* view = w.torrentView();
    view->setCurrentIndex(view->model()->index(2, 0));
    ASSERT_EQ(w.currentId(), 3);
    ASSERT_NE(w.generalForm()->torrent(), nullptr);

    // Автообновление (раз в 2 с): модель пересоздаётся — выбранное,
    // строка и «General» должны остаться на том же торренте, а не
    // сброситься на первую строку.
    w.applyTorrentsUpdate(torrents);
    ASSERT_EQ(w.currentId(), 3);
    ASSERT_NE(w.generalForm()->torrent(), nullptr);
    EXPECT_EQ(w.generalForm()->torrent()->id, 3);

    const auto sel = view->selectionModel()->selectedRows();
    ASSERT_EQ(sel.size(), 1);
    EXPECT_EQ(sel.first().row(), 2);
}

TEST(MainWindowLayout, RefreshClearsDetailsWhenTorrentGone)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));

    MainWindow w;
    w.torrentView()->sortByColumn(TorrentModel::ColName, Qt::AscendingOrder);
    Torrent a;
    a.id = 1;
    a.name = "alpha";
    Torrent b;
    b.id = 2;
    b.name = "beta";
    w.applyTorrentsUpdate({a, b});
    w.torrentView()->setCurrentIndex(w.torrentView()->model()->index(0, 0));
    ASSERT_EQ(w.currentId(), 1);

    // Выбранный торрент удалён из списка — вкладки деталей очищаются.
    w.applyTorrentsUpdate({b});
    EXPECT_EQ(w.currentId(), -1);
    EXPECT_EQ(w.generalForm()->torrent(), nullptr);
}

TEST(MainWindowLayout, RefreshKeepsListScrollPosition)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));

    MainWindow w;
    w.resize(900, 500);
    w.show();
    QVector<Torrent> torrents;
    for (int i = 0; i < 100; ++i) {
        Torrent t;
        t.id = i + 1;
        t.name = QStringLiteral("torrent-%1").arg(i, 3, 10, QLatin1Char('0'));
        torrents.push_back(t);
    }
    w.applyTorrentsUpdate(torrents);
    QCoreApplication::processEvents();

    auto* bar = w.torrentView()->verticalScrollBar();
    ASSERT_GT(bar->maximum(), 0);
    const int target = bar->maximum() / 2;
    bar->setValue(target);

    torrents[0].rateDownload = 1024;
    w.applyTorrentsUpdate(torrents);
    QCoreApplication::processEvents();
    EXPECT_EQ(bar->value(), target);

    Torrent added;
    added.id = 101;
    added.name = QStringLiteral("torrent-100");
    torrents.push_back(added);
    w.applyTorrentsUpdate(torrents);
    QCoreApplication::processEvents();
    EXPECT_EQ(bar->value(), target);
}

TEST(MainWindowLayout, GridTopTabsBottom)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));

    MainWindow w;
    auto* tabs = w.findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 5);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("General"));
    EXPECT_EQ(tabs->tabText(4), QStringLiteral("Statistics"));
    // Грид торрентов — сверху, вне вкладок (всегда видим).
    EXPECT_NE(w.torrentView()->window(), static_cast<QWidget*>(tabs));

    // Выбор строки → панель «General» получает данные (до ответа RPC).
    Torrent t;
    t.id = 7;
    t.name = "props-check";
    QVector<Torrent> torrents;
    torrents.push_back(t);
    w.torrentModel().setTorrents(torrents);
    w.torrentView()->setCurrentIndex(w.torrentView()->model()->index(0, 0));
    ASSERT_NE(w.generalForm()->torrent(), nullptr);
    EXPECT_EQ(w.generalForm()->torrent()->id, 7);
}
