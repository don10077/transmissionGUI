#include <algorithm>
#include <memory>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QTemporaryFile>

#include <gtest/gtest.h>

#include "mockhttpserver.h"
#include "tgui/rpcclient.h"

using namespace tgui;

namespace {

// Ответ session-get с указанной версией RPC.
MockRpcServer::Response sessionGetResponse(int rpcVersion)
{
    MockRpcServer::Response r;
    r.body = QString(
                   "{\"result\": \"success\", \"arguments\": "
                   "{\"rpc-version\": %1, \"version\": \"4.1.0\", "
                   "\"download-dir\": \"/downloads\", "
                   "\"alt-speed-enabled\": true, \"alt-speed-down\": 50, "
                   "\"alt-speed-up\": 20, \"blocklist-enabled\": true, "
                   "\"speed-limit-down-enabled\": false, \"speed-limit-down\": 0, "
                   "\"speed-limit-up-enabled\": true, \"speed-limit-up\": 100, "
                   "\"peer-port\": 51413, \"dht-enabled\": true, "
                   "\"torrent-count\": 3}}")
                    .arg(rpcVersion)
                    .toUtf8();
    return r;
}

class RpcClientTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_NE(server.start(), 0);
        RpcClient::Options options;
        options.url = server.url();
        client = std::make_unique<RpcClient>(options);
    }

    void connectWith(int rpcVersion)
    {
        // rpc >= 15 → connectToDaemon делает session-get + free-space.
        server.enqueue(sessionGetResponse(rpcVersion));
        if (rpcVersion >= 15)
            server.enqueue(MockRpcServer::Response{
                200, "{\"result\": \"success\", \"arguments\": {\"size-bytes\": 4294967296}}", {}});
        QEventLoop loop;
        bool connected = false;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(client.get(), &RpcClient::connected,
                         [&connected, &loop](const SessionInfo&) {
                             connected = true;
                             loop.quit();
                         });
        timer.start(5000);
        client->connectToDaemon();
        loop.exec();
        EXPECT_TRUE(connected);
    }

    MockRpcServer server;
    std::unique_ptr<RpcClient> client;
};

TEST_F(RpcClientTest, ConnectAndSessionInfo)
{
    server.enqueue(sessionGetResponse(18));
    server.enqueue(MockRpcServer::Response{
        200, "{\"result\": \"success\", \"arguments\": {\"size-bytes\": 4294967296}}", {}});

    QEventLoop loop;
    bool connected = false;
    SessionInfo info;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::connected,
                     [&connected, &info, &loop](const SessionInfo& i) {
                         connected = true;
                         info = i;
                         loop.quit();
                     });
    timer.start(5000);
    client->connectToDaemon();
    loop.exec();

    ASSERT_TRUE(connected);
    EXPECT_TRUE(client->isConnected());
    EXPECT_EQ(client->rpcVersion(), 18);
    EXPECT_EQ(info.version, "4.1.0");
    EXPECT_EQ(info.downloadDir, "/downloads");
    ASSERT_TRUE(info.downloadDirFreeSpace.has_value());
    EXPECT_EQ(*info.downloadDirFreeSpace, 4294967296);
    EXPECT_TRUE(info.altSpeedEnabled);

    // session-get без "arguments", затем free-space c path.
    ASSERT_GE(server.requestCount(), 2);
    EXPECT_EQ(server.requests().at(0).rpcMethod, "session-get");
    EXPECT_FALSE(server.requests().at(0).json.contains("arguments"));
    EXPECT_EQ(server.requests().at(1).rpcMethod, "free-space");
    EXPECT_EQ(server.requests().at(1).arguments.value("path").toString(), "/downloads");
}

TEST_F(RpcClientTest, ConnectFailure)
{
    server.enqueue(MockRpcServer::Response{
        200, "{\"result\": \"failure\", \"error\": \"bad request\"}", {}});

    QEventLoop loop;
    bool failed = false;
    QString error;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::connectionFailed,
                     [&failed, &error, &loop](const QString& e) {
                         failed = true;
                         error = e;
                         loop.quit();
                     });
    timer.start(5000);
    client->connectToDaemon();
    loop.exec();

    EXPECT_TRUE(failed);
    EXPECT_EQ(error, "bad request");
    EXPECT_FALSE(client->isConnected());
}

TEST_F(RpcClientTest, GetTorrentsRpc18FieldsAndTable)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200,
        "{\"result\": \"success\", \"arguments\": {\"torrents\": "
        "[[\"id\",\"name\",\"status\",\"sequential_download\"], [7,\"movie\",4,true]]}}",
        {}});

    QEventLoop loop;
    bool got = false;
    QVector<Torrent> torrents;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::torrentsReceived,
                     [&got, &torrents, &loop](const QVector<Torrent>& t) {
                         got = true;
                         torrents = t;
                         loop.quit();
                     });
    timer.start(5000);
    client->getTorrents({"labels", "downloadDir", "uploadRatio", "totalSize"});
    loop.exec();

    ASSERT_TRUE(got);
    ASSERT_EQ(torrents.size(), 1);
    EXPECT_EQ(torrents.first().id, 7);
    EXPECT_EQ(*torrents.first().name, "movie");
    EXPECT_TRUE(*torrents.first().sequentialDownload);

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-get");
    EXPECT_EQ(req.arguments.value("format").toString(), "table");
    const QStringList fields = [&]() {
        QStringList out;
        for (const auto& v : req.arguments.value("fields").toArray())
            out << v.toString();
        return out;
    }();
    EXPECT_TRUE(fields.contains("labels"));
    EXPECT_TRUE(fields.contains("sequential_download"));
    EXPECT_TRUE(fields.contains("downloadDir"));
    EXPECT_TRUE(fields.contains("trackerStats"));
    EXPECT_TRUE(fields.contains("uploadRatio"));
    // сортировка полей
    QStringList sorted = fields;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(fields, sorted);
}

TEST_F(RpcClientTest, GetTorrentsOldRpcGates)
{
    connectWith(15);
    server.enqueue(MockRpcServer::Response{
        200,
        "{\"result\": \"success\", \"arguments\": {\"torrents\": "
        "[{\"id\": 1, \"name\": \"old\", \"status\": 6}]}}",
        {}});

    QEventLoop loop;
    bool got = false;
    QVector<Torrent> torrents;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::torrentsReceived,
                     [&got, &torrents, &loop](const QVector<Torrent>& t) {
                         got = true;
                         torrents = t;
                         loop.quit();
                     });
    timer.start(5000);
    client->getTorrents({"labels", "sequential_download"});
    loop.exec();

    ASSERT_TRUE(got);
    EXPECT_EQ(torrents.size(), 1);
    EXPECT_EQ(*torrents.first().name, "old");
    EXPECT_TRUE(torrents.first().sequentialDownload == std::nullopt);
    EXPECT_TRUE(torrents.first().labels == std::nullopt);

    const auto req = server.requests().last();
    EXPECT_FALSE(req.arguments.contains("format"));
    const QStringList fields = [&]() {
        QStringList out;
        for (const auto& v : req.arguments.value("fields").toArray())
            out << v.toString();
        return out;
    }();
    EXPECT_FALSE(fields.contains("labels"));
    EXPECT_FALSE(fields.contains("sequential_download"));
}

TEST_F(RpcClientTest, AddTorrentSequentialGate)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200,
        "{\"result\": \"success\", \"arguments\": {\"torrent-added\": {\"id\": 9}}}", {}});

    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&,
                                    const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    RpcClient::AddTorrent add;
    add.source = RpcClient::AddTorrent::Source::File;
    add.file = "C:\\torrents\\movie.torrent";
    add.downloadDir = "D:\\downloads";
    add.peerLimit = 50;
    add.start = false;
    add.sequential = true;
    client->addTorrent(add);
    loop.exec();

    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-add");
    EXPECT_EQ(req.arguments.value("filename").toString(), "C:\\torrents\\movie.torrent");
    EXPECT_EQ(req.arguments.value("download-dir").toString(), "D:\\downloads");
    EXPECT_TRUE(req.arguments.value("paused").toBool());
    EXPECT_EQ(req.arguments.value("peer-limit").toInt(), 50);
    EXPECT_TRUE(req.arguments.value("sequential_download").toBool());
}

TEST_F(RpcClientTest, AddTorrentRpc17NoSequential)
{
    connectWith(17);
    server.enqueue(MockRpcServer::Response{
        200, "{\"result\": \"success\", \"arguments\": {\"torrent-added\": {\"id\": 9}}}", {}});

    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&,
                                    const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    RpcClient::AddTorrent add;
    add.source = RpcClient::AddTorrent::Source::Url;
    add.url = "magnet:?xt=urn:btih:abc123";
    add.sequential = true; // rpc 17 < 18 — не шлём
    add.start = true;
    client->addTorrent(add);
    loop.exec();

    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.arguments.value("filename").toString(), "magnet:?xt=urn:btih:abc123");
    EXPECT_FALSE(req.arguments.value("paused").toBool());
    EXPECT_FALSE(req.arguments.contains("sequential_download"));
    EXPECT_FALSE(req.arguments.contains("peer-limit")); // 0 — не устанавливать
}

TEST_F(RpcClientTest, RemoveWithLocalData)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&,
                                    const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->removeTorrent(3, true);
    loop.exec();
    ASSERT_TRUE(done);

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-remove");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 3);
    EXPECT_TRUE(req.arguments.value("delete-local-data").toBool());
}

TEST_F(RpcClientTest, StartAllWithoutIds)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&,
                                    const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->startAll(true);
    loop.exec();
    ASSERT_TRUE(done);

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-start-now");
    // Без ids — все торренты.
    EXPECT_FALSE(req.arguments.contains("ids"));
}

TEST_F(RpcClientTest, SetFilePriorityHigh)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->setFilePriority(7, {2, 4}, 1); // high
    loop.exec();
    ASSERT_TRUE(done);

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
    // high → files-wanted + priority-high (тот же массив индексов), без остальных.
    EXPECT_EQ(req.arguments.value("files-wanted").toArray().size(), 2);
    EXPECT_TRUE(req.arguments.contains("priority-high"));
    EXPECT_EQ(req.arguments.value("priority-high").toArray().size(), 2);
    EXPECT_FALSE(req.arguments.contains("priority-normal"));
    EXPECT_FALSE(req.arguments.contains("priority-low"));
}

TEST_F(RpcClientTest, SetFilePriorityLow)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->setFilePriority(7, {1}, -1);
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_TRUE(req.arguments.contains("priority-low"));
    EXPECT_FALSE(req.arguments.contains("priority-high"));
    EXPECT_FALSE(req.arguments.contains("priority-normal"));
}

TEST_F(RpcClientTest, SetFilePriorityNormal)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->setFilePriority(7, {1, 3}, 0);
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_TRUE(req.arguments.contains("priority-normal"));
    EXPECT_FALSE(req.arguments.contains("priority-low"));
    EXPECT_FALSE(req.arguments.contains("priority-high"));
}

TEST_F(RpcClientTest, SetFilePriorityEmptyIsNoop)
{
    connectWith(18);
    const int before = server.requestCount();
    client->setFilePriority(7, {}, 1); // пустой список — запрос не шлём
    EXPECT_EQ(server.requestCount(), before);
}

// Приоритет всего торрента (W04): torrent-set с ключом bandwidthPriority
// (camelCase, как в образце). Регистр важен: lowercase-ключ демон игнорирует.
TEST_F(RpcClientTest, SetTorrentPriorityKeyCase)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->setPriority(7, 1); // high
    loop.exec();
    ASSERT_TRUE(done);

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
    EXPECT_TRUE(req.arguments.contains("bandwidthPriority"));
    EXPECT_EQ(req.arguments.value("bandwidthPriority").toInt(), 1);
    EXPECT_FALSE(req.arguments.contains("bandwidthpriority")); // не lowercase
}

TEST_F(RpcClientTest, SetFilePriorityVersionGate)
{
    connectWith(4); // приоритет файлов — только от RPC 5
    const int before = server.requestCount();

    QEventLoop loop;
    bool done = false;
    bool ok = true;
    QString error;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &ok, &error, &loop](const QString&, bool o, const QString& e,
                                                 const QJsonObject&) {
                         done = true;
                         ok = o;
                         error = e;
                         loop.quit();
                     });
    timer.start(5000);
    client->setFilePriority(7, {1}, 1);
    // Gate RPC-версии отвечает синхронно — loop может не понадобиться.
    if (!done)
        loop.exec();

    ASSERT_TRUE(done);
    EXPECT_FALSE(ok);
    EXPECT_NE(error.indexOf("5"), -1);
    EXPECT_EQ(server.requestCount(), before); // запрос в сеть не уходил
}

TEST_F(RpcClientTest, QueueMoveBottom)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->queueMoveBottom(7);
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "queue-move-bottom");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
}

TEST_F(RpcClientTest, StartTorrentForce)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->startTorrent(7, true); // force → torrent-start-now
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-start-now");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
}

TEST_F(RpcClientTest, UpdateBlocklist)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->updateBlocklist();
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "blocklist-update");
}

TEST_F(RpcClientTest, SetSequentialEnabled)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->setSequential(7, true);
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
    EXPECT_TRUE(req.arguments.value("sequential_download").toBool());
}

TEST_F(RpcClientTest, SetSequentialDisabled)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->setSequential(7, false);
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_TRUE(req.arguments.contains("sequential_download"));
    EXPECT_FALSE(req.arguments.value("sequential_download").toBool());
}

TEST_F(RpcClientTest, SetSequentialVersionGate)
{
    connectWith(17); // sequential_download — только от RPC 18
    const int before = server.requestCount();

    QEventLoop loop;
    bool done = false;
    bool ok = true;
    QString error;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &ok, &error, &loop](const QString&, bool o, const QString& e,
                                                 const QJsonObject&) {
                         done = true;
                         ok = o;
                         error = e;
                         loop.quit();
                     });
    timer.start(5000);
    client->setSequential(7, true);
    // Gate RPC-версии отвечает синхронно — loop может не понадобиться.
    if (!done)
        loop.exec();

    ASSERT_TRUE(done);
    EXPECT_FALSE(ok);
    EXPECT_NE(error.indexOf("18"), -1);
    EXPECT_EQ(server.requestCount(), before); // запрос в сеть не уходил
}

TEST_F(RpcClientTest, RenamePath)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->renamePath(7, "Old Name", "New Name");
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-rename-path");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
    EXPECT_EQ(req.arguments.value("path").toString(), "Old Name");
    EXPECT_EQ(req.arguments.value("name").toString(), "New Name");
}

TEST_F(RpcClientTest, RenamePathVersionGate)
{
    connectWith(14); // torrent-rename-path — только от RPC 15
    const int before = server.requestCount();

    QEventLoop loop;
    bool done = false;
    bool ok = true;
    QString error;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &ok, &error, &loop](const QString&, bool o, const QString& e,
                                                 const QJsonObject&) {
                         done = true;
                         ok = o;
                         error = e;
                         loop.quit();
                     });
    timer.start(5000);
    client->renamePath(7, "Old", "New");
    if (!done)
        loop.exec();
    ASSERT_TRUE(done);
    EXPECT_FALSE(ok);
    EXPECT_NE(error.indexOf("15"), -1);
    EXPECT_EQ(server.requestCount(), before); // запрос в сеть не уходил
}

TEST_F(RpcClientTest, ConnectAuth401)
{
    // 401 на session-get → authenticationFailed (а не connectionFailed).
    server.enqueue(MockRpcServer::Response{401, "Unauthorized", {}});
    QEventLoop loop;
    bool authFailed = false;
    bool connFailed = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::authenticationFailed,
                     [&authFailed, &loop](const QString&) { authFailed = true; loop.quit(); });
    QObject::connect(client.get(), &RpcClient::connectionFailed,
                     [&connFailed, &loop](const QString&) { connFailed = true; loop.quit(); });
    timer.start(5000);
    client->connectToDaemon();
    loop.exec();
    EXPECT_TRUE(authFailed);
    EXPECT_FALSE(connFailed);
}

TEST_F(RpcClientTest, ReportAuth401)
{
    connectWith(18);
    // 401 на изменяющем запросе → authenticationFailed (а не requestCompleted).
    server.enqueue(MockRpcServer::Response{401, "Unauthorized", {}});
    QEventLoop loop;
    bool authFailed = false;
    bool completed = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::authenticationFailed,
                     [&authFailed, &loop](const QString&) { authFailed = true; loop.quit(); });
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&completed, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         completed = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->startTorrent(7);
    loop.exec();
    EXPECT_TRUE(authFailed);
    EXPECT_FALSE(completed);
}

TEST_F(RpcClientTest, AddTracker)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->addTracker(7, "http://tracker.example/announce");
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
    auto urls = req.arguments.value("trackerAdd").toArray();
    ASSERT_EQ(urls.size(), 1);
    EXPECT_EQ(urls.first().toString(), "http://tracker.example/announce");
    EXPECT_FALSE(req.arguments.contains("trackerRemove"));
    EXPECT_FALSE(req.arguments.contains("trackerReplace"));
}

TEST_F(RpcClientTest, RemoveTracker)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->removeTracker(7, 2); // trackerId = 2
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
    auto ids = req.arguments.value("trackerRemove").toArray();
    ASSERT_EQ(ids.size(), 1);
    EXPECT_EQ(ids.first().toInt(), 2);
    EXPECT_FALSE(req.arguments.contains("trackerAdd"));
    EXPECT_FALSE(req.arguments.contains("trackerReplace"));
}

TEST_F(RpcClientTest, ReplaceTracker)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{200, "{\"result\": \"success\", \"arguments\": {}}", {}});
    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&, const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    client->replaceTracker(7, 2, "http://new.example/announce");
    loop.exec();
    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-set");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 7);
    auto pair = req.arguments.value("trackerReplace").toArray();
    ASSERT_EQ(pair.size(), 2); // [trackerId, newUrl]
    EXPECT_EQ(pair.at(0).toInt(), 2);
    EXPECT_EQ(pair.at(1).toString(), "http://new.example/announce");
    EXPECT_FALSE(req.arguments.contains("trackerAdd"));
    EXPECT_FALSE(req.arguments.contains("trackerRemove"));
}

TEST_F(RpcClientTest, AddTrackerEmptyUrlNoop)
{
    connectWith(18);
    const int before = server.requestCount();
    client->addTracker(7, "   "); // пустой URL — запрос не шлём
    EXPECT_EQ(server.requestCount(), before);
}

TEST_F(RpcClientTest, TrackerVersionGate)
{
    connectWith(9); // трекеры — только от RPC 10
    const int before = server.requestCount();

    QEventLoop loop;
    bool done = false;
    bool ok = true;
    QString error;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &ok, &error, &loop](const QString&, bool o, const QString& e,
                                                 const QJsonObject&) {
                         done = true;
                         ok = o;
                         error = e;
                         loop.quit();
                     });
    timer.start(5000);
    client->addTracker(7, "http://tracker.example/announce");
    // Gate RPC-версии отвечает синхронно — loop может не понадобиться.
    if (!done)
        loop.exec();

    ASSERT_TRUE(done);
    EXPECT_FALSE(ok);
    EXPECT_NE(error.indexOf("10"), -1);
    EXPECT_EQ(server.requestCount(), before); // запрос в сеть не уходил
}

TEST_F(RpcClientTest, SetLabelsVersionGate)
{
    connectWith(15); // labels — только от RPC 16
    const int before = server.requestCount();

    QEventLoop loop;
    bool done = false;
    bool ok = true;
    QString error;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &ok, &error, &loop](const QString&, bool o, const QString& e,
                                                 const QJsonObject&) {
                         done = true;
                         ok = o;
                         error = e;
                         loop.quit();
                     });
    timer.start(5000);
    client->setLabels(1, {"a", "b"});
    // Gate RPC-версии отвечает синхронно — loop может не понадобиться.
    if (!done)
        loop.exec();

    ASSERT_TRUE(done);
    EXPECT_FALSE(ok);
    EXPECT_NE(error.indexOf("16"), -1);
    EXPECT_EQ(server.requestCount(), before); // запрос в сеть не уходил
}

TEST_F(RpcClientTest, AddTorrentLocalFileSendsMetainfo)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200,
        "{\"result\": \"success\", \"arguments\": {\"torrent-added\": {\"id\": 9}}}", {}});

    // Файл существует локально → клиент шлёт метаданные (base64), а не путь:
    // демону (на другой машине) путь клиента недоступен.
    const QByteArray content = "d4:infol6:lengthi1024e4:name14:test-file.bine12:piece "
                               "lengthi16384e6:pieces20:0000000000000000000000000000ee";
    const QString path = QDir::tempPath() + QStringLiteral("/tgui-test-add.torrent");
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        ASSERT_EQ(f.write(content), static_cast<qint64>(content.size()));
    }

    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&,
                                    const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    RpcClient::AddTorrent add;
    add.source = RpcClient::AddTorrent::Source::File;
    add.file = path;
    client->addTorrent(add);
    loop.exec();

    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-add");
    EXPECT_EQ(req.arguments.value("metainfo").toString(),
              QString::fromLatin1(content.toBase64()));
    EXPECT_FALSE(req.arguments.contains("filename"));
    QFile::remove(path);
}

TEST_F(RpcClientTest, AddTorrentMissingFileSendsPath)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200,
        "{\"result\": \"success\", \"arguments\": {\"torrent-added\": {\"id\": 9}}}", {}});

    QEventLoop loop;
    bool done = false;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::requestCompleted,
                     [&done, &loop](const QString&, bool, const QString&,
                                    const QJsonObject&) {
                         done = true;
                         loop.quit();
                     });
    timer.start(5000);
    RpcClient::AddTorrent add;
    add.source = RpcClient::AddTorrent::Source::File;
    add.file = QStringLiteral("C:\\no-such\\path.torrent"); // не существует
    client->addTorrent(add);
    loop.exec();

    ASSERT_TRUE(done);
    const auto req = server.requests().last();
    EXPECT_EQ(req.arguments.value("filename").toString(),
              QStringLiteral("C:\\no-such\\path.torrent"));
    EXPECT_FALSE(req.arguments.contains("metainfo"));
}

TEST_F(RpcClientTest, PeersReceived)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200,
        "{\"result\": \"success\", \"arguments\": {\"torrents\": [{\"peers\": "
        "[{\"address\": \"1.2.3.4\", \"port\": 5000, \"clientName\": "
        "\"transmission/4.1.0\", \"flagStr\": \"EFH\", \"progress\": 10.0, "
        "\"rateToClient\": 900, \"rateToPeer\": 0}]}]}}",
        {}});

    QEventLoop loop;
    bool got = false;
    QVector<Peer> peers;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::peersReceived,
                     [&got, &peers, &loop](int, const QVector<Peer>& p) {
                         got = true;
                         peers = p;
                         loop.quit();
                     });
    timer.start(5000);
    client->getPeers(1);
    loop.exec();

    ASSERT_TRUE(got);
    ASSERT_EQ(peers.size(), 1);
    EXPECT_EQ(peers.first().address, "1.2.3.4");
    EXPECT_EQ(peers.first().rateToClient, 900);

    const auto req = server.requests().last();
    EXPECT_EQ(req.rpcMethod, "torrent-get");
    EXPECT_EQ(req.arguments.value("ids").toArray().first().toInt(), 1);
    EXPECT_EQ(req.arguments.value("fields").toArray().first().toString(), "peers");
}

TEST_F(RpcClientTest, ConfiguresNewTorrentBeforeStarting)
{
    connectWith(18);
    for (int i = 0; i < 4; ++i)
        server.enqueue({200, R"({"result":"success","arguments":{}})", {}});

    TorrentDetails details;
    details.id = 7;
    details.name = "Original.mkv";
    details.downloadDir = "/old";
    details.peerLimit = 20;
    RpcClient::AddTorrent options;
    options.downloadDir = "/new";
    options.peerLimit = 40;
    options.sequential = true;
    options.start = true;

    QEventLoop loop;
    bool ok = false;
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    client->configureAddedTorrent(details, options, "Movie.mkv", {1},
        [&](const JsonRpcResult& result) {
            ok = result.ok;
            loop.quit();
        });
    loop.exec();
    ASSERT_TRUE(ok);

    const auto requests = server.requests();
    ASSERT_GE(requests.size(), 4);
    const auto first = requests.size() - 4;
    EXPECT_EQ(requests.at(first).rpcMethod, "torrent-set-location");
    EXPECT_EQ(requests.at(first).arguments.value("location").toString(), "/new");
    EXPECT_FALSE(requests.at(first).arguments.value("move").toBool());
    EXPECT_EQ(requests.at(first + 1).rpcMethod, "torrent-set");
    EXPECT_EQ(requests.at(first + 1).arguments.value("peer-limit").toInt(), 40);
    EXPECT_TRUE(requests.at(first + 1).arguments.value("sequential_download").toBool());
    EXPECT_EQ(requests.at(first + 1).arguments.value("files-unwanted").toArray(), QJsonArray{1});
    EXPECT_EQ(requests.at(first + 2).rpcMethod, "torrent-rename-path");
    EXPECT_EQ(requests.at(first + 2).arguments.value("name").toString(), "Movie.mkv");
    EXPECT_EQ(requests.at(first + 3).rpcMethod, "torrent-start");
}

TEST_F(RpcClientTest, TorrentDetailsVerificationProgress)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200,
        R"({"result":"success","arguments":{"torrents":[
            ["id","files","priorities","wanted","downloadDir","status","recheckProgress","pieceCount"],
            [7,[
                {"begin_piece":0,"bytesCompleted":1000,"end_piece":5,"length":1000,"name":"a.bin"},
                {"begin_piece":5,"bytesCompleted":1000,"end_piece":10,"length":1000,"name":"b.bin"}
            ],[0,0],[1,1],"/dl",2,0.5,10]
        ]}})",
        {}});

    QEventLoop loop;
    bool got = false;
    TorrentDetails details;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::torrentDetailsReceived,
                     [&got, &details, &loop](const TorrentDetails& d) {
                         got = true;
                         details = d;
                         loop.quit();
                     });
    timer.start(5000);
    client->getTorrentDetails(7);
    loop.exec();

    ASSERT_TRUE(got);
    EXPECT_EQ(details.status, static_cast<int>(TorrentStatus::Check));
    EXPECT_EQ(details.pieceCount, 10);
    ASSERT_EQ(details.files.size(), 2);
    // Проверено 5 из 10 кусков: первый файл проверен полностью, второй — нет.
    EXPECT_DOUBLE_EQ(details.files.at(0).checkProgress, 1.0);
    EXPECT_DOUBLE_EQ(details.files.at(1).checkProgress, 0.0);
    // bytesCompleted при этом остаётся 100% (демон не сбрасывает have-биты).
    EXPECT_DOUBLE_EQ(details.files.at(0).completion, 1.0);

    const auto req = server.requests().last();
    const QStringList fields = [&]() {
        QStringList out;
        for (const auto& v : req.arguments.value("fields").toArray())
            out << v.toString();
        return out;
    }();
    EXPECT_TRUE(fields.contains("recheckProgress"));
    EXPECT_TRUE(fields.contains("pieceCount"));
    EXPECT_TRUE(fields.contains("status"));
}

TEST_F(RpcClientTest, SessionStatsReceived)
{
    connectWith(18);
    server.enqueue(MockRpcServer::Response{
        200,
        "{\"result\": \"success\", \"arguments\": "
        "{\"current-stats\": {\"downloadedBytes\": 100, \"uploadedBytes\": 50, "
        "\"filesAdded\": 1, \"secondsActive\": 60}, "
        "\"cumulative-stats\": {\"downloadedBytes\": 200, \"uploadedBytes\": 80, "
        "\"filesAdded\": 2, \"secondsActive\": 120}}}",
        {}});

    QEventLoop loop;
    bool got = false;
    SessionStats stats;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(client.get(), &RpcClient::sessionStatsReceived,
                     [&got, &stats, &loop](const SessionStats& s) {
                         got = true;
                         stats = s;
                         loop.quit();
                     });
    timer.start(5000);
    client->getSessionStats();
    loop.exec();

    ASSERT_TRUE(got);
    EXPECT_DOUBLE_EQ(stats.current.downloadedBytes, 100);
    EXPECT_EQ(stats.cumulative.filesAdded, 2);
}

// Живой демон: цепочка getTrackers (пропуск, если не заданы TGUI_E2E_*).
TEST(RpcLive, Trackers)
{
    const QString url = qgetenv("TGUI_E2E_URL");
    const QString user = qgetenv("TGUI_E2E_USER");
    const QString pass = qgetenv("TGUI_E2E_PASS");
    if (url.isEmpty() || user.isEmpty())
        GTEST_SKIP() << "TGUI_E2E_URL/TGUI_E2E_USER не заданы";

    RpcClient::Options o;
    o.url = QUrl(url);
    o.username = user;
    o.password = pass;
    auto client = std::make_unique<RpcClient>(o);
    RpcClient* c = client.get();

    auto waitSlot = [&](auto memberSignal, auto readyPred, auto fire) {
        QEventLoop loop;
        QObject::connect(c, memberSignal, [&](auto&&... args) {
            if (readyPred(args...))
                loop.quit();
        });
        QObject::connect(c, &RpcClient::connectionFailed,
                         [&loop](const QString&) { loop.quit(); });
        QTimer t;
        t.setSingleShot(true);
        QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
        t.start(15000);
        fire();
        loop.exec();
    };

    bool connected = false;
    waitSlot(&RpcClient::connected,
             [&connected](const SessionInfo&) { connected = true; return true; },
             [c] { c->connectToDaemon(); });
    ASSERT_TRUE(connected);

    int id = -1;
    waitSlot(&RpcClient::torrentsReceived,
             [&id](const QVector<Torrent>& ts) {
                 if (!ts.isEmpty())
                     id = ts.first().id;
                 return !ts.isEmpty();
             },
             [c] { c->getTorrents(); });
    ASSERT_GT(id, 0);

    bool got = false;
    QVector<TrackerStat> trackers;
    bool readErr = false;
    QString errMsg;
    QObject::connect(c, &RpcClient::readError,
                     [&readErr, &errMsg](const QString& what, const QString& err) {
                         readErr = true;
                         errMsg = what + ": " + err;
                     });
    waitSlot(&RpcClient::trackersReceived,
             [&got, &trackers, id](int i, const QVector<TrackerStat>& ts) {
                 if (i != id)
                     return false;
                 got = true;
                 trackers = ts;
                 return true;
             },
             [c, id] { c->getTrackers(id); });

    fprintf(stderr, "DIAG got=%d readErr=%d id=%d msg=%s trackers=%d\n",
            got ? 1 : 0, readErr ? 1 : 0, id, errMsg.toUtf8().constData(),
            static_cast<int>(trackers.size()));
    fflush(stderr);
    EXPECT_TRUE(got) << errMsg.toStdString();
    if (!trackers.isEmpty())
        fprintf(stderr, "DIAG first tracker: %s\n",
                trackers.first().announce.toUtf8().constData());
}

} // namespace

TEST_F(RpcClientTest, MoveUsesSetLocationWithExplicitMoveFlag)
{
    connectWith(14);
    for (bool move : {true, false}) {
        server.enqueue({});
        QEventLoop loop;
        bool completed = false;
        const auto connection = QObject::connect(client.get(), &RpcClient::requestCompleted,
            [&](const QString& method, bool ok, const QString&, const QJsonObject&) {
                if (method != "torrent-set-location") return;
                completed = ok; loop.quit();
            });
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.setSingleShot(true); timer.start(2500);
        client->setLocation(42, "/server/target", move);
        loop.exec();
        QObject::disconnect(connection);
        ASSERT_TRUE(completed);
        const auto request = server.requests().last();
        EXPECT_EQ(request.rpcMethod, QStringLiteral("torrent-set-location"));
        EXPECT_EQ(request.arguments.value("location").toString(), QStringLiteral("/server/target"));
        EXPECT_EQ(request.arguments.value("move").toBool(), move);
        EXPECT_EQ(request.arguments.value("ids").toArray().first().toInt(), 42);
    }
}

TEST_F(RpcClientTest, AddTorrentRepairsMetadataInMemory)
{
    connectWith(18);
    const QByteArray info = "d6:lengthi1e4:name1:x12:piece lengthi16384e6:pieces20:12345678901234567890e";
    const QByteArray good = "d8:announce22:http://tracker.example4:info" + info + "e";
    const QByteArray corrupt = "d8:announce22://tracker.example4:info" + info + "e";
    const QByteArray broken = "d4:infod6:pieces20:truncated";
    for (auto source : {RpcClient::AddTorrent::Source::File, RpcClient::AddTorrent::Source::Base64}) {
        for (const QByteArray& content : {good, corrupt, broken}) {
            QTemporaryFile file;
            ASSERT_TRUE(file.open());
            ASSERT_EQ(file.write(content), content.size());
            ASSERT_TRUE(file.flush());
            server.enqueue(MockRpcServer::Response{
                200, "{\"result\":\"success\",\"arguments\":{}}", {}});
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            bool done = false;
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            const auto connection = QObject::connect(
                client.get(), &RpcClient::requestCompleted, &loop,
                [&](const QString& method, bool, const QString&, const QJsonObject&) {
                    if (method == "torrent-add") {
                        done = true;
                        loop.quit();
                    }
                });
            RpcClient::AddTorrent add;
            add.source = source;
            add.file = file.fileName();
            add.metainfoBase64 = QString::fromLatin1(content.toBase64());
            add.start = true;
            add.downloadDir = "/downloads/repaired";
            timer.start(5000);
            client->addTorrent(add);
            loop.exec();
            QObject::disconnect(connection);
            ASSERT_TRUE(done);
            const auto request = server.requests().last();
            const auto sent = QByteArray::fromBase64(request.arguments.value("metainfo").toString().toLatin1());
            EXPECT_EQ(sent, content == corrupt ? good : content);
            EXPECT_FALSE(request.arguments.value("paused").toBool());
            EXPECT_EQ(request.arguments.value("download-dir").toString(), add.downloadDir);
            ASSERT_TRUE(file.seek(0));
            EXPECT_EQ(file.readAll(), content);
        }
    }
}
