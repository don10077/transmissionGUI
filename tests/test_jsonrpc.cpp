#include <QByteArray>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <chrono>

#include <gtest/gtest.h>

#include "mockhttpserver.h"
#include "qostream.h"
#include "tgui/jsonrpc.h"

namespace {

class JsonRpcTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_NE(server.start(), 0);
        // Ответ «по умолчанию», как у демона без специфичной сценарной очереди.
        server.setDefault(MockRpcServer::Response{
            200,
            "{\"result\": \"success\", \"arguments\": {\"rpc-version\": 18}}",
            {},
        });
    }

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
    // Пустые arguments — без ключа "arguments".
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

TEST_F(JsonRpcTest, SessionId409WithoutHeader)
{
    tgui::JsonRpcClient client(server.url());
    // 409 без заголовка x-transmission-session-id → повтор не выполняется,
    // ошибка берётся из тела ответа.
    server.enqueue(MockRpcServer::Response{409, "{\"result\": \"failure\"}", {}});
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.httpCode, 409);
    EXPECT_EQ(server.requestCount(), 1);
}

TEST_F(JsonRpcTest, ModernHandshakeAfterRedirectAndTableResponse)
{
    server.enqueue({301, {}, {{"Location", "/transmission/web/"}}});
    server.enqueue({409, {}, {{"X-Transmission-Session-Id", "modern-session"},
                              {"X-Transmission-Rpc-Version", "6.1.0"}}});
    server.enqueue({200, R"({"jsonrpc":"2.0","id":1,"result":{"rpc_version":19,"download_dir":"/data"}})", {}});
    server.enqueue({200, R"({"jsonrpc":"2.0","id":2,"result":{"torrents":[
        ["id","download_dir","name","tracker_stats","files"],
        [7,"/data","download_dir",[{"last_announce_time":42}],[{"name":"bytes_completed","bytes_completed":100}]]
    ]}})", {}});

    tgui::JsonRpcClient client(server.url());
    const auto session = client.callSync("session-get", {});
    ASSERT_TRUE(session.ok) << session.error;
    EXPECT_EQ(session.arguments.value("rpc-version").toInt(), 19);
    EXPECT_EQ(session.arguments.value("download-dir").toString(), "/data");

    const auto list = client.callSync("torrent-get",
        {{"fields", QJsonArray{"id", "downloadDir", "name", "trackerStats", "files"}},
         {"format", "table"}});
    ASSERT_TRUE(list.ok) << list.error;
    const auto rows = list.arguments.value("torrents").toArray();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.first().toArray(), (QJsonArray{"id", "downloadDir", "name", "trackerStats", "files"}));
    const auto row = rows.at(1).toArray();
    EXPECT_EQ(row.at(2).toString(), "download_dir");
    EXPECT_EQ(row.at(3).toArray().first().toObject().value("lastAnnounceTime").toInt(), 42);
    const auto file = row.at(4).toArray().first().toObject();
    EXPECT_EQ(file.value("name").toString(), "bytes_completed");
    EXPECT_EQ(file.value("bytesCompleted").toInt(), 100);

    const auto requests = server.requests();
    ASSERT_EQ(requests.size(), 4);
    EXPECT_EQ(requests.at(0).rpcMethod, "session-get");
    EXPECT_EQ(requests.at(1).path, "/transmission/rpc");
    EXPECT_EQ(requests.at(2).rpcMethod, "session_get");
    EXPECT_EQ(requests.at(2).headers.value("x-transmission-session-id"), "modern-session");
    EXPECT_EQ(requests.at(3).json.value("jsonrpc").toString(), "2.0");
    EXPECT_EQ(requests.at(3).rpcMethod, "torrent_get");
    EXPECT_EQ(requests.at(3).json.value("params").toObject().value("fields").toArray(),
              (QJsonArray{"id", "download_dir", "name", "tracker_stats", "files"}));
}

TEST_F(JsonRpcTest, ModernFallbackAndStructuredError)
{
    server.enqueue({200, R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Invalid Request"}})", {}});
    server.enqueue({200, R"({"jsonrpc":"2.0","id":1,"result":{"rpc_version_semver":"6.0.0"}})", {}});
    server.enqueue({200, R"({"jsonrpc":"2.0","id":2,"error":{"code":7,"message":"Backend error",
        "data":{"error_string":"Port check failed","result":{"port_is_open":false}}}})", {}});
    server.enqueue({200, R"({"jsonrpc":"2.0","id":999,"result":{}})", {}});

    tgui::JsonRpcClient client(server.url());
    ASSERT_TRUE(client.callSync("session-get", {}).ok);
    const auto failure = client.callSync("port-test", {});
    EXPECT_FALSE(failure.ok);
    EXPECT_EQ(failure.errorCode, 7);
    EXPECT_EQ(failure.status, "Backend error");
    EXPECT_EQ(failure.error, "Port check failed");
    EXPECT_TRUE(failure.arguments.contains("port-is-open"));
    EXPECT_FALSE(client.callSync("torrent-stop", {{"ids", QJsonArray{3}}}).ok);
    EXPECT_EQ(server.requestCount(), 4);
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

TEST_F(JsonRpcTest, LegacySessionSetUsesSeedRatioKeys)
{
    tgui::JsonRpcClient client(server.url());
    const tgui::JsonRpcResult r = client.callSync(
        "session-set",
        {{"ratio-limit", 2.5}, {"ratio-limit-enabled", true}, {"download-queue-size", 4}},
        std::chrono::seconds(5));
    ASSERT_TRUE(r.ok) << r.error;

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "session-set");
    EXPECT_EQ(req.arguments.value("seedRatioLimit").toDouble(), 2.5);
    EXPECT_TRUE(req.arguments.value("seedRatioLimited").toBool());
    EXPECT_EQ(req.arguments.value("download-queue-size").toInt(), 4);
    EXPECT_FALSE(req.arguments.contains("ratio-limit"));
}

TEST_F(JsonRpcTest, ModernSessionSetUsesModernKeys)
{
    server.enqueue({200, R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Invalid Request"}})", {}});
    server.enqueue({200, R"({"jsonrpc":"2.0","id":1,"result":{"rpc_version":19}})", {}});
    server.enqueue({200, R"({"jsonrpc":"2.0","id":2,"result":{}})", {}});

    tgui::JsonRpcClient client(server.url());
    ASSERT_TRUE(client.callSync("session-get", {}, std::chrono::seconds(5)).ok);
    const tgui::JsonRpcResult r = client.callSync(
        "session-set",
        {{"ratio-limit", 2.5},
         {"ratio-limit-enabled", true},
         {"download-queue-size", 4},
         {"incomplete-dir", "/inc"},
         {"cache-size-mb", 64},
         {"encryption", "tolerated"}},
        std::chrono::seconds(5));
    ASSERT_TRUE(r.ok) << r.error;

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "session_set");
    const auto params = req.json.value("params").toObject();
    EXPECT_EQ(params.value("seed_ratio_limit").toDouble(), 2.5);
    EXPECT_TRUE(params.value("seed_ratio_limited").toBool());
    EXPECT_EQ(params.value("download_queue_size").toInt(), 4);
    EXPECT_EQ(params.value("incomplete_dir").toString(), "/inc");
    EXPECT_EQ(params.value("cache_size_mib").toInt(), 64);
    EXPECT_EQ(params.value("encryption").toString(), "allowed");
    EXPECT_FALSE(params.contains("ratio_limit"));
}

TEST_F(JsonRpcTest, MovedPermanentRedirectToWeb)
{
    // 301: Location «/web/» → путь обрезается до «/» + «rpc».
    MockRpcServer::Response redirect;
    redirect.code = 301;
    redirect.body = "";
    redirect.headers.insert("Location", "/web/");
    server.enqueue(redirect);

    tgui::JsonRpcClient client(server.url());
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(5));
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(client.rpcPath(), "/rpc");
    ASSERT_EQ(server.requestCount(), 2);
    EXPECT_EQ(server.requests().at(1).path, "/rpc");
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
        while (QTcpSocket* s = slow.nextPendingConnection())
            QObject::connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
    });

    tgui::JsonRpcClient client(QUrl(QString("http://127.0.0.1:%1").arg(slow.serverPort())));
    client.setTimeout(std::chrono::milliseconds(300));
    const tgui::JsonRpcResult r =
        client.callSync("session-get", QJsonObject{}, std::chrono::seconds(3));
    EXPECT_FALSE(r.ok);
}

} // namespace
