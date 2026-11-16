#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>

#include "mockhttpserver.h"
#include "tgui/bencode.h"
#include "torrentreplacement.h"

namespace {

const QString oldHash(40, QLatin1Char('a'));
const QString newHash(40, QLatin1Char('b'));

/** Даёт RPC и диалогам обработать события до выполнения условия. */
bool await(const std::function<bool()>& condition)
{
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (condition())
            loop.quit();
    });
    poll.start(5);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    if (!condition())
        loop.exec();
    return condition();
}

/** Отчёт демона о старой или новой раздаче с накопленными данными. */
QJsonObject details(bool replacement, int status, QString root = "Season")
{
    QJsonArray files{QJsonObject{{"name", root + "/01.mkv"}, {"length", 100}, {"bytesCompleted", 100}},
                     QJsonObject{{"name", root + "/02.mkv"}, {"length", 100}, {"bytesCompleted", 100}}};
    if (replacement)
        files.append(QJsonObject{{"name", root + "/03.mkv"}, {"length", 100}, {"bytesCompleted", 0}});
    return {{"torrents", QJsonArray{QJsonObject{
        {"id", replacement ? 2 : 1}, {"hashString", replacement ? newHash : oldHash},
        {"name", root}, {"status", status}, {"downloadDir", "/series"},
        {"files", files}, {"priorities", QJsonArray{1, -1}}, {"wanted", QJsonArray{true, false}},
        {"labels", QJsonArray{"Series"}}, {"maxConnectedPeers", 25}}}}};
}

class TorrentReplacementTest : public testing::Test {
protected:
    void SetUp() override
    {
        using tgui::bencode::Value;
        Value::List files;
        for (const auto* name : {"01.mkv", "02.mkv", "03.mkv"}) {
            files.push_back(Value::dictionary({
                {"length", Value::integer(100)}, {"path", Value::list({Value::string(name)})},
            }));
        }
        const auto bytes = tgui::bencode::encode(Value::dictionary({{"info", Value::dictionary({
            {"files", Value::list(std::move(files))}, {"name", Value::string("Season")},
            {"piece length", Value::integer(16384)}, {"pieces", Value::string(std::string(20, 'p'))},
        })}}));
        metainfo = QByteArray::fromStdString(bytes);
        ASSERT_NE(server.start(), 0);
    }

    /** Добавляет в очередь один успешный legacy-ответ. */
    void enqueue(const QJsonObject& arguments = {})
    {
        server.enqueue({200, QJsonDocument(QJsonObject{{"result", "success"}, {"arguments", arguments}})
                                    .toJson(QJsonDocument::Compact), {}});
    }

    /** Подготавливает ответы до первого диалога подтверждения. */
    void queueVerification()
    {
        enqueue(details(false, 6));
        enqueue();
        enqueue(details(false, 0));
        enqueue({{"torrent-added", QJsonObject{{"id", 2}, {"hashString", newHash}}}});
        enqueue();
        enqueue(details(true, 2));
        enqueue(details(true, 0));
    }

    MockRpcServer server;
    QByteArray metainfo;
};

class TorrentReplacementCommitTest : public TorrentReplacementTest, public testing::WithParamInterface<bool> {};

TEST_P(TorrentReplacementCommitTest, AppliesCurrentChoicesOnlyAfterVerifiedAndConfirmed)
{
    enqueue({{"rpc-version", 16}, {"version", "4.0.6"}});
    tgui::RpcClient::Options connection;
    connection.url = server.url();
    QWidget window;
    tgui::RpcClient rpc(connection);
    rpc.connectToDaemon();
    ASSERT_TRUE(await([&] { return rpc.isConnected(); }));

    queueVerification();
    enqueue(details(true, 0));
    enqueue();
    enqueue(details(false, 0));
    enqueue();
    if (GetParam())
        enqueue();
    int finished = -2;
    tgui::RpcClient::AddTorrent options;
    options.start = GetParam();
    options.peerLimit = 40;
    options.labels = {QStringLiteral("Video")};
    replaceTorrent(&window, &rpc, oldHash, metainfo, options, {QStringLiteral("Season/03.mkv")},
                   [&](int id) { finished = id; });
    ASSERT_TRUE(await([&] { return window.findChild<QMessageBox*>(); }));
    auto* confirmation = window.findChild<QMessageBox*>();
    ASSERT_EQ(server.requestCount(), 8);
    EXPECT_EQ(server.requests().at(4).rpcMethod, QString("torrent-add"));
    EXPECT_EQ(server.requests().at(4).arguments.value("paused"), QJsonValue(true));
    EXPECT_EQ(server.requests().at(4).arguments.value("download-dir"), QJsonValue("/series"));
    ASSERT_NE(confirmation->button(QMessageBox::Yes), nullptr);
    confirmation->button(QMessageBox::Yes)->click();
    ASSERT_TRUE(await([&] { return finished != -2; }));
    EXPECT_EQ(finished, 2);
    const auto requests = server.requests();
    ASSERT_EQ(requests.size(), GetParam() ? 13 : 12);
    EXPECT_EQ(requests.at(9).rpcMethod, QString("torrent-set"));
    EXPECT_EQ(requests.at(9).arguments.value("files-unwanted").toArray(), QJsonArray{2});
    EXPECT_EQ(requests.at(9).arguments.value("files-wanted").toArray(), (QJsonArray{0, 1}));
    EXPECT_EQ(requests.at(9).arguments.value("peer-limit").toInt(), 40);
    EXPECT_EQ(requests.at(9).arguments.value("labels").toArray(), QJsonArray{"Video"});
    EXPECT_EQ(requests.at(11).rpcMethod, QString("torrent-remove"));
    EXPECT_EQ(requests.at(11).arguments.value("ids").toArray(), QJsonArray{oldHash});
    EXPECT_FALSE(requests.at(11).arguments.value("delete-local-data").toBool(true));
    if (GetParam()) {
        EXPECT_EQ(requests.at(12).rpcMethod, QString("torrent-start"));
        EXPECT_EQ(requests.at(12).arguments.value("ids").toArray(), QJsonArray{newHash});
    }
}

INSTANTIATE_TEST_SUITE_P(StartSetting, TorrentReplacementCommitTest, testing::Bool());

TEST_F(TorrentReplacementTest, CancelRemovesNewEntryAndRestoresOld)
{
    tgui::RpcClient::Options connection;
    connection.url = server.url();
    QWidget window;
    tgui::RpcClient rpc(connection);
    queueVerification();
    enqueue();
    enqueue();
    enqueue();
    int finished = -2;
    replaceTorrent(&window, &rpc, oldHash, metainfo, {}, {}, [&](int id) { finished = id; });
    ASSERT_TRUE(await([&] { return window.findChild<QMessageBox*>(); }));
    auto* cancel = window.findChild<QMessageBox*>()->button(QMessageBox::Cancel);
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    ASSERT_TRUE(await([&] { return finished != -2; }));
    EXPECT_EQ(finished, -1);
    const auto requests = server.requests();
    ASSERT_EQ(requests.size(), 10);
    EXPECT_EQ(requests.at(7).rpcMethod, QString("torrent-remove"));
    EXPECT_EQ(requests.at(7).arguments.value("ids").toArray(), QJsonArray{newHash});
    EXPECT_FALSE(requests.at(7).arguments.value("delete-local-data").toBool(true));
    EXPECT_EQ(requests.at(8).rpcMethod, QString("torrent-verify"));
    EXPECT_EQ(requests.at(8).arguments.value("ids").toArray(), QJsonArray{oldHash});
    EXPECT_EQ(requests.at(9).rpcMethod, QString("torrent-start"));
    EXPECT_EQ(requests.at(9).arguments.value("ids").toArray(), QJsonArray{oldHash});
}

TEST_F(TorrentReplacementTest, DifferentPathsDoNotPauseTheOldTorrent)
{
    tgui::RpcClient::Options connection;
    connection.url = server.url();
    QWidget window;
    tgui::RpcClient rpc(connection);
    enqueue(details(false, 6, "Other Season"));
    int finished = -2;
    replaceTorrent(&window, &rpc, oldHash, metainfo, {}, {}, [&](int id) { finished = id; });
    ASSERT_TRUE(await([&] { return finished != -2; }));
    EXPECT_EQ(finished, -1);
    ASSERT_EQ(server.requestCount(), 1);
    EXPECT_EQ(server.requests().first().rpcMethod, QString("torrent-get"));
}

} // namespace
