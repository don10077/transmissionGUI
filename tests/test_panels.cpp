// Unit-тесты панелей «General»/«Статистика» и двухуровневой раскладки
// главного окна (грид сверху, вкладки снизу — раскладка по референсу,
// см. WORKFLOW.md).
#include <gtest/gtest.h>
#include <algorithm>

#include <QApplication>
#include <QAbstractButton>
#include <QToolButton>
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QMimeData>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QDir>
#include "theme.h"
#include <QTreeView>
#include <QTreeWidget>
#include <QWheelEvent>

#include "mainwindow.h"
#include "connectdialog.h"
#include "daemonprofile.h"
#include "externalopen.h"
#include "fileselectdialog.h"
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
    QLabel* labels = nullptr;
    for (auto* field : f.findChildren<QLabel*>())
        if (field->accessibleName() == QStringLiteral("hd, video"))
            labels = field;
    ASSERT_NE(labels, nullptr);
    EXPECT_EQ(labels->textFormat(), Qt::RichText);
    labels->setSelection(2, 2);
    EXPECT_TRUE(labels->hasSelectedText());
    const auto resource = labels->resourceProvider();
    ASSERT_TRUE(resource);
    EXPECT_FALSE(resource(QUrl(QStringLiteral("tgui-label:/0"))).value<QImage>().isNull());
    EXPECT_FALSE(resource(QUrl(QStringLiteral("tgui-label:/1"))).value<QImage>().isNull());
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
    auto* progress = f.findChild<QWidget*>(QStringLiteral("progressRow"));
    ASSERT_NE(progress, nullptr);
    f.setTorrent(t);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    EXPECT_EQ(bar->value(), target);
    EXPECT_EQ(f.findChild<QWidget*>(QStringLiteral("progressRow")), progress);
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
    EXPECT_EQ(table->rowCount(), 5);
    EXPECT_EQ(table->horizontalHeaderItem(1)->text(), QStringLiteral("Current"));
    EXPECT_EQ(table->horizontalHeaderItem(2)->text(), QStringLiteral("Cumulative"));
    EXPECT_EQ(table->item(0, 1)->text(), QStringLiteral("1 MB"));
    EXPECT_EQ(table->item(0, 2)->text(), QStringLiteral("2.00 GB"));
    EXPECT_EQ(table->item(2, 0)->text(), QStringLiteral("Ratio"));
    EXPECT_EQ(table->item(3, 1)->text(), QStringLiteral("3"));
    EXPECT_TRUE(table->item(4, 1)->text().contains(QStringLiteral("01:01:01")));

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

static QMenu* openPropertyMenu(PropsForm& form, QWidget* field)
{
    const QPoint point = field->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, field->mapToGlobal(point));
    QApplication::sendEvent(field, &event);
    return form.findChild<QMenu*>(QStringLiteral("copyFieldMenu"));
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
        auto* menu = openPropertyMenu(form, label);
        ASSERT_NE(menu, nullptr);
        auto* action = menu->findChild<QAction*>(QStringLiteral("copyFieldValue"));
        ASSERT_NE(action, nullptr);
        QApplication::clipboard()->clear();
        action->trigger();
        EXPECT_EQ(QApplication::clipboard()->text(), *torrent.hashString);
        delete menu;
    }

    QPointer<QWidget> oldField = value;
    QPointer<QMenu> menu = openPropertyMenu(form, value);
    ASSERT_FALSE(menu.isNull());
    const QString oldHash = *torrent.hashString;
    torrent.hashString = QString(200, QChar('b'));
    form.setTorrent(torrent);
    EXPECT_TRUE(oldField.isNull());
    ASSERT_FALSE(menu.isNull());
    auto* copy = menu->findChild<QAction*>(QStringLiteral("copyFieldValue"));
    ASSERT_NE(copy, nullptr);
    QApplication::clipboard()->clear();
    copy->trigger();
    EXPECT_EQ(QApplication::clipboard()->text(), oldHash);
    menu->close();
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
        if (label->text().contains(torrent.comment->toHtmlEscaped()))
            link = label;
    ASSERT_NE(link, nullptr);
    EXPECT_TRUE(link->toolTip().isEmpty());
    EXPECT_TRUE(link->openExternalLinks());
    EXPECT_TRUE(link->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    EXPECT_TRUE(link->text().contains(QStringLiteral("<a href=")));
    auto* menu = openPropertyMenu(form, link);
    ASSERT_NE(menu, nullptr);
    auto* copy = menu->findChild<QAction*>(QStringLiteral("copyFieldValue"));
    ASSERT_NE(copy, nullptr);
    copy->trigger();
    EXPECT_EQ(QApplication::clipboard()->text(), *torrent.comment);
    delete menu;

    torrent.comment = QStringLiteral("ordinary comment <b>not markup</b>");
    form.setTorrent(torrent);
    QLabel* plainComment = nullptr;
    for (auto* label : form.findChildren<QLabel*>())
        if (label->text() == *torrent.comment)
            plainComment = label;
    ASSERT_NE(plainComment, nullptr);
    EXPECT_EQ(plainComment->textFormat(), Qt::PlainText);
    EXPECT_FALSE(plainComment->openExternalLinks());
    EXPECT_TRUE(plainComment->toolTip().isEmpty());
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
    EXPECT_EQ(tree->model()->columnCount(), FileTreeModel::ColumnCount);
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


#include "propsdialog.h"
#include "daemonoptionsdialog.h"
#include "connoptionsdialog.h"
#include "optionsdialog.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>

TEST(Properties, SendsOnlyChangedTorrentSettings)
{
    Torrent torrent;
    torrent.name = QStringLiteral("Test torrent");
    torrent.maxConnectedPeers = 50;
    torrent.editableSettings = {{"downloadLimited", false}, {"downloadLimit", 0},
        {"uploadLimited", true}, {"uploadLimit", 123}, {"seedRatioMode", 0},
        {"seedRatioLimit", 0.}, {"seedIdleMode", 2}, {"seedIdleLimit", 30}};
    PropsDialog unchanged(torrent);
    unchanged.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    EXPECT_EQ(unchanged.result(), QDialog::Accepted);
    EXPECT_TRUE(unchanged.changes().isEmpty());
    PropsDialog changed(torrent);
    changed.findChild<QSpinBox*>(QStringLiteral("peerLimit"))->setValue(75);
    changed.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    EXPECT_EQ(changed.changes(), (QJsonObject{{"peer-limit",75}}));
}

TEST(Properties, SequentialDownloadToggle)
{
    // RPC ≥ 18, торрент качается без sequential: включаем — ключ уходит в payload.
    Torrent torrent;
    torrent.name = QStringLiteral("Test torrent");
    torrent.sequentialDownload = false;
    PropsDialog on(torrent, nullptr, 18);
    auto* seq = on.findChild<QCheckBox*>(QStringLiteral("sequentialDownload"));
    ASSERT_NE(seq, nullptr);
    ASSERT_TRUE(seq->isEnabled());
    EXPECT_FALSE(seq->isChecked());
    seq->setChecked(true);
    on.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    EXPECT_EQ(on.result(), QDialog::Accepted);
    EXPECT_EQ(on.changes(), (QJsonObject{{"sequential_download", true}}));

    // Уже включено и не трогаем — ключа в payload нет.
    Torrent active;
    active.name = QStringLiteral("Active");
    active.sequentialDownload = true;
    PropsDialog keep(active, nullptr, 18);
    keep.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    EXPECT_TRUE(keep.changes().isEmpty());

    // Демон < 18: пункт неактивен, ключ в payload не попадает.
    PropsDialog old(active, nullptr, 14);
    auto* oldSeq = old.findChild<QCheckBox*>(QStringLiteral("sequentialDownload"));
    ASSERT_NE(oldSeq, nullptr);
    EXPECT_FALSE(oldSeq->isEnabled());
    oldSeq->setChecked(false);
    old.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    EXPECT_TRUE(old.changes().isEmpty());
}

TEST(Properties, BatchChangesOnlyExplicitSettings)
{
    PropsDialog dialog(3, nullptr, 18);
    auto* ratio = dialog.findChild<QComboBox*>("seedRatioMode");
    auto* download = dialog.findChild<QComboBox*>("downloadLimited");
    ASSERT_NE(ratio, nullptr);
    ASSERT_NE(download, nullptr);
    ratio->setCurrentIndex(3);
    download->setCurrentIndex(1);
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(buttons, nullptr);
    buttons->button(QDialogButtonBox::Ok)->click();
    EXPECT_EQ(dialog.changes(), (QJsonObject{{"seedRatioMode", 2}, {"downloadLimited", false}}));
}

TEST(Properties, TrackerTierEditingUsesTrackerListOnSupportedDaemon)
{
    tgui::Torrent torrent;
    torrent.trackerStats = {{.id = 1, .announce = "https://one.test/announce", .tier = 0},
                            {.id = 2, .announce = "https://two.test/announce", .tier = 1}};
    PropsDialog dialog(torrent, nullptr, 17, 1);
    auto* edit = dialog.findChild<QPlainTextEdit*>();
    ASSERT_NE(edit, nullptr);
    EXPECT_EQ(edit->toPlainText(), "https://one.test/announce\n\nhttps://two.test/announce");
    edit->setPlainText("https://two.test/announce\nhttps://one.test/announce");
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    ASSERT_NE(buttons, nullptr);
    buttons->button(QDialogButtonBox::Ok)->click();
    EXPECT_EQ(dialog.changes(),
              (QJsonObject{{"trackerList", "https://two.test/announce\nhttps://one.test/announce"}}));
}

TEST(DialogAppearance, AllPagesFitAndDaemonPreservesZero)
{
    tgui::SessionInfo session;
    session.altSpeedDown = 0; session.altSpeedUp = 0; session.ratioLimit = 0.;
    DaemonOptionsDialog daemon(session);
    EXPECT_EQ(daemon.settings().altSpeedDown, 0);
    EXPECT_EQ(daemon.settings().altSpeedUp, 0);
    EXPECT_DOUBLE_EQ(daemon.settings().ratioLimit, 0.);
    OptionsDialog application;
    ConnOptionsDialog connection;
    Torrent torrent; torrent.name = QStringLiteral("Example torrent");
    PropsDialog properties(torrent);
    QList<QDialog*> dialogs{&properties, &daemon, &application, &connection};
    const QString output = qEnvironmentVariable("TGUI_DIALOG_PREVIEW_DIR");
    if (!output.isEmpty()) {
        tgui::theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
        QDir().mkpath(output);
    }
    for (int index = 0; index < dialogs.size(); ++index) {
        auto* dialog = dialogs[index]; dialog->show();
        auto* tabs = dialog->findChild<QTabWidget*>();
        ASSERT_NE(tabs, nullptr);
        for (int page = 0; page < tabs->count(); ++page) {
            tabs->setCurrentIndex(page); QCoreApplication::processEvents();
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            ASSERT_NE(buttons, nullptr);
            EXPECT_LE(buttons->geometry().bottom(), dialog->height());
            if (!output.isEmpty()) {
                EXPECT_TRUE(dialog->grab().save(output + QStringLiteral("/dialog-%1-%2.png").arg(index).arg(page)));
            }
        }
        dialog->hide();
    }
}


#include "mockhttpserver.h"
#include <QElapsedTimer>
#include <functional>
#include <QThread>
#include <QSettings>

namespace {
bool waitForSettings(const std::function<bool()>& predicate)
{
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 2500) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    return predicate();
}
}

enum class AddScenario { Default, AddFailure, StartFailure, SkipAndDelete, Cancel, Duplicate, ChangedSource };
class TorrentAddOutcome : public testing::TestWithParam<AddScenario> {};

TEST_P(TorrentAddOutcome, HandlesResultAndOriginalFile)
{
    QSettings settings;
    const auto scenario = GetParam();
    const bool skipDialog = scenario == AddScenario::SkipAndDelete || scenario == AddScenario::ChangedSource;
    QHash<QString, QVariant> saved;
    for (const QString& key : {QStringLiteral("daemons/current"), QStringLiteral("ui/autoRefresh"),
                               QString(OptionsDialog::skipAddDialogKey), QString(OptionsDialog::deleteSourceTorrentKey)})
        saved.insert(key, settings.value(key));
    const auto restore = qScopeGuard([&] {
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
            if (it.value().isValid()) settings.setValue(it.key(), it.value());
            else settings.remove(it.key());
        }
    });
    settings.remove("daemons/current");
    settings.setValue("ui/autoRefresh", false);
    settings.setValue(OptionsDialog::skipAddDialogKey, skipDialog);
    settings.setValue(OptionsDialog::deleteSourceTorrentKey, scenario != AddScenario::Default);

    MockRpcServer server;
    ASSERT_NE(server.start(), 0);
    const MockRpcServer::Response initial{200, R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","download-dir":"/downloads","torrents":[{"id":1,"name":"Original"}]}})", {}};
    server.setDefault(initial);
    class DropWindow : public MainWindow {
    public:
        using MainWindow::dropEvent;
    } window;
    RpcClient::Options options;
    options.url = server.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.currentId() == 1 && server.requestCount() >= 6; }));

    QTemporaryDir sourceDir;
    ASSERT_TRUE(sourceDir.isValid());
    QFile source(sourceDir.filePath("source.torrent"));
    ASSERT_TRUE(source.open(QIODevice::WriteOnly));
    ASSERT_EQ(source.write("de"), 2);
    source.close();
    QMimeData file;
    file.setUrls({QUrl::fromLocalFile(source.fileName())});
    const auto drop = [&](const QMimeData& mime) {
        QDropEvent event(QPointF(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        window.dropEvent(&event);
        EXPECT_TRUE(event.isAccepted());
    };
    const auto countRequests = [&](const QString& method) {
        const auto requests = server.requests();
        return std::count_if(requests.cbegin(), requests.cend(),
            [&](const auto& request) { return request.rpcMethod == method; });
    };
    if (scenario == AddScenario::AddFailure) {
        server.enqueue({200, R"({"result":"test add failure"})", {}});
        drop(file);
        ASSERT_TRUE(waitForSettings([&] {
            return window.statusBar()->currentMessage().contains("test add failure");
        }));
        EXPECT_TRUE(QFile::exists(source.fileName()));
        EXPECT_EQ(countRequests("torrent-remove"), 0);
        return;
    }
    const MockRpcServer::Response duplicate{200, R"({"result":"success","arguments":{"torrent-duplicate":{"id":1,"name":"Original"}}})", {}};
    const auto duplicatePrompt = [&]() -> QMessageBox* {
        for (auto* box : window.findChildren<QMessageBox*>())
            if (box->isVisible())
                return box;
        return nullptr;
    };
    if (scenario == AddScenario::Duplicate) {
        server.enqueue(duplicate);
        drop(file);
        ASSERT_TRUE(waitForSettings([&] { return duplicatePrompt() != nullptr; }));
        const int before = server.requestCount();
        duplicatePrompt()->reject();
        ASSERT_TRUE(waitForSettings([&] { return server.requestCount() > before; }));
        EXPECT_TRUE(QFile::exists(source.fileName()));
        EXPECT_EQ(countRequests("torrent-add"), 1);
        EXPECT_EQ(countRequests("torrent-remove"), 0);
        return;
    }
    auto* search = window.findChild<QLineEdit*>("searchEdit");
    ASSERT_NE(search, nullptr);
    search->setText("Original");
    server.enqueue({200, R"({"result":"success","arguments":{"torrent-added":{"id":2,"name":"New"}}})", {}});
    const MockRpcServer::Response addedList{200, R"({"result":"success","arguments":{"torrents":[{"id":2,"name":"New","downloadDir":"/downloads"},{"id":1,"name":"Original","downloadDir":"/downloads"}]}})", {}};
    server.setDefault(addedList);
    if (skipDialog) {
        server.enqueue(addedList);
        server.enqueue({200, R"({"result":"success"})", {}, 200});
    }
    drop(file);
    const auto visibleOptions = [&] {
        auto* dialog = window.findChild<FileSelectDialog*>();
        return dialog && dialog->isVisible();
    };
    if (skipDialog) {
        ASSERT_TRUE(waitForSettings([&] { return countRequests("torrent-start") == 1; }));
        EXPECT_FALSE(visibleOptions());
        EXPECT_TRUE(QFile::exists(source.fileName()));
        if (scenario == AddScenario::ChangedSource) {
            QFile replacement(source.fileName());
            ASSERT_TRUE(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
            ASSERT_EQ(replacement.write("replacement"), 11);
        }
    } else {
        ASSERT_TRUE(waitForSettings(visibleOptions));
        auto* dialog = window.findChild<FileSelectDialog*>();
        if (scenario == AddScenario::Cancel) {
            server.setDefault(initial);
            dialog->reject();
            ASSERT_TRUE(waitForSettings([&] { return countRequests("torrent-remove") == 1; }));
            EXPECT_EQ(countRequests("torrent-start"), 0);
            EXPECT_TRUE(QFile::exists(source.fileName()));
            return;
        }
        auto* start = dialog->findChild<QCheckBox*>("startTorrent");
        ASSERT_NE(start, nullptr);
        start->setChecked(true);
        if (scenario == AddScenario::StartFailure)
            server.enqueue({200, R"({"result":"test start failure"})", {}});
        static_cast<QDialog*>(dialog)->accept();
    }
    ASSERT_TRUE(waitForSettings([&] { return window.currentId() == 2 && server.requestCount() >= 13; }));
    if (scenario == AddScenario::StartFailure) {
        EXPECT_TRUE(window.statusBar()->currentMessage().contains("test start failure"));
    }
    EXPECT_TRUE(search->text().isEmpty());
    EXPECT_EQ(QFile::exists(source.fileName()), scenario != AddScenario::SkipAndDelete);
    if (scenario != AddScenario::Default)
        return;

    QMimeData magnet;
    magnet.setText("magnet:?xt=urn:btih:0123456789012345678901234567890123456789");
    server.enqueue(duplicate);
    drop(magnet);
    ASSERT_TRUE(waitForSettings([&] { return duplicatePrompt() != nullptr; }));
    auto* box = duplicatePrompt();
    const auto buttons = box->buttons();
    const auto update = std::find_if(buttons.cbegin(), buttons.cend(),
        [box](auto* button) { return box->buttonRole(button) == QMessageBox::AcceptRole; });
    ASSERT_NE(update, buttons.cend());
    (*update)->click();
    ASSERT_TRUE(waitForSettings([&] { return window.currentId() == 1; }));

    int adds = 0;
    for (const auto& request : server.requests()) {
        if (request.rpcMethod == "torrent-add")
            ++adds;
        EXPECT_NE(request.rpcMethod, "torrent-remove");
    }
    EXPECT_EQ(adds, 2);
    EXPECT_TRUE(QFile::exists(source.fileName()));
}

INSTANTIATE_TEST_SUITE_P(AddSettings, TorrentAddOutcome,
    testing::Values(AddScenario::Default, AddScenario::AddFailure, AddScenario::StartFailure,
                    AddScenario::SkipAndDelete, AddScenario::Cancel, AddScenario::Duplicate,
                    AddScenario::ChangedSource));

class MockDaemonWindow : public testing::Test {
protected:
    inline static const MockRpcServer::Response healthy{200,
        R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","download-dir":"/default","torrents":[]}})", {}};
    void SetUp() override
    {
        for (const QString& key : {QStringLiteral("daemons/current"), QStringLiteral("ui/autoRefresh"),
                                   QString(OptionsDialog::clipboardLinksKey), QString(OptionsDialog::watchFolderKey),
                                   QString(OptionsDialog::watchDestinationKey)})
            saved.insert(key, settings.value(key));
        settings.remove("daemons/current");
        settings.setValue("ui/autoRefresh", false);
        settings.setValue(OptionsDialog::clipboardLinksKey, false);
        settings.remove(OptionsDialog::watchFolderKey);
        ASSERT_NE(server.start(), 0);
        server.setDefault(healthy);
    }

    void TearDown() override
    {
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
            if (it.value().isValid()) settings.setValue(it.key(), it.value());
            else settings.remove(it.key());
        }
    }

    void connectWindow(MainWindow& window)
    {
        const int before = server.requestCount();
        RpcClient::Options options;
        options.url = server.url();
        window.setAutoConnect(options);
        ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow() && server.requestCount() >= before + 3; }));
    }

    QList<QJsonObject> additions() const
    {
        QList<QJsonObject> result;
        for (const auto& request : server.requests())
            if (request.rpcMethod == "torrent-add")
                result.append(request.arguments);
        return result;
    }

    QSettings settings;
    MockRpcServer server;
    QHash<QString, QVariant> saved;
};

using AutomaticAddSources = MockDaemonWindow;

TEST_F(MockDaemonWindow, RefreshIntervalFollowsWindowVisibilityAndAutoRefresh)
{
    const auto foreground = settings.value("ui/refreshInterval");
    const auto background = settings.value("ui/backgroundRefreshInterval");
    const auto restore = qScopeGuard([&] {
        if (foreground.isValid()) settings.setValue("ui/refreshInterval", foreground);
        else settings.remove("ui/refreshInterval");
        if (background.isValid()) settings.setValue("ui/backgroundRefreshInterval", background);
        else settings.remove("ui/backgroundRefreshInterval");
    });
    settings.setValue("ui/autoRefresh", true);
    settings.setValue("ui/refreshInterval", 2);
    settings.setValue("ui/backgroundRefreshInterval", 9);
    MainWindow window;
    window.show();
    ASSERT_NO_FATAL_FAILURE(connectWindow(window));
    auto* timer = window.findChild<QTimer*>("refreshTimer");
    auto* refresh = window.findChild<QAction*>("autoRefreshAction");
    ASSERT_NE(timer, nullptr);
    ASSERT_NE(refresh, nullptr);
    EXPECT_TRUE(timer->isActive());
    EXPECT_EQ(timer->interval(), 2000);
    window.showMinimized();
    EXPECT_EQ(timer->interval(), 9000);
    window.showNormal();
    EXPECT_EQ(timer->interval(), 2000);
    window.hide();
    EXPECT_EQ(timer->interval(), 9000);
    refresh->setChecked(false);
    window.show();
    EXPECT_FALSE(timer->isActive());
}

TEST(TorrentModel, RateSmoothingLeavesRpcSamplesAndActiveFilterUnchanged)
{
    TorrentModel model;
    model.setSmoothRates(true);
    tgui::Torrent torrent;
    torrent.id = 1;
    torrent.rateDownload = 1000;
    model.setTorrents({torrent});
    torrent.rateDownload = 3000;
    model.setTorrents({torrent});
    EXPECT_DOUBLE_EQ(model.displayedRates(model.torrentAt(0)).first, 2000);
    EXPECT_EQ(model.torrentAt(0).rateDownload.value_or(0), 3000);
    torrent.rateDownload = 0;
    model.setTorrents({torrent});
    EXPECT_DOUBLE_EQ(model.displayedRates(model.torrentAt(0)).first, 0);
    EXPECT_FALSE(TorrentModel::matchesGroup(model.torrentAt(0), TorrentModel::GroupActive));
}

TEST_F(MockDaemonWindow, QuickSpeedMenuUsesTheSelectedProfile)
{
    server.setDefault({200, R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","alt-speed-enabled":true,"speed-limit-down-enabled":true,"speed-limit-down":77,"torrents":[]}})", {}});
    ProfileStore profiles;
    const auto cleanup = qScopeGuard([&] {
        profiles.remove("speeds-a");
        profiles.remove("speeds-b");
    });
    for (const auto& [name, speed] : {std::pair{QStringLiteral("speeds-a"), 111},
                                      std::pair{QStringLiteral("speeds-b"), 222}}) {
        profiles.add(name);
        DaemonProfile profile;
        profile.name = name;
        profile.host = server.url().host();
        profile.port = server.url().port();
        profiles.save(profile);
        ProfilePreferences(name).setSpeedPresets(true, {speed});
    }
    profiles.setCurrent("speeds-a");
    MainWindow window;
    ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow() && server.requestCount() >= 3; }));
    auto* speedMenu = window.findChild<QMenu*>("quickDownloadSpeed");
    ASSERT_NE(speedMenu, nullptr);
    const auto speedAction = [speedMenu](int speed) -> QAction* {
        for (auto* action : speedMenu->actions())
            if (action->data().isValid() && action->data().toInt() == speed)
                return action;
        return nullptr;
    };
    const auto writes = [&] {
        QList<QJsonObject> result;
        for (const auto& request : server.requests())
            if (request.rpcMethod == "session-set")
                result.append(request.arguments);
        return result;
    };
    ASSERT_TRUE(QMetaObject::invokeMethod(speedMenu, "aboutToShow", Qt::DirectConnection));
    EXPECT_NE(speedAction(77), nullptr); // Текущий лимит показывается вне списка пресетов.
    EXPECT_EQ(speedAction(222), nullptr);
    auto* oldAction = speedAction(111);
    ASSERT_NE(oldAction, nullptr);
    oldAction->trigger();
    ASSERT_TRUE(waitForSettings([&] { return writes().size() == 1 && server.requestCount() >= 5; }));

    auto* connection = window.findChild<QToolButton*>("connectButton");
    ASSERT_NE(connection, nullptr);
    auto* profileMenu = connection->menu();
    ASSERT_NE(profileMenu, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(profileMenu, "aboutToShow", Qt::DirectConnection));
    const auto actions = profileMenu->actions();
    const auto other = std::find_if(actions.cbegin(), actions.cend(),
        [](const auto* action) { return action->text() == "speeds-b"; });
    ASSERT_NE(other, actions.cend());
    (*other)->trigger();
    ASSERT_TRUE(window.isConnectedNow()); // Оба профиля используют один адрес.
    oldAction->trigger();
    ASSERT_TRUE(QMetaObject::invokeMethod(speedMenu, "aboutToShow", Qt::DirectConnection));
    EXPECT_EQ(speedAction(111), nullptr);
    auto* newAction = speedAction(222);
    ASSERT_NE(newAction, nullptr);
    newAction->trigger();
    ASSERT_TRUE(waitForSettings([&] { return writes().size() >= 2; }));
    EXPECT_EQ(writes(), (QList<QJsonObject>{
        {{"speed-limit-down", 111}, {"speed-limit-down-enabled", true}, {"alt-speed-enabled", false}},
        {{"speed-limit-down", 222}, {"speed-limit-down-enabled", true}, {"alt-speed-enabled", false}}
    }));
}

TEST_F(AutomaticAddSources, ClipboardOptInAndNormalizesRepeatedLinks)
{
    MainWindow window;
    ASSERT_NO_FATAL_FAILURE(connectWindow(window));
    auto* clipboard = QApplication::clipboard();
    const auto previous = clipboard->text();
    const auto restore = qScopeGuard([&] {
        settings.setValue(OptionsDialog::clipboardLinksKey, false);
        clipboard->setText(previous);
    });
    clipboard->setText("https://example.test/disabled.torrent");
    QCoreApplication::processEvents();
    EXPECT_TRUE(additions().isEmpty());

    settings.setValue(OptionsDialog::clipboardLinksKey, true);
    clipboard->setText("https://example.test/ordinary-page");
    clipboard->setText("  0123456789ABCDEF0123456789ABCDEF01234567  ");
    ASSERT_TRUE(waitForSettings([&] { return additions().size() >= 1; }));
    clipboard->setText("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=repeat");
    clipboard->setText("https://example.test/file.torrent?token=example#fragment");
    ASSERT_TRUE(waitForSettings([&] { return additions().size() >= 2 && server.requestCount() >= 7; }));
    const auto result = additions();
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result.at(0).value("filename").toString(),
              "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567");
    EXPECT_EQ(result.at(1).value("filename").toString(),
              "https://example.test/file.torrent?token=example");
}

TEST_F(AutomaticAddSources, WatchFolderWaitsForStableFileAndPersistsAttempt)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    settings.setValue(OptionsDialog::watchFolderKey, directory.path());
    settings.setValue(OptionsDialog::watchDestinationKey, "/watched");
    const auto historyKey = QStringLiteral("downloads/watchHistory/") +
        QString::fromLatin1(QUrl::toPercentEncoding(QDir(directory.path()).absolutePath()));
    const auto cleanup = qScopeGuard([&] { settings.remove(historyKey); });
    const auto path = directory.filePath("example.TORRENT");
    const auto writeSource = [&](const QByteArray& bytes) {
        QFile source(path);
        ASSERT_TRUE(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ(source.write(bytes), bytes.size());
    };
    ASSERT_NO_FATAL_FAILURE(writeSource("de"));
    const auto scan = [](MainWindow& window) {
        EXPECT_TRUE(QMetaObject::invokeMethod(&window, "scanWatchFolder", Qt::DirectConnection));
    };
    {
        MainWindow window;
        ASSERT_NO_FATAL_FAILURE(connectWindow(window));
        scan(window);
        ASSERT_NO_FATAL_FAILURE(writeSource("d1:ai1ee"));
        scan(window);
        EXPECT_TRUE(settings.value(historyKey).toMap().isEmpty());
        scan(window);
        ASSERT_TRUE(waitForSettings([&] { return additions().size() == 1 && server.requestCount() >= 5; }));
        EXPECT_EQ(additions().first().value("download-dir").toString(), "/watched");
        EXPECT_EQ(additions().first().value("metainfo").toString(), QString::fromLatin1(QByteArray("d1:ai1ee").toBase64()));
        EXPECT_TRUE(QFile::exists(path));
    }
    MainWindow reopened;
    ASSERT_NO_FATAL_FAILURE(connectWindow(reopened));
    scan(reopened);
    scan(reopened);
    ASSERT_NO_FATAL_FAILURE(writeSource("d1:ai10ee"));
    scan(reopened);
    scan(reopened);
    ASSERT_TRUE(waitForSettings([&] { return additions().size() >= 2; }));
    const auto result = additions();
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result.last().value("metainfo").toString(), QString::fromLatin1(QByteArray("d1:ai10ee").toBase64()));
}

TEST_F(AutomaticAddSources, ExternalOpenForwardsUnicodeFileAndMagnetBeforeConnection)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QFile source(directory.filePath(QStringLiteral("тест файл.torrent")));
    ASSERT_TRUE(source.open(QIODevice::WriteOnly));
    ASSERT_EQ(source.write("de"), 2);
    source.close();
    const auto path = externalTorrentInput(QUrl::fromLocalFile(source.fileName()).toString(QUrl::FullyEncoded));
    EXPECT_EQ(path, source.fileName());
    const auto magnet = externalTorrentInput("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567");
    ASSERT_FALSE(magnet.isEmpty());
    EXPECT_TRUE(externalTorrentInput("https://example.test/file.torrent").isEmpty());

    MainWindow window;
    ExternalOpen primary(directory.path());
    QStringList received;
    ASSERT_EQ(primary.listen([&](const auto& inputs) {
        received.append(inputs);
        window.openExternalTorrents(inputs);
    }), ExternalOpen::Result::Listening);
    ExternalOpen secondary(directory.path());
    ASSERT_EQ(secondary.listen([](const auto&) {}), ExternalOpen::Result::AlreadyRunning);
    const QStringList inputs{path, magnet};
    bool delivered = false;
    const std::unique_ptr<QThread> sender(QThread::create([&] { delivered = secondary.forward(inputs); }));
    sender->start();
    const bool finished = waitForSettings([&] { return sender->isFinished(); });
    ASSERT_TRUE(sender->wait(4000));
    ASSERT_TRUE(finished);
    ASSERT_TRUE(delivered);
    EXPECT_EQ(received, inputs);
    EXPECT_TRUE(additions().isEmpty());

    ASSERT_NO_FATAL_FAILURE(connectWindow(window));
    ASSERT_TRUE(waitForSettings([&] { return additions().size() >= 2; }));
    const auto result = additions();
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result.first().value("metainfo").toString(), QString::fromLatin1(QByteArray("de").toBase64()));
    EXPECT_EQ(result.last().value("filename").toString(), magnet);
}

class ConnectionRecovery : public MockDaemonWindow {
protected:
    static void processFor(int milliseconds)
    {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    }

    static bool waitingForRetry(MainWindow& window, int seconds)
    {
        return window.statusBar()->currentMessage().contains(QString("Retrying in %1 s").arg(seconds));
    }
};

TEST_F(ConnectionRecovery, TimeoutAndTransportLossRecoverWithBackoff)
{
    auto slow = healthy;
    slow.delayMs = 400;
    server.enqueue(slow);
    server.enqueue({503, "Service temporarily unavailable", {}});
    RpcClient::Options options;
    options.url = server.url();
    options.timeoutMs = 120;
    MainWindow window(nullptr, options);
    ASSERT_TRUE(waitForSettings([&] { return waitingForRetry(window, 1); }));
    ASSERT_TRUE(waitForSettings([&] { return waitingForRetry(window, 2); }));
    ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow() && server.requestCount() >= 5; }));

    const int before = server.requestCount();
    server.setDefault({0, {}, {}});
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onRefresh", Qt::DirectConnection));
    ASSERT_TRUE(waitForSettings([&] { return waitingForRetry(window, 1); }));
    EXPECT_FALSE(window.isConnectedNow());
    EXPECT_GT(server.requestCount(), before);
    server.setDefault(healthy);
    EXPECT_TRUE(waitForSettings([&] { return window.isConnectedNow(); }));
}

TEST_F(ConnectionRecovery, DisconnectAndProfileSwitchCancelPendingRetry)
{
    server.setDefault({503, "Service temporarily unavailable", {}});
    RpcClient::Options options;
    options.url = server.url();
    MainWindow window(nullptr, options);
    auto* disconnect = window.findChild<QAction*>("disconnectAction");
    ASSERT_NE(disconnect, nullptr);
    ASSERT_TRUE(waitForSettings([&] { return waitingForRetry(window, 1); }));
    ASSERT_TRUE(disconnect->isEnabled());
    disconnect->trigger();
    const int before = server.requestCount();
    processFor(1200);
    EXPECT_EQ(server.requestCount(), before);
    EXPECT_FALSE(disconnect->isEnabled());

    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return waitingForRetry(window, 1); }));
    MockRpcServer second;
    ASSERT_NE(second.start(), 0);
    second.setDefault(healthy);
    ProfileStore profiles;
    DaemonProfile target;
    target.name = QStringLiteral("recovery-target");
    target.host = second.url().host();
    target.port = second.url().port();
    profiles.add(target.name);
    profiles.save(target);
    const auto cleanup = qScopeGuard([&] { profiles.remove(target.name); });
    auto* connectButton = window.findChild<QToolButton*>("connectButton");
    ASSERT_NE(connectButton, nullptr);
    auto* menu = connectButton->menu();
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
    const auto actions = menu->actions();
    const auto targetAction = std::find_if(actions.cbegin(), actions.cend(),
        [&](const auto* action) { return action->text() == target.name; });
    ASSERT_NE(targetAction, actions.cend());
    (*targetAction)->trigger();
    ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow() && second.requestCount() >= 3; }));
    const int beforeSwitchRetry = server.requestCount();
    processFor(1200);
    EXPECT_EQ(server.requestCount(), beforeSwitchRetry);
    EXPECT_TRUE(window.isConnectedNow());
}

TEST_F(ConnectionRecovery, Parallel401ShowsOnePromptAndCancelStops)
{
    MainWindow window;
    ASSERT_NO_FATAL_FAILURE(connectWindow(window));
    const int before = server.requestCount();
    server.requireAuthorization("Basic dGVzdDp0ZXN0");
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onRefresh", Qt::DirectConnection));
    ASSERT_TRUE(waitForSettings([&] {
        return window.findChild<ConnectDialog*>() && server.requestCount() >= before + 2;
    }));
    processFor(50);
    const auto dialogs = window.findChildren<ConnectDialog*>();
    ASSERT_EQ(dialogs.size(), 1);
    EXPECT_FALSE(window.isConnectedNow());
    dialogs.first()->reject();
    const int requests = server.requestCount();
    processFor(1200);
    EXPECT_EQ(server.requestCount(), requests);
    EXPECT_FALSE(window.isConnectedNow());
    EXPECT_TRUE(window.findChildren<ConnectDialog*>().isEmpty());
}

TEST(SettingsFixes, CompletionNotificationIsTransitionAndPreferenceGated)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings().setValue("ui/trayNotify", true);
    MainWindow window;
    int messages = 0;
    QObject::connect(&window, &MainWindow::downloadNotification, [&](const QString&) { ++messages; });
    Torrent torrent; torrent.id = 100; torrent.status = 6;
    torrent.sizeWhenDone = 1024.; torrent.leftUntilDone = 0.;
    window.applyTorrentsUpdate({torrent});
    EXPECT_EQ(messages, 0); // Existing completed torrents must not notify on connect.
    torrent.status = 4; torrent.leftUntilDone = 512.;
    window.applyTorrentsUpdate({torrent});
    torrent.status = 6; torrent.leftUntilDone = 0.;
    window.applyTorrentsUpdate({torrent}); window.applyTorrentsUpdate({torrent});
    EXPECT_EQ(messages, 1);
    QSettings().setValue("ui/trayNotify", false);
    torrent.status = 4; torrent.leftUntilDone = 512.; window.applyTorrentsUpdate({torrent});
    torrent.status = 6; torrent.leftUntilDone = 0.; window.applyTorrentsUpdate({torrent});
    EXPECT_EQ(messages, 1);
}

TEST(SettingsFixes, AltTogglePreservesLimitsAndEditedActiveProfileReconnects)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    MockRpcServer first, second;
    ASSERT_NE(first.start(), 0); ASSERT_NE(second.start(), 0);
    const QByteArray response = R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","alt-speed-enabled":true,"alt-speed-down":234,"alt-speed-up":67,"download-dir":"/downloads"}})";
    first.setDefault({200,response,{}}); second.setDefault({200,response,{}});
    ProfileStore store;
    DaemonProfile profile;
    profile.name = "settings-fix"; profile.host = "127.0.0.1"; profile.port = first.url().port();
    store.add(profile.name); store.save(profile); store.setCurrent(profile.name);
    MainWindow window;
    window.setAutoConnect(profile.toOptions());
    ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow(); }));
    QAction* toggle = nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->shortcut() == QKeySequence(Qt::Key_F8)) toggle = action;
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isChecked());
    toggle->trigger();
    ASSERT_TRUE(waitForSettings([&] {
        for (const auto& request : first.requests())
            if (request.rpcMethod == "session-set") return true;
        return false;
    }));
    for (const auto& request : first.requests()) {
        if (request.rpcMethod != "session-set") continue;
        EXPECT_EQ(request.arguments, (QJsonObject{{"alt-speed-enabled",false}}));
    }
    ASSERT_TRUE(waitForSettings([&] { return toggle->isChecked(); })); // Confirmed session state.
    profile.port = second.url().port();
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<ConnOptionsDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        dialog->setProfile(profile); dialog->saveCurrent(); dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onConnectionOptions", Qt::DirectConnection));
    ASSERT_TRUE(waitForSettings([&] { return second.requestCount() > 0 && window.isConnectedNow(); }));
    EXPECT_EQ(second.requests().first().rpcMethod, QStringLiteral("session-get"));
    const QString renamed = QStringLiteral("settings-fix-renamed");
    store.rename(profile.name, renamed);
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<ConnOptionsDialog*>(QApplication::activeModalWidget());
        if (dialog) dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onConnectionOptions", Qt::DirectConnection));
    EXPECT_EQ(window.findChild<QToolButton*>(QStringLiteral("connectButton"))->text(), renamed);
    store.remove(renamed);
}

TEST(InfoPanel, FilesTabRequestsProgressRefresh)
{
    // Проценты файлов меняются во время проверки/загрузки: вкладка «Файлы»
    // должна перезапрашивать детали, а не показывать данные, полученные
    // при выборе торрента (иначе при проверке так и висят нули).
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const QVariant oldProfile = settings.value("daemons/current");
    settings.remove("daemons/current"); // иначе конструктор уйдёт на профиль из настроек
    const QVariant oldAutoRefresh = settings.value("ui/autoRefresh");
    settings.setValue("ui/autoRefresh", false);
    MockRpcServer server;
    ASSERT_NE(server.start(), 0);
    server.setDefault({200,
        R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","torrents":[]}})",
        {}});
    MainWindow window;
    tgui::RpcClient::Options options;
    options.url = server.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow(); }));
    Torrent torrent;
    torrent.id = 900;
    torrent.name = QStringLiteral("checking");
    torrent.status = static_cast<int>(TorrentStatus::Check);
    window.applyTorrentsUpdate({torrent});

    auto filesRequests = [&] {
        int count = 0;
        for (const auto& request : server.requests()) {
            if (request.rpcMethod == QStringLiteral("torrent-get") &&
                request.arguments.value("fields").toArray().contains(
                    QJsonValue(QStringLiteral("files"))))
                ++count;
        }
        return count;
    };
    ASSERT_TRUE(waitForSettings([&] { return filesRequests() > 0; }));
    const int before = filesRequests();
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("infoPanel"));
    ASSERT_NE(tabs, nullptr);
    tabs->setCurrentIndex(3); // Files
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onRefresh", Qt::DirectConnection));
    EXPECT_TRUE(waitForSettings([&] { return filesRequests() > before; }));
    if (oldAutoRefresh.isValid())
        settings.setValue("ui/autoRefresh", oldAutoRefresh);
    else
        settings.remove("ui/autoRefresh");
    if (oldProfile.isValid())
        settings.setValue("daemons/current", oldProfile);
    else
        settings.remove("daemons/current");
}

TEST(InfoPanel, PeersTabRequestsRefresh)
{
    // Набор пиров меняется во время скачивания: при выборе торрента пиров
    // ещё не было, вкладка «Пиры» открыта — автообновление должно
    // перезапрашивать peers, а не показывать пустой снимок от выбора.
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const QVariant oldProfile = settings.value("daemons/current");
    settings.remove("daemons/current"); // иначе конструктор уйдёт на профиль из настроек
    const QVariant oldAutoRefresh = settings.value("ui/autoRefresh");
    settings.setValue("ui/autoRefresh", false);
    MockRpcServer server;
    ASSERT_NE(server.start(), 0);
    server.setDefault({200,
        R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","torrents":[]}})",
        {}});
    MainWindow window;
    tgui::RpcClient::Options options;
    options.url = server.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow(); }));
    Torrent torrent;
    torrent.id = 902;
    torrent.name = QStringLiteral("peers-refresh");
    torrent.status = static_cast<int>(TorrentStatus::Download);
    window.applyTorrentsUpdate({torrent});

    auto peersRequests = [&] {
        int count = 0;
        for (const auto& request : server.requests()) {
            if (request.rpcMethod == QStringLiteral("torrent-get") &&
                request.arguments.value("fields").toArray().contains(
                    QJsonValue(QStringLiteral("peers"))))
                ++count;
        }
        return count;
    };
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("infoPanel"));
    ASSERT_NE(tabs, nullptr);
    tabs->setCurrentIndex(2); // Peers
    ASSERT_TRUE(waitForSettings([&] { return peersRequests() > 0; }));
    const int before = peersRequests();
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onRefresh", Qt::DirectConnection));
    EXPECT_TRUE(waitForSettings([&] { return peersRequests() > before; }));
    if (oldAutoRefresh.isValid())
        settings.setValue("ui/autoRefresh", oldAutoRefresh);
    else
        settings.remove("ui/autoRefresh");
    if (oldProfile.isValid())
        settings.setValue("daemons/current", oldProfile);
    else
        settings.remove("daemons/current");
}


#include <QDesktopServices>
#include <QFile>
#include <QToolButton>

class LocalOpenCapture : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void open(const QUrl& url) { urls.append(url); }
};

TEST(TorrentInteraction, DoubleClickOpensCompletedFileAndFolder)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QFile file(directory.filePath("movie.mkv"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.close();
    ASSERT_TRUE(QDir(directory.path()).mkdir("release"));
    LocalOpenCapture capture;
    QDesktopServices::setUrlHandler("file", &capture, "open");
    struct ResetHandler { ~ResetHandler() { QDesktopServices::unsetUrlHandler("file"); } } reset;
    QSettings().setValue("paths/mappings", QStringList{});
    MainWindow window;
    Torrent torrent; torrent.id = 300; torrent.name = "movie.mkv";
    torrent.downloadDir = directory.path(); torrent.status = 0;
    torrent.sizeWhenDone = 1024.; torrent.leftUntilDone = 0.;
    window.applyTorrentsUpdate({torrent});
    auto* view = window.torrentView();
    ASSERT_TRUE(QMetaObject::invokeMethod(view, "doubleClicked", Qt::DirectConnection,
                Q_ARG(QModelIndex, view->model()->index(0, 0))));
    ASSERT_EQ(capture.urls.size(), 1);
    EXPECT_EQ(QDir::cleanPath(capture.urls.last().toLocalFile()), directory.filePath("movie.mkv"));
    torrent.name = "release"; window.applyTorrentsUpdate({torrent});
    ASSERT_TRUE(QMetaObject::invokeMethod(view, "doubleClicked", Qt::DirectConnection,
                Q_ARG(QModelIndex, view->model()->index(0, 0))));
    ASSERT_EQ(capture.urls.size(), 2);
    EXPECT_EQ(QDir::cleanPath(capture.urls.last().toLocalFile()), directory.filePath("release"));
}

TEST(TorrentInteraction, DoubleClickUsesFileListForRenamedRoot)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir(directory.path()).mkdir("Renamed Root"));
    for (const QString& name : {QStringLiteral("renamed.mkv"), QStringLiteral("subtitle.srt")}) {
        QFile file(directory.filePath(QStringLiteral("Renamed Root/") + name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.close();
    }

    LocalOpenCapture capture;
    QDesktopServices::setUrlHandler("file", &capture, "open");
    struct ResetHandler { ~ResetHandler() { QDesktopServices::unsetUrlHandler("file"); } } reset;
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const QVariant oldProfile = settings.value("daemons/current");
    settings.remove("daemons/current");
    const QVariant oldMappings = settings.value("paths/mappings");
    settings.setValue("paths/mappings", QStringList{});

    MockRpcServer server;
    ASSERT_NE(server.start(), 0);
    const QString body = QStringLiteral(
        R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","torrents":[{"id":300,"name":"Renamed Root","downloadDir":"%1","status":0,"sizeWhenDone":1024,"leftUntilDone":0,"files":[{"name":"Renamed Root/renamed.mkv","length":1024,"completed":1},{"name":"Renamed Root/subtitle.srt","length":32,"completed":1}]}]}})")
        .arg(QString(directory.path()).replace('\\', '/'));
    server.setDefault({200, body.toUtf8(), {}});

    MainWindow window;
    RpcClient::Options options;
    options.url = server.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.currentId() == 300; }));
    auto* view = window.torrentView();
    ASSERT_TRUE(QMetaObject::invokeMethod(view, "doubleClicked", Qt::DirectConnection,
                Q_ARG(QModelIndex, view->model()->index(0, 0))));
    ASSERT_TRUE(waitForSettings([&] { return !capture.urls.isEmpty(); }));
    EXPECT_EQ(QDir::cleanPath(capture.urls.last().toLocalFile()),
              directory.filePath("Renamed Root"));

    if (oldMappings.isValid())
        settings.setValue("paths/mappings", oldMappings);
    else
        settings.remove("paths/mappings");
    if (oldProfile.isValid())
        settings.setValue("daemons/current", oldProfile);
    else
        settings.remove("daemons/current");
}

TEST(DetailTabs, FilesAndTrackersColumnsRestore)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const QVariant oldProfile = settings.value("daemons/current");
    settings.remove("daemons/current");
    const QStringList filesKeys = {
        QStringLiteral("files/columns/order"), QStringLiteral("files/columns/visible"),
        QStringLiteral("files/columns/width_0"), QStringLiteral("files/columns/width_1")};
    const QStringList trackerKeys = {
        QStringLiteral("trackers/columns/order"), QStringLiteral("trackers/columns/visible"),
        QStringLiteral("trackers/columns/width_0")};
    QHash<QString, QVariant> saved;
    for (const auto& key : filesKeys + trackerKeys) {
        saved.insert(key, settings.value(key));
        settings.remove(key);
    }
    settings.setValue(QStringLiteral("files/columns/order"),
                      QStringList{"1", "0", "2", "3", "4", "5"});
    settings.setValue(QStringLiteral("files/columns/visible"),
                      QStringList{"1", "1", "1", "1", "1", "1"});
    settings.setValue(QStringLiteral("files/columns/width_0"), 222);
    settings.setValue(QStringLiteral("files/columns/width_1"), 333);
    settings.setValue(QStringLiteral("trackers/columns/order"),
                      QStringList{"2", "0", "4", "5", "1", "3"});
    settings.setValue(QStringLiteral("trackers/columns/visible"),
                      QStringList{"1", "0", "1", "0", "1", "1"});
    settings.setValue(QStringLiteral("trackers/columns/width_0"), 444);

    MainWindow window;
    auto* filesView = window.findChild<QTreeView*>(QStringLiteral("filesView"));
    auto* trackersView = window.findChild<QTableView*>(QStringLiteral("trackersView"));
    ASSERT_NE(filesView, nullptr);
    ASSERT_NE(trackersView, nullptr);
    EXPECT_EQ(filesView->header()->logicalIndex(0), FileTreeModel::Name);
    EXPECT_EQ(filesView->columnWidth(FileTreeModel::Want), 222);
    EXPECT_EQ(filesView->columnWidth(FileTreeModel::Name), 333);
    EXPECT_EQ(trackersView->horizontalHeader()->logicalIndex(0), TrackerListModel::ColSeeders);
    EXPECT_TRUE(trackersView->isColumnHidden(TrackerListModel::ColTier));
    EXPECT_TRUE(trackersView->isColumnHidden(TrackerListModel::ColLeechers));
    EXPECT_EQ(trackersView->columnWidth(TrackerListModel::ColAnnounce), 444);

    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
        if (it.value().isValid())
            settings.setValue(it.key(), it.value());
        else
            settings.remove(it.key());
    }
    if (oldProfile.isValid())
        settings.setValue("daemons/current", oldProfile);
    else
        settings.remove("daemons/current");
}

TEST(TorrentInteraction, StatusDistinguishesSeedingAndPauseAndSeedQueue)
{
    EXPECT_EQ(TorrentModel::statusText(6, true), QStringLiteral("Seeding"));
    EXPECT_EQ(TorrentModel::statusText(0, true), QStringLiteral("Finished"));
    EXPECT_EQ(TorrentModel::statusText(0, false), QStringLiteral("Paused"));
    Torrent torrent; torrent.status = 5;
    EXPECT_FALSE(TorrentModel::matchesGroup(torrent, TorrentModel::GroupWaiting));
    torrent.status = 2;
    EXPECT_TRUE(TorrentModel::matchesGroup(torrent, TorrentModel::GroupWaiting));
}


TEST(TorrentInteraction, VerificationShowsRecheckProgressInDone)
{
    // При проверке уже скачанного торрента sizeWhenDone-leftUntilDone = 100%,
    // поэтому Done/прогресс-бар должны показывать recheckProgress (референс —
    // FillTorrentsList, main.pas).
    Torrent torrent;
    torrent.id = 501;
    torrent.status = static_cast<int>(TorrentStatus::Check);
    torrent.sizeWhenDone = 100.;
    torrent.leftUntilDone = 0.;
    torrent.recheckProgress = 0.42;
    TorrentModel model;
    model.setTorrents({torrent});
    EXPECT_EQ(model.index(0, TorrentModel::ColDone).data().toString(),
              QStringLiteral("42.0%"));
    EXPECT_NEAR(model.index(0, 0).data(TorrentModel::ProgressRole).toDouble(), 42.0, 0.01);

    // После завершения проверки — обычный прогресс.
    torrent.status = static_cast<int>(TorrentStatus::Seed);
    torrent.recheckProgress = 1.0;
    model.setTorrents({torrent});
    EXPECT_EQ(model.index(0, TorrentModel::ColDone).data().toString(),
              QStringLiteral("100.0%"));
    EXPECT_DOUBLE_EQ(model.index(0, 0).data(TorrentModel::ProgressRole).toDouble(), 100.0);
}

TEST(TorrentInteraction, ReferenceCompletionAndErrorRules)
{
    Torrent torrent; torrent.id = 500; torrent.status = 0;
    torrent.sizeWhenDone = 100.; torrent.leftUntilDone = 0.;
    TorrentModel model;
    model.setTorrents({torrent});
    auto status = [&] { return model.index(0, TorrentModel::ColStatus).data().toString(); };
    EXPECT_EQ(status(), QStringLiteral("Finished"));
    EXPECT_TRUE(model.index(0, 0).data(TorrentModel::FinishedRole).toBool());
    torrent.status = 6; model.setTorrents({torrent});
    EXPECT_EQ(status(), QStringLiteral("Seeding"));

    torrent.status = 0; torrent.errorString = QStringLiteral("No data found");
    model.setTorrents({torrent});
    EXPECT_EQ(status(), QStringLiteral("Error"));
    EXPECT_TRUE(model.index(0, 0).data(TorrentModel::ErrorRole).toBool());
    EXPECT_FALSE(model.index(0, 0).data(TorrentModel::FinishedRole).toBool());
    EXPECT_EQ(model.index(0, 0).data(Qt::ToolTipRole).toString(), *torrent.errorString);
    EXPECT_TRUE(TorrentModel::matchesGroup(torrent, TorrentModel::GroupError));
    EXPECT_FALSE(TorrentModel::matchesGroup(torrent, TorrentModel::GroupStopped));
    EXPECT_FALSE(TorrentModel::matchesGroup(torrent, TorrentModel::GroupCompleted));
    PropsForm form; form.setTorrent(torrent);
    EXPECT_TRUE(hasExactLabel(&form, *torrent.errorString));

    TrackerStat bad;
    bad.hasAnnounced = true; bad.lastAnnounceSucceeded = false;
    bad.lastAnnounceResult = QStringLiteral("Connection refused");
    torrent.status = 4; torrent.errorString = bad.lastAnnounceResult;
    torrent.trackerStats = {bad};
    EXPECT_EQ(tgui::torrentError(torrent), QStringLiteral("Tracker error: Connection refused"));
    TrackerStat good; good.hasAnnounced = true; good.lastAnnounceSucceeded = true;
    torrent.trackerStats.append(good);
    EXPECT_TRUE(tgui::torrentError(torrent).isEmpty());
    torrent.errorString = QStringLiteral("Permission denied");
    EXPECT_EQ(tgui::torrentError(torrent), *torrent.errorString);
    torrent.trackerStats = {bad}; torrent.errorString = bad.lastAnnounceResult; torrent.status = 0;
    EXPECT_TRUE(tgui::torrentError(torrent).isEmpty()); // Stale tracker failure on a stopped torrent.
    torrent.errorString = QStringLiteral("Permission denied");
    EXPECT_EQ(tgui::torrentError(torrent), *torrent.errorString);
}


#include "movedialog.h"
#include <QFileDialog>

TEST(TorrentInteraction, MoveBrowseStartsAtMappedFolderAndReturnsDaemonPath)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir(directory.path()).mkdir("current"));
    ASSERT_TRUE(QDir(directory.path()).mkdir("target"));
    QSettings().setValue("paths/mappings", QStringList{"/server=" + directory.path()});
    const bool nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    struct Restore { bool value; ~Restore() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, value); } } restore{nativeDisabled};
    MoveDialog dialog(QStringLiteral("/server/current"));
    EXPECT_TRUE(dialog.moveData());
    QPushButton* browse = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>())
        if (button->text() == QStringLiteral("Browse…")) browse = button;
    ASSERT_NE(browse, nullptr);
    bool inspected = false;
    QTimer::singleShot(0, [&] {
        auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!picker) { if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject(); return; }
        inspected = true;
        EXPECT_EQ(QDir::cleanPath(picker->directory().absolutePath()), directory.filePath("current"));
        picker->setDirectory(directory.filePath("target"));
        QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
    });
    browse->click();
    EXPECT_TRUE(inspected);
    EXPECT_EQ(dialog.targetDir(), QStringLiteral("/server/target"));
    QSettings().remove("paths/mappings");
}


#include "sequentialpolicy.h"
#include <QCheckBox>
#include <QPlainTextEdit>

TEST(AutoSequential, MetadataFileCountManualOverrideAndCapabilities)
{
    tgui::TorrentDetails details;
    details.name = "Movie.MKV";
    details.downloadDir = "/downloads";
    tgui::FileStat file;
    file.name = details.name;
    file.size = 100;
    details.files = {file};
    QSettings settings;
    settings.remove("downloads/autoSequential");
    settings.remove("downloads/sequentialExtensions");
    EXPECT_FALSE(tgui::automaticSequential(details.files, 18));

    OptionsDialog options(nullptr, 18);
    options.findChild<QCheckBox*>("autoSequential")->setChecked(true);
    EXPECT_TRUE(options.findChild<QPlainTextEdit*>("sequentialExtensions")->toPlainText().contains("mkv"));
    options.apply();

    FileSelectDialog add(details, false, 18);
    EXPECT_TRUE(add.options().sequential);
    auto* checkbox = add.findChild<QCheckBox*>("sequentialDownload");
    ASSERT_NE(checkbox, nullptr);
    checkbox->click();
    EXPECT_FALSE(add.options().sequential);

    FileSelectDialog old(details, false, 17);
    EXPECT_FALSE(old.findChild<QCheckBox*>("sequentialDownload")->isEnabled());
    EXPECT_FALSE(old.options().sequential);
    OptionsDialog unsupported(nullptr, 17);
    EXPECT_FALSE(unsupported.findChild<QCheckBox*>("autoSequential")->isEnabled());

    settings.setValue("downloads/sequentialExtensions", ".mp4, *.FLAC; mkv");
    file.name = "audio.FLAC";
    EXPECT_TRUE(tgui::automaticSequential({file}, 18));
    file.name = "Folder/audio.FLAC";
    EXPECT_TRUE(tgui::automaticSequential({file}, 18));
    tgui::TorrentDetails folder = details;
    folder.name = "Folder";
    folder.files = {file};
    FileSelectDialog addFolder(folder, false, 18);
    EXPECT_TRUE(addFolder.options().sequential);
    folder.files.append(details.files.first());
    FileSelectDialog addMultiple(folder, false, 18);
    EXPECT_FALSE(addMultiple.options().sequential);
    auto* folderCheckbox = addMultiple.findChild<QCheckBox*>("sequentialDownload");
    ASSERT_NE(folderCheckbox, nullptr);
    EXPECT_TRUE(folderCheckbox->isEnabled());
    folderCheckbox->click();
    EXPECT_TRUE(addMultiple.options().sequential);
    EXPECT_FALSE(tgui::automaticSequential({}, 18));
    details.files.first().name = "document.pdf";
    FileSelectDialog manual(details, false, 18);
    EXPECT_FALSE(manual.options().sequential);
    manual.findChild<QCheckBox*>("sequentialDownload")->click();
    EXPECT_TRUE(manual.options().sequential);

    settings.remove("downloads/autoSequential");
    settings.remove("downloads/sequentialExtensions");
}


#include <QToolBar>
#include <QTranslator>

TEST(ToolbarVariantB, LayoutAvailabilityAndSavedStyle)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const QVariant oldProfile = settings.value("daemons/current");
    settings.remove("daemons/current");
    const QVariant previous = settings.value("ui/toolbarButtonStyle");
    settings.remove("ui/toolbarButtonStyle");
    MockRpcServer server; ASSERT_NE(server.start(),0);
    server.setDefault({200,R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","alt-speed-enabled":true}})",{}});
    MainWindow window;
    auto* toolbar = window.findChild<QToolBar*>("mainToolBar");
    ASSERT_NE(toolbar,nullptr);
    EXPECT_EQ(toolbar->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
    EXPECT_EQ(toolbar->iconSize(),QSize(22,22));
    auto* queue = window.findChild<QAction*>("queueAction");
    ASSERT_NE(queue,nullptr); ASSERT_NE(queue->menu(),nullptr);
    EXPECT_FALSE(queue->isEnabled());
    EXPECT_EQ(queue->menu()->actions().size(),4);
    RpcClient::Options options; options.url=server.url(); window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&]{return window.isConnectedNow();}));
    Torrent torrent; torrent.id=900; torrent.name=QStringLiteral("Example movie.mkv");
    torrent.status=4; torrent.sizeWhenDone=100.; torrent.leftUntilDone=50.;
    window.applyTorrentsUpdate({torrent});
    EXPECT_TRUE(queue->isEnabled());
    window.resize(1120,700);window.show();QCoreApplication::processEvents();
    const QString preview=qEnvironmentVariable("TGUI_TOOLBAR_PREVIEW_DIR");
    if(!preview.isEmpty()){
        tgui::theme::apply(*qobject_cast<QApplication*>(QCoreApplication::instance()));
        QTranslator translator; EXPECT_TRUE(translator.load(QStringLiteral(TGUI_QM_PATH)));
        QApplication::installTranslator(&translator);
        // Build a translated instance using the same production actions.
        MainWindow localized; localized.resize(1120,700);localized.show();
        localized.setAutoConnect(options);
        EXPECT_TRUE(waitForSettings([&]{return localized.isConnectedNow();}));
        localized.applyTorrentsUpdate({torrent});
        QCoreApplication::processEvents();QDir().mkpath(preview);
        EXPECT_TRUE(localized.findChild<QToolBar*>("mainToolBar")->grab().save(preview+"/toolbar.png"));
        QApplication::removeTranslator(&translator);
    }
    QTimer::singleShot(0, [&] {
        auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(!menu)return;
        for(auto* action:menu->actions())if(action->text()==QStringLiteral("Icons only")){action->trigger();break;}
        menu->close();
    });
    EXPECT_TRUE(QMetaObject::invokeMethod(toolbar,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(5,5))));
    EXPECT_EQ(toolbar->toolButtonStyle(),Qt::ToolButtonIconOnly);
    MainWindow restored;
    EXPECT_EQ(restored.findChild<QToolBar*>("mainToolBar")->toolButtonStyle(),Qt::ToolButtonIconOnly);
    if(previous.isValid())settings.setValue("ui/toolbarButtonStyle",previous);else settings.remove("ui/toolbarButtonStyle");
    if(oldProfile.isValid())settings.setValue("daemons/current",oldProfile);
}

TEST(TorrentActions, QueueSelectionIsBatchedAndRefreshWaitsForSuccess)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const auto oldProfile = settings.value("daemons/current");
    const auto oldAutoRefresh = settings.value("ui/autoRefresh");
    settings.remove("daemons/current");
    settings.setValue("ui/autoRefresh", false);

    MockRpcServer server;
    ASSERT_NE(server.start(), 0);
    server.setDefault({200, R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","torrents":[
        {"id":8,"name":"A","queuePosition":1},{"id":9,"name":"B","queuePosition":2},
        {"id":10,"name":"C","queuePosition":3}]}})", {}});
    MainWindow window;
    RpcClient::Options options;
    options.url = server.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.torrentView()->model()->rowCount() == 3; }));
    window.torrentView()->selectAll();
    ASSERT_EQ(window.torrentView()->selectionModel()->selectedRows().size(), 3);

    const auto listReads = [&] {
        const auto requests = server.requests();
        return std::count_if(requests.cbegin(), requests.cend(), [](const auto& request) {
            return request.rpcMethod == "torrent-get" && !request.arguments.contains("ids");
        });
    };
    const auto queueWrites = [&] {
        QList<MockRpcServer::RecordedRequest> writes;
        for (const auto& request : server.requests())
            if (request.rpcMethod.startsWith("queue-move"))
                writes.append(request);
        return writes;
    };
    // Дожидаемся стартовых запросов, чтобы ответы очереди обслуживали только команды.
    ASSERT_TRUE(waitForSettings([&] { return server.requestCount() >= 6; }));
    const auto before = listReads();
    server.enqueue({200, R"({"result":"success","arguments":{}})", {}, 250});
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onQueueUp", Qt::DirectConnection));
    ASSERT_TRUE(waitForSettings([&] { return !queueWrites().isEmpty(); }));
    EXPECT_EQ(listReads(), before);
    const auto firstWrite = queueWrites().first();
    EXPECT_EQ(firstWrite.rpcMethod, QStringLiteral("queue-move-up"));
    const auto ids = firstWrite.arguments.value("ids").toArray();
    EXPECT_EQ(ids.size(), 3);
    for (const int id : {8, 9, 10})
        EXPECT_TRUE(ids.contains(id));
    ASSERT_TRUE(waitForSettings([&] { return listReads() == before + 1; }));
    EXPECT_EQ(queueWrites().size(), 1);

    server.enqueue({200, R"({"result":"queue error"})", {}});
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onQueueDown", Qt::DirectConnection));
    ASSERT_TRUE(waitForSettings([&] { return window.statusBar()->currentMessage().contains("queue error"); }));
    EXPECT_EQ(queueWrites().size(), 2);
    EXPECT_EQ(listReads(), before + 1);

    if (oldProfile.isValid()) settings.setValue("daemons/current", oldProfile);
    else settings.remove("daemons/current");
    if (oldAutoRefresh.isValid()) settings.setValue("ui/autoRefresh", oldAutoRefresh);
    else settings.remove("ui/autoRefresh");
}

TEST(TorrentActions, MoveSendsBatchAndVerifiesNewPath)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const auto oldProfile = settings.value("daemons/current");
    const auto oldAutoRefresh = settings.value("ui/autoRefresh");
    const auto oldMoveDirs = settings.value("paths/moveDirs");
    settings.remove("daemons/current");
    settings.setValue("ui/autoRefresh", false);
    settings.remove("paths/moveDirs");

    MockRpcServer server;
    ASSERT_NE(server.start(), 0);
    const auto data = [](const QString& dir) {
        const QString body = QStringLiteral(
            R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","torrents":[{"id":8,"name":"A","downloadDir":"%1"},{"id":9,"name":"B","downloadDir":"%1"},{"id":10,"name":"C","downloadDir":"%1"}]}})")
            .arg(dir);
        return body.toUtf8();
    };
    server.setDefault({200, data(QStringLiteral("/server/old")), {}});
    MainWindow window;
    RpcClient::Options options;
    options.url = server.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.torrentView()->model()->rowCount() == 3; }));
    window.torrentView()->selectAll();
    ASSERT_EQ(window.torrentView()->selectionModel()->selectedRows().size(), 3);
    ASSERT_TRUE(waitForSettings([&] { return server.requestCount() >= 6; }));

    server.setDefault({200, data(QStringLiteral("/server/target")), {}});
    bool accepted = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<MoveDialog*>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        if (auto* combo = dialog->findChild<QComboBox*>())
            combo->setCurrentText(QStringLiteral("/server/target"));
        accepted = true;
        dialog->accept();
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onMoveSelected", Qt::DirectConnection));
    ASSERT_TRUE(accepted);

    const auto moveRequests = [&] {
        QList<MockRpcServer::RecordedRequest> writes;
        for (const auto& request : server.requests())
            if (request.rpcMethod == QLatin1String("torrent-set-location"))
                writes.append(request);
        return writes;
    };
    ASSERT_TRUE(waitForSettings([&] { return !moveRequests().isEmpty(); }));
    const auto write = moveRequests().first();
    EXPECT_EQ(write.arguments.value("location").toString(), QStringLiteral("/server/target"));
    EXPECT_TRUE(write.arguments.value("move").toBool());
    const auto ids = write.arguments.value("ids").toArray();
    ASSERT_EQ(ids.size(), 3);
    for (const int id : {8, 9, 10})
        EXPECT_TRUE(ids.contains(id));
    EXPECT_TRUE(waitForSettings([&] {
        return window.statusBar()->currentMessage().contains("Move completed");
    }));
    EXPECT_EQ(QSettings().value("paths/moveDirs").toStringList().first(),
              QStringLiteral("/server/target"));

    if (oldProfile.isValid()) settings.setValue("daemons/current", oldProfile);
    else settings.remove("daemons/current");
    if (oldAutoRefresh.isValid()) settings.setValue("ui/autoRefresh", oldAutoRefresh);
    else settings.remove("ui/autoRefresh");
    if (oldMoveDirs.isValid()) settings.setValue("paths/moveDirs", oldMoveDirs);
    else settings.remove("paths/moveDirs");
}

TEST(TorrentActions, ConfirmedRemovalCannotRetargetAnotherDaemon)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const auto oldProfile = settings.value("daemons/current");
    settings.remove("daemons/current");
    MockRpcServer first;
    MockRpcServer second;
    ASSERT_NE(first.start(), 0);
    ASSERT_NE(second.start(), 0);
    const MockRpcServer::Response data{200,
        R"({"result":"success","arguments":{"rpc-version":14,"version":"4.0","torrents":[{"id":8,"name":"A"},{"id":9,"name":"B"}]}})",
        {}};
    first.setDefault(data);
    second.setDefault(data);
    MainWindow window;
    RpcClient::Options options;
    options.url = first.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.currentId() >= 0; }));

    QVector<tgui::TrackerStat> trackers(1);
    trackers.first().id = 7;
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onTrackersReceived", Qt::DirectConnection,
                                         Q_ARG(int, window.currentId()),
                                         Q_ARG(QVector<tgui::TrackerStat>, trackers)));
    ASSERT_EQ(window.trackerModel().rowCount(), 1);
    auto* view = window.torrentView();
    const auto next = view->model()->index(1, 0);
    view->selectionModel()->setCurrentIndex(next, QItemSelectionModel::ClearAndSelect |
                                                 QItemSelectionModel::Rows);
    EXPECT_EQ(window.trackerModel().rowCount(), 0);

    bool confirmed = false;
    QTimer::singleShot(0, &window, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box)
            return;
        EXPECT_TRUE(QMetaObject::invokeMethod(&window, "onDisconnect", Qt::DirectConnection));
        options.url = second.url();
        window.setAutoConnect(options);
        confirmed = true;
        box->done(QMessageBox::Yes);
    });
    ASSERT_TRUE(QMetaObject::invokeMethod(&window, "onRemoveWithDataSelected", Qt::DirectConnection));
    ASSERT_TRUE(confirmed);
    ASSERT_TRUE(waitForSettings([&] { return window.currentId() >= 0 && second.requestCount() >= 6; }));
    for (const auto& request : first.requests() + second.requests())
        EXPECT_NE(request.rpcMethod, QStringLiteral("torrent-remove"));
    if (oldProfile.isValid()) settings.setValue("daemons/current", oldProfile);
    else settings.remove("daemons/current");
}

TEST(W04Context, AltSpeedF8ChooseLabelF7AndContextMenuItems)
{
    QCoreApplication::setOrganizationName("tgui-test");
    QCoreApplication::setApplicationName("tgui-test");
    QSettings settings;
    const QVariant oldProfile = settings.value("daemons/current");
    const QVariant oldLabels = settings.value("downloads/labels");
    const QVariant oldGrouping = settings.value("ui/labelGrouping");
    settings.remove("daemons/current");
    settings.setValue("downloads/labels", QStringList{"Video", "Music"});
    settings.setValue("ui/labelGrouping", true);
    MockRpcServer server; ASSERT_NE(server.start(), 0);
    server.setDefault({200,R"({"result":"success","arguments":{"rpc-version":16,"version":"4.0","alt-speed-enabled":false,"download-dir":"/downloads"}})",{}});
    MainWindow window;
    RpcClient::Options options; options.url = server.url();
    window.setAutoConnect(options);
    ASSERT_TRUE(waitForSettings([&] { return window.isConnectedNow(); }));
    Torrent torrent; torrent.id = 700; torrent.name = QStringLiteral("labels-cols");
    torrent.status = 4; torrent.sizeWhenDone = 100.; torrent.leftUntilDone = 50.;
    torrent.labels = QStringList{"Video"};
    Torrent unlabeled;
    unlabeled.id = 701;
    unlabeled.name = QStringLiteral("unlabeled");
    Torrent emptyLabels;
    emptyLabels.id = 702;
    emptyLabels.name = QStringLiteral("empty-label-list");
    emptyLabels.labels = QStringList{};
    window.applyTorrentsUpdate({torrent, unlabeled, emptyLabels});

    QAction* altSpeed = nullptr; QAction* setLabels = nullptr;
    QAction* labelGrouping = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->shortcut() == QKeySequence(Qt::Key_F8)) altSpeed = action;
        if (action->shortcut() == QKeySequence(Qt::Key_F7)) setLabels = action;
        if (action->text() == QStringLiteral("Label grouping")) labelGrouping = action;
    }
    ASSERT_NE(altSpeed, nullptr);
    EXPECT_EQ(altSpeed->text(), QStringLiteral("Use alternate bandwidth settings"));
    ASSERT_NE(setLabels, nullptr);
    EXPECT_EQ(setLabels->text(), QStringLiteral("Choose label…"));
    ASSERT_NE(labelGrouping, nullptr);
    EXPECT_TRUE(labelGrouping->isVisible());
    auto* filterTree = window.findChild<QTreeWidget*>("filterTree");
    ASSERT_NE(filterTree, nullptr);
    const auto sections = filterTree->findItems("Labels", Qt::MatchExactly);
    ASSERT_EQ(sections.size(), 1);
    auto* labelsSection = sections.first();
    ASSERT_EQ(labelsSection->childCount(), 3);
    EXPECT_EQ(labelsSection->child(0)->text(0), QStringLiteral("Music (0)"));
    EXPECT_EQ(labelsSection->child(1)->text(0), QStringLiteral("Video (1)"));
    EXPECT_EQ(labelsSection->child(2)->text(0), QStringLiteral("No label (2)"));
    EXPECT_FALSE(labelsSection->isHidden());
    labelGrouping->setChecked(false);
    EXPECT_TRUE(labelsSection->isHidden());
    labelGrouping->setChecked(true);
    EXPECT_FALSE(labelsSection->isHidden());
    filterTree->setCurrentItem(labelsSection->child(2));
    EXPECT_EQ(window.torrentView()->model()->rowCount(), 2);
    filterTree->setCurrentItem(labelsSection->child(1));
    ASSERT_EQ(window.torrentView()->model()->rowCount(), 1);

    // Хоткеи главного меню — как в образце (ShortCut из main.lfm).
    QHash<QString, QKeySequence> byShortcut;
    for (auto* action : window.findChildren<QAction*>())
        if (!action->shortcut().isEmpty())
            byShortcut.insert(action->text(), action->shortcut());
    const QHash<QString, QKeySequence> expected = {
        {QStringLiteral("Start all torrents"), QKeySequence(Qt::CTRL | Qt::Key_F3)},
        {QStringLiteral("Pause all torrents"), QKeySequence(Qt::CTRL | Qt::Key_F4)},
        {QStringLiteral("High priority"), QKeySequence(Qt::CTRL | Qt::Key_H)},
        {QStringLiteral("Normal priority"), QKeySequence(Qt::CTRL | Qt::Key_N)},
        {QStringLiteral("Low priority"), QKeySequence(Qt::CTRL | Qt::Key_L)},
        {QStringLiteral("Move top"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1)},
        {QStringLiteral("Move up"), QKeySequence(Qt::CTRL | Qt::Key_1)},
        {QStringLiteral("Move down"), QKeySequence(Qt::CTRL | Qt::Key_2)},
        {QStringLiteral("Move bottom"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2)},
        {QStringLiteral("Reannounce (get more peers)"), QKeySequence(Qt::CTRL | Qt::Key_R)},
        {QStringLiteral("Remove torrent and Data"), QKeySequence(Qt::SHIFT | Qt::Key_Delete)},
        {QStringLiteral("Rename"), QKeySequence(Qt::Key_F2)},
    };
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
        EXPECT_EQ(byShortcut.value(it.key()), it.value()) << it.key().toStdString();
    }

    // Выбор метки доступен при выделении строки.
    QWidget* view = window.findChild<QWidget*>(QStringLiteral("torrentView"));
    if (!view) {
        for (auto* widget : window.findChildren<QWidget*>())
            if (widget->contextMenuPolicy() == Qt::CustomContextMenu) { view = widget; break; }
    }
    ASSERT_NE(view, nullptr);
    auto* itemView = qobject_cast<QAbstractItemView*>(view);
    ASSERT_NE(itemView, nullptr);
    ASSERT_TRUE(itemView->model() && itemView->model()->rowCount() > 0);
    auto* selModel = itemView->selectionModel();
    ASSERT_NE(selModel, nullptr);
    selModel->select(itemView->model()->index(0, 0),
                     QItemSelectionModel::ClearAndSelect);
    bool hasLabels = false, hasColumns = false, labelsEnabled = false;
    QTimer::singleShot(0, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) return;
        for (auto* action : menu->actions()) {
            if (action->text() == QStringLiteral("Label: Video")) {
                hasLabels = true; labelsEnabled = action->isEnabled();
            }
            if (action->text() == QStringLiteral("Setup columns…")) hasColumns = true;
        }
        menu->close();
    });
    EXPECT_TRUE(QMetaObject::invokeMethod(view, "customContextMenuRequested",
                                         Qt::DirectConnection, Q_ARG(QPoint, QPoint(0, 0))));
    EXPECT_TRUE(hasLabels);
    EXPECT_TRUE(labelsEnabled); // строка выделена — доступно
    EXPECT_TRUE(hasColumns);

    filterTree->setCurrentItem(labelsSection->child(1));
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget();
        if (!dialog)
            return;
        auto* editor = dialog->findChild<QPlainTextEdit*>("labelNames");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (editor && buttons) {
            editor->clear();
            buttons->button(QDialogButtonBox::Ok)->click();
        }
    });
    EXPECT_TRUE(QMetaObject::invokeMethod(&window, "onOptions", Qt::DirectConnection));
    EXPECT_FALSE(setLabels->isVisible());
    EXPECT_TRUE(labelGrouping->isVisible());
    EXPECT_FALSE(labelsSection->isHidden());
    EXPECT_EQ(filterTree->currentItem(), filterTree->topLevelItem(0));
    ASSERT_EQ(labelsSection->childCount(), 1);
    EXPECT_EQ(labelsSection->child(0)->text(0), QStringLiteral("No label (2)"));
    filterTree->setCurrentItem(labelsSection->child(0));
    EXPECT_EQ(window.torrentView()->model()->rowCount(), 2);

    if (oldProfile.isValid()) settings.setValue("daemons/current", oldProfile);
    if (oldLabels.isValid()) settings.setValue("downloads/labels", oldLabels);
    else settings.remove("downloads/labels");
    if (oldGrouping.isValid()) settings.setValue("ui/labelGrouping", oldGrouping);
    else settings.remove("ui/labelGrouping");
}

#include "test_panels.moc"
