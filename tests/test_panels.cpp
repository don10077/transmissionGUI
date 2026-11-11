// Unit-тесты панелей «General»/«Статистика» и двухуровневой раскладки
// главного окна (грид сверху, вкладки снизу — раскладка по референсу,
// см. WORKFLOW.md).
#include <gtest/gtest.h>

#include <QApplication>
#include <QToolButton>
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
        if (action->shortcut() == QKeySequence(Qt::Key_F7)) toggle = action;
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


#include <QDesktopServices>
#include <QTemporaryDir>
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


#include "fileselectdialog.h"
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
#include <QMenu>
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

#include "test_panels.moc"
