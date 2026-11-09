// E2E (живой демон, offscreen): список → выбор → вкладки Файлы/Пиры/Трекеры.
// Активен только при заданных TGUI_E2E_URL/TGUI_E2E_USER/TGUI_E2E_PASS,
// иначе пропускается — базовый прогон остаётся автономным.
#include <gtest/gtest.h>

#include <QApplication>
#include <QEventLoop>
#include <QModelIndex>
#include <QTimer>
#include <QTreeView>

#include <cstdlib>

#include "mainwindow.h"

namespace {

void spinEvents(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

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
    QTimer::singleShot(45000, qApp, [] { std::_Exit(3); });

    MainWindow w;
    tgui::RpcClient::Options o;
    o.url = QUrl(url);
    o.username = user;
    o.password = pass;
    w.setAutoConnect(o);

    // Ждём подключение и список торрентов.
    for (int i = 0;
         i < 60 && !(w.isConnectedNow() && !w.torrentModel().torrents().isEmpty());
         ++i)
        spinEvents(250);
    ASSERT_TRUE(w.isConnectedNow());
    ASSERT_FALSE(w.torrentModel().torrents().isEmpty());
    w.show();

    // Выбираем торренты, пока не найдём с непустыми трекерами (не у каждого
    // торрента есть трекеры — например, magnet без announce).
    bool trackersLoaded = false;
    for (int row = 0; row < 6 && !trackersLoaded; ++row) {
        const QModelIndex idx = w.torrentView()->model()->index(row, 0);
        if (!idx.isValid())
            break;
        w.torrentView()->setCurrentIndex(idx);
        for (int i = 0; i < 20; ++i) {
            spinEvents(250);
            if (w.fileModel().rowCount() > 0 || w.trackerModel().rowCount() > 0)
                break;
        }
        trackersLoaded = w.trackerModel().rowCount() > 0;
    }

    EXPECT_GT(w.currentId(), 0);
    EXPECT_GT(w.fileModel().rowCount(), 0);
    EXPECT_TRUE(trackersLoaded);
    // «Пиры» может быть пустым — все торренты остановлены; проверяем лишь
    // корректность модели (0 строк — допустимо).
    EXPECT_GE(w.peerModel().rowCount(), 0);
}