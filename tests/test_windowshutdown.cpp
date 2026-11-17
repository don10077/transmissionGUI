// ARCH-02: закрытие главного окна, пока запрос к демону «в полёте».
// Деструктор MainWindow обязан остановить таймеры и уничтожить RpcClient
// (вместе с QNetworkAccessManager) до моделей и контроллеров; под
// ASan/UBSan сценарий должен пройти без UAF/дабл-фри.
#include <gtest/gtest.h>

#include "mainwindow.h"
#include "mockhttpserver.h"
#include "tgui/rpcclient.h"

#include "testwait.h"

TEST(WindowShutdown, CloseDuringPendingRequest)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));

    MockRpcServer server;
    ASSERT_NE(server.start(), 0);

    // Ответ на первый session-get задерживаем: запрос клиента остаётся в полёте.
    MockRpcServer::Response session;
    session.body =
        QString("{\"result\": \"success\", \"arguments\": "
                "{\"rpc-version\": 16, \"version\": \"4.1.0\", "
                "\"download-dir\": \"/downloads\", \"torrent-count\": 0}}")
            .toUtf8();
    server.enqueue(session);
    server.holdNext();

    {
        MainWindow w;
        tgui::RpcClient::Options o;
        o.url = server.url();
        w.setAutoConnect(o);

        // Запрос должен дойти до сервера и быть удержан (ответ не отправлен).
        ASSERT_TRUE(waitUntil([&] { return server.requestCount() >= 1; }, 5000));
        ASSERT_EQ(server.heldCount(), 1);
        ASSERT_FALSE(w.isConnectedNow());
        // Уничтожение окна при зависшем запросе (w завершает область ниже).
    }

    // Удержанный ответ больше никому не нужен: освобождаем, чтобы мок-сервер
    // корректно закрыл сокет при выходе (QPointer гаснет, если клиента нет).
    server.release();
}
