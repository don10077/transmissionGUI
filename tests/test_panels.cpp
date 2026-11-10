// Unit-тесты панелей «General»/«Статистика» и двухуровневой раскладки
// главного окна (грид сверху, вкладки снизу — раскладка по референсу,
// см. WORKFLOW.md).
#include <gtest/gtest.h>

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QDir>
#include "theme.h"
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
    panel.setCounts(50, 0, 0, 0, 0, 0, 0, 0, folders, {}, {});
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
    panel.setCounts(51, 0, 0, 0, 0, 0, 0, 0, folders, {}, {});
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
    // Раздел «Downloaded:» + одна шкала (карта кусков) + процент справа —
    // как в старом transgui (panProgress: метка, pbDownloaded, «100.0%»).
    EXPECT_FALSE(labelContaining(&f, "Downloaded:").isEmpty());
    // Процент — отдельной меткой (50.0% для этого торрента).
    EXPECT_TRUE(hasExactLabel(&f, QStringLiteral("50.0%")));
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

    auto* table = s.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 4);
    EXPECT_EQ(table->horizontalHeaderItem(1)->text(), QStringLiteral("Current"));
    EXPECT_EQ(table->horizontalHeaderItem(2)->text(), QStringLiteral("Cumulative"));
    EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("1 MB"));
    EXPECT_EQ(table->item(0, 2)->text(), QStringLiteral("2.00 GB"));
    EXPECT_EQ(table->item(2, 1)->text(), QStringLiteral("3"));
    EXPECT_TRUE(table->item(3, 1)->text().contains(QStringLiteral("01:01:01")));

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

TEST(MainWindowLayout, SelectAllSurvivesRefresh)
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
    w.applyTorrentsUpdate(torrents);

    // Ctrl+A: выделены все строки. Автообновление не должно сбрасывать
    // множественное выделение на одну строку (регресс-тест к selectedTorrentIds).
    w.torrentView()->selectAll();
    ASSERT_EQ(w.torrentView()->selectionModel()->selectedRows().size(), 3);

    w.applyTorrentsUpdate(torrents);
    EXPECT_EQ(w.torrentView()->selectionModel()->selectedRows().size(), 3);

    // Небольшое изменение данных (как при обычном refresh) — выделение цело.
    torrents[1].rateDownload = 4096;
    w.applyTorrentsUpdate(torrents);
    EXPECT_EQ(w.torrentView()->selectionModel()->selectedRows().size(), 3);
    EXPECT_EQ(w.currentId(), 1);
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

TEST(PropsForm, ProgressStaysVisibleWhileDetailsScroll)
{
    PropsForm form;
    form.resize(520, 180);
    Torrent torrent;
    torrent.id = 51;
    torrent.name = QString(400, QChar('n'));
    torrent.magnetLink = QStringLiteral("magnet:?xt=urn:btih:") + QString(400, QChar('a'));
    form.setTorrent(torrent);
    form.show();
    QCoreApplication::processEvents();

    auto* scroll = form.findChild<QScrollArea*>();
    auto* progress = form.findChild<QWidget*>(QStringLiteral("progressRow"));
    auto* magnet = form.findChild<QLineEdit*>(QStringLiteral("magnetLink"));
    ASSERT_NE(scroll, nullptr);
    ASSERT_NE(progress, nullptr);
    ASSERT_NE(magnet, nullptr);
    EXPECT_EQ(progress->parentWidget(), &form);
    const QPoint before = progress->mapTo(&form, QPoint());
    ASSERT_GT(scroll->verticalScrollBar()->maximum(), 0);
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    EXPECT_EQ(progress->mapTo(&form, QPoint()), before);
    EXPECT_LE(progress->geometry().bottom(), scroll->geometry().top());
    EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
    EXPECT_TRUE(magnet->isReadOnly());
    magnet->selectAll();
    EXPECT_EQ(magnet->selectedText(), *torrent.magnetLink);

    form.clear();
    EXPECT_EQ(form.findChild<QWidget*>(QStringLiteral("progressRow")), nullptr);
}

TEST(PropsForm, CopyFromCaptionAndValueIncludesClippedText)
{
    PropsForm form;
    form.resize(520, 180);
    Torrent torrent;
    torrent.id = 52;
    torrent.hashString = QString(200, QChar('a'));
    form.setTorrent(torrent);
    QLabel* caption = nullptr;
    QLabel* value = nullptr;
    for (auto* label : form.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Hash:"))
            caption = label;
        if (label->text() == *torrent.hashString)
            value = label;
    }
    ASSERT_NE(caption, nullptr);
    ASSERT_NE(value, nullptr);
    for (auto* label : {caption, value}) {
        auto* action = label->findChild<QAction*>(QStringLiteral("copyFieldValue"));
        ASSERT_NE(action, nullptr);
        QApplication::clipboard()->clear();
        action->trigger();
        EXPECT_EQ(QApplication::clipboard()->text(), *torrent.hashString);
    }
}

TEST(PropsForm, CommentLinkIsClickableAndCopiesOriginalUrl)
{
    PropsForm form;
    Torrent torrent;
    torrent.id = 53;
    torrent.comment = QStringLiteral("https://example.org/details?a=1&b=2");
    form.setTorrent(torrent);
    QLabel* link = nullptr;
    for (auto* label : form.findChildren<QLabel*>())
        if (label->toolTip() == *torrent.comment)
            link = label;
    ASSERT_NE(link, nullptr);
    EXPECT_TRUE(link->openExternalLinks());
    EXPECT_TRUE(link->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    EXPECT_TRUE(link->text().contains(QStringLiteral("<a href=")));
    auto* copy = link->findChild<QAction*>(QStringLiteral("copyFieldValue"));
    ASSERT_NE(copy, nullptr);
    copy->trigger();
    EXPECT_EQ(QApplication::clipboard()->text(), *torrent.comment);

    torrent.comment = QStringLiteral("ordinary comment <b>not markup</b>");
    form.setTorrent(torrent);
    for (auto* label : form.findChildren<QLabel*>()) {
        if (label->toolTip() == *torrent.comment) {
            EXPECT_EQ(label->textFormat(), Qt::PlainText);
            EXPECT_FALSE(label->openExternalLinks());
            EXPECT_EQ(label->text(), *torrent.comment);
        }
    }
}

TEST(InfoPanel, RendersAllReferenceTabs)
{
    if (!qEnvironmentVariable("TGUI_PANEL_PREVIEW_DIR").isEmpty())
        tgui::theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
    MainWindow window;
    Torrent torrent;
    torrent.id = 71;
    torrent.name = QStringLiteral("Example release");
    torrent.downloadDir = QStringLiteral("D:/Downloads");
    torrent.status = 4;
    torrent.sizeWhenDone = 1073741824.;
    torrent.totalSize = torrent.sizeWhenDone;
    torrent.leftUntilDone = 536870912.;
    torrent.pieceCount = 16;
    torrent.pieces = QString::fromLatin1(QByteArray::fromHex("aa55").toBase64());
    torrent.hashString = QString(40, QChar('a'));
    torrent.comment = QStringLiteral("https://example.org/release");
    window.applyTorrentsUpdate({torrent});
    window.generalForm()->setTorrent(torrent);
    QVector<FileStat> files;
    files.append({0, QStringLiteral("Release/Video/movie.mkv"), 1073741824., .5, 0, true});
    files.append({1, QStringLiteral("Release/Subtitles/English.srt"), 4096., 1., 1, true});
    files.append({2, QStringLiteral("Release/readme.txt"), 2048., 0., 0, false});
    const_cast<FileListModel&>(window.fileModel()).setFiles(files);
    Peer peer;
    peer.address = QStringLiteral("192.0.2.1");
    peer.clientName = QStringLiteral("Transmission 4.0");
    peer.progress = 62.; peer.flagStr = QStringLiteral("DU");
    peer.rateToClient = 1024 * 256; peer.rateToPeer = 8192;
    const_cast<PeerListModel&>(window.peerModel()).setPeers({peer});
    TrackerStat tracker;
    tracker.announce = QStringLiteral("https://tracker.example.org/announce");
    tracker.lastAnnounceResult = QStringLiteral("Success"); tracker.seederCount = 125;
    const_cast<TrackerListModel&>(window.trackerModel()).setTrackers({tracker});
    SessionStats stats;
    stats.current.downloadedBytes = 1073741824.; stats.current.uploadedBytes = 536870912.;
    stats.current.filesAdded = 3; stats.current.secondsActive = 3600;
    stats.cumulative = stats.current;
    window.statsForm()->setStats(stats);
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("infoPanel"));
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 5);
    // Separate from the main splitter so every tab is checked at reference size.
    tabs->setParent(nullptr);
    tabs->resize(984, 326);
    tabs->show();
    auto* tree = tabs->findChild<QTreeView*>(QStringLiteral("filesView"));
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->model()->columnCount(), 5);
    EXPECT_EQ(tree->model()->rowCount(), 1);
    tree->expandAll();
    const QString output = qEnvironmentVariable("TGUI_PANEL_PREVIEW_DIR");
    if (!output.isEmpty()) QDir().mkpath(output);
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        QCoreApplication::processEvents();
        EXPECT_TRUE(tabs->currentWidget()->isVisible());
        if (!output.isEmpty()) {
            EXPECT_TRUE(tabs->grab().save(output + QStringLiteral("/panel-%1.png").arg(i)));
        }
    }
    delete tabs;
}
