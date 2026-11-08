#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <chrono>

#include <gtest/gtest.h>

#include "mockhttpserver.h"
#include "tgui/jsonrpc.h"

namespace {

class JsonRpcTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_NE(server.start(), 0); }

    MockRpcServer server;
};

TEST_F(JsonRpcTest, SuccessAndRequestShape)
{
    tgui::JsonRpcClient client(server.url());
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.httpCode, 200);
    EXPECT_EQ(r.status, "success");
    EXPECT_EQ(r.arguments.value("rpc-version").toInt(), 18);

    ASSERT_EQ(server.requestCount(), 1);
    const auto req = server.requests().first();
    EXPECT_EQ(req.path, "/transmission/rpc");
    EXPECT_EQ(req.headers.value("content-type"), "application/json");
    EXPECT_EQ(req.rpcMethod, "session-get");
    // Пустые arguments — без ключа "arguments" (как в transgui).
    EXPECT_FALSE(req.json.contains("arguments"));
}

TEST_F(JsonRpcTest, CustomRpcPath)
{
    tgui::JsonRpcClient client(server.url(), "/web/rpc");
    const tgui::JsonRpcResult r = client.callSync("session-get", QJsonObject{},
                                                  std::chrono::seconds(5));
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(server.requests().first().path, "/web/rpc");
}

TEST_F(JsonRpcTest, SessionIdRetryOn409)
{
    tgui::JsonRpcClient client(server.url());
    server.setExpectedSessionId("test-session-123");
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));

    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(server.requestCount(), 2);
    const auto first = server.requests().at(0);
    const auto second = server.requests().at(1);
    // Первый запрос — без session id (409), второй — с выданным id.
    EXPECT_FALSE(first.headers.contains("x-transmission-session-id"));
    EXPECT_EQ(second.headers.value("x-transmission-session-id"), "test-session-123");
}

TEST_F(JsonRpcTest, SessionIdErrorWhenNoHeaderIn409)
{
    tgui::JsonRpcClient client(server.url());
    // 409 без заголовка x-transmission-session-id → "Session ID error."
    for (int i = 0; i < 2; ++i)
        server.enqueue(MockRpcServer::Response{409, "{\"result\": \"failure\"}", {}});
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.httpCode, 409);
    EXPECT_EQ(r.error, "Session ID error.");
    EXPECT_EQ(server.requestCount(), 2);
}

TEST_F(JsonRpcTest, BasicAuthHeaderSent)
{
    tgui::JsonRpcClient client(server.url());
    client.setUsernamePassword("user", "secret");
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));
    ASSERT_TRUE(r.ok) << r.error;

    // "Basic " + base64("user:secret")
    EXPECT_EQ(server.requests().first().headers.value("authorization"),
              "Basic dXNlcjpzZWNyZXQ=");
}

TEST_F(JsonRpcTest, Unauthorized401)
{
    tgui::JsonRpcClient client(server.url()); // без auth
    server.requireAuthorization("Basic dXNlcjpwYXNz");
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.httpCode, 401);
    EXPECT_EQ(r.error, "unauthorized");
}

TEST_F(JsonRpcTest, RpcLevelError)
{
    tgui::JsonRpcClient client(server.url());
    server.enqueue(MockRpcServer::Response{
        200, "{\"result\": \"failure\", \"error\": \"duplicate torrent\"}", {}});
    const tgui::JsonRpcResult r =
        client.callSync("torrent-add", QJsonObject{}, std::chrono::seconds(5));

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.httpCode, 200);
    EXPECT_EQ(r.status, "failure");
    EXPECT_EQ(r.error, "duplicate torrent");
}

TEST_F(JsonRpcTest, InvalidServerResponse)
{
    tgui::JsonRpcClient client(server.url());
    server.enqueue(MockRpcServer::Response{200, "not a json", {}});
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.error, "Invalid server response.");
}

TEST_F(JsonRpcTest, ArgumentsPassed)
{
    tgui::JsonRpcClient client(server.url());
    QJsonObject args{{"path", "/downloads"}};
    const tgui::JsonRpcResult r =
        client.callSync("free-space", args, std::chrono::seconds(5));
    ASSERT_TRUE(r.ok) << r.error;

    const auto req = server.requests().first();
    EXPECT_EQ(req.rpcMethod, "free-space");
    EXPECT_EQ(req.arguments.value("path").toString(), "/downloads");
}

TEST_F(JsonRpcTest, MovedPermanentRedirectToWeb)
{
    // Transmission 4.x: 301 Location: /web/ → клиент пробует /web/rpc.
    MockRpcServer::Response redirect;
    redirect.code = 301;
    redirect.body = "";
    redirect.headers.insert("Location", "/web/");
    server.enqueue(redirect);

    tgui::JsonRpcClient client(server.url());
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(client.rpcPath(), "/web/rpc");
    ASSERT_EQ(server.requestCount(), 2);
    EXPECT_EQ(server.requests().at(1).path, "/web/rpc");
}

TEST_F(JsonRpcTest, ConnectionRefused)
{
    // Порт, на котором никто не слушает.
    tgui::JsonRpcClient client(QUrl("http://127.0.0.1:1"));
    client.setTimeout(std::chrono::seconds(2));
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.httpCode, 0);
    EXPECT_FALSE(r.error.isEmpty());
}

TEST_F(JsonRpcTest, Timeout)
{
    // Сервер принимает соединение, но не отвечает.
    QTcpServer slow;
    ASSERT_TRUE(slow.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&slow, &QTcpServer::newConnection, &slow, [&slow]() {
        if (QTcpSocket* s = slow.pendingConnection())
            QObject::connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
    });

    tgui::JsonRpcClient client(QUrl(QString("http://127.0.0.1:%1").arg(slow.serverPort())));
    client.setTimeout(std::chrono::milliseconds(300));
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(3));
    EXPECT_FALSE(r.ok);
}

} // namespace