// E2E (живой демон, offscreen): список → выбор → вкладки Файлы/Пиры/Трекеры.
// Активен только при заданных TGUI_E2E_URL/TGUI_E2E_USER/TGUI_E2E_PASS,
// иначе пропускается — базовый прогон остаётся автономным.
#include <gtest/gtest.h>

#include <QApplication>
#include <QItemSelectionModel>
#include <QModelIndex>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QTreeView>

#include <cstdlib>

#include "mainwindow.h"

#include "testwait.h"

TEST(E2ETabs, SelectionLoadsDetails)
{
    const QString url = qgetenv("TGUI_E2E_URL"); // базовый URL демона (без rpc-пути)
    const QString user = qgetenv("TGUI_E2E_USER");
    const QString pass = qgetenv("TGUI_E2E_PASS");
    if (url.isEmpty() || user.isEmpty())
        GTEST_SKIP() << "E2E-демон не задан (TGUI_E2E_URL/TGUI_E2E_USER/TGUI_E2E_PASS)";

    QCoreApplication::setOrganizationName(QStringLiteral("tgui-e2e"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-e2e"));
    QApplication::setQuitOnLastWindowClosed(false);

    // Страховка: при сбое подключения MainWindow показывает модалку, которая
    // блокирует тест — форс-выход вместо зависания.
    // Стековый QTimer автоматически останавливается при выходе из теста.
    QTimer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(45000);
    QObject::connect(&watchdog, &QTimer::timeout, qApp, [] { std::_Exit(3); });
    watchdog.start();

    MainWindow w;
    tgui::RpcClient::Options o;
    o.url = QUrl(url);
    o.rpcPath = qEnvironmentVariable("TGUI_E2E_PATH");
    o.username = user;
    o.password = pass;
    w.setAutoConnect(o);

    ASSERT_TRUE(waitUntil([&] { return w.isConnectedNow() && !w.torrentModel().torrents().isEmpty(); }, 15000));
    w.show();

    auto* tabs = w.findChild<QTabWidget*>("infoPanel");
    ASSERT_NE(tabs, nullptr);
    auto* trackers = tabs->findChild<QTableView*>("trackersView");
    auto* files = tabs->findChild<QTreeView*>("filesView");
    auto* peers = tabs->findChild<QTableView*>("peersView");
    ASSERT_NE(trackers, nullptr);
    ASSERT_NE(files, nullptr);
    ASSERT_NE(peers, nullptr);

    // MainWindow запрашивает детали только для видимой вкладки. Ищем торрент
    // с трекерами, так как в списке могут быть magnet-ссылки без announce.
    tabs->setCurrentWidget(trackers);
    bool trackersLoaded = false;
    for (int row = 0; row < 6 && !trackersLoaded; ++row) {
        const auto index = w.torrentView()->model()->index(row, 0);
        if (!index.isValid())
            break;
        w.torrentView()->selectionModel()->setCurrentIndex(
            index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        trackersLoaded = waitUntil([&] { return w.trackerModel().rowCount() > 0; }, 3000);
    }

    ASSERT_TRUE(trackersLoaded);
    ASSERT_GT(w.currentId(), 0);
    tabs->setCurrentWidget(files);
    EXPECT_TRUE(waitUntil([&] { return w.fileModel().rowCount() > 0; }));
    // На остановленных раздачах список пиров может быть пустым.
    tabs->setCurrentWidget(peers);
    EXPECT_EQ(peers->model(), &w.peerModel());
}
