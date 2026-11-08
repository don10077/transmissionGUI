#include <algorithm>
#include <memory>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

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
    EXPECT_EQ(req.arguments.value("path").toString(), "D:\\downloads");
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

} // namespace