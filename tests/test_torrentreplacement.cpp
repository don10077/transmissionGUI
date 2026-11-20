#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>

#include "mockhttpserver.h"
#include "displaynames.h"
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
QJsonObject details(bool replacement, int status, QString root = "Season", int fileSize = 100,
                    int completed = 100, int fileCount = -1)
{
    QJsonArray files;
    const int count = fileCount >= 0 ? fileCount : (replacement ? 3 : 2);
    for (int i = 1; i <= count; ++i)
        files.append(QJsonObject{{"name", root + QString("/%1.mkv").arg(i, 2, 10, QLatin1Char('0'))},
                                 {"length", fileSize}, {"bytesCompleted", replacement && i == 3 ? 0 : completed}});
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
        setMetainfo({"01.mkv", "02.mkv", "03.mkv"});
        ASSERT_NE(server.start(), 0);
    }

    /** Создаёт новые метаданные, включая вложенные пути. */
    void setMetainfo(const QStringList& paths)
    {
        using tgui::bencode::Value;
        Value::List files;
        for (const auto& name : paths) {
            Value::List components;
            for (const auto& component : name.split('/'))
                components.push_back(Value::string(component.toStdString()));
            files.push_back(Value::dictionary({
                {"length", Value::integer(100)}, {"path", Value::list(std::move(components))},
            }));
        }
        const auto bytes = tgui::bencode::encode(Value::dictionary({{"info", Value::dictionary({
            {"files", Value::list(std::move(files))}, {"name", Value::string("Season")},
            {"piece length", Value::integer(16384)}, {"pieces", Value::string(std::string(20, 'p'))},
        })}}));
        metainfo = QByteArray::fromStdString(bytes);
    }

    void TearDown() override
    {
        displaynames::remove(oldHash);
        displaynames::remove(newHash);
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
    displaynames::set(oldHash, QStringLiteral("My series"));
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
    EXPECT_TRUE(displaynames::storedName(newHash).isEmpty());
    confirmation->button(QMessageBox::Yes)->click();
    ASSERT_TRUE(await([&] { return finished != -2; }));
    EXPECT_EQ(finished, 2);
    EXPECT_EQ(displaynames::storedName(newHash), QStringLiteral("My series"));
    const auto requests = server.requests();
    ASSERT_EQ(requests.size(), GetParam() ? 13 : 12);
    EXPECT_EQ(requests.at(9).rpcMethod, QString("torrent-set"));
    EXPECT_EQ(requests.at(9).arguments.value("files-unwanted").toArray(), (QJsonArray{1, 2}));
    EXPECT_EQ(requests.at(9).arguments.value("files-wanted").toArray(), QJsonArray{0});
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
    displaynames::set(oldHash, QStringLiteral("My series"));
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
    EXPECT_EQ(displaynames::storedName(oldHash), QStringLiteral("My series"));
    EXPECT_TRUE(displaynames::storedName(newHash).isEmpty());
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

class TorrentReplacementUpdatedTest : public TorrentReplacementTest, public testing::WithParamInterface<int> {};

TEST_P(TorrentReplacementUpdatedTest, ReviewsAndCommitsChangedContentsAfterVerification)
{
    tgui::RpcClient::Options connection;
    connection.url = server.url();
    QWidget window;
    tgui::RpcClient rpc(connection);
    const auto root = GetParam() == 0 ? QStringLiteral("Other Season") : QStringLiteral("Season");
    const int oldSize = GetParam() == 1 ? 120 : 100;
    const int oldCount = GetParam() == 2 ? 3 : (GetParam() == 3 ? 4 : 2);
    const int verified = GetParam() == 4 ? 0 : 50;
    const int oldCompleted = GetParam() == 5 ? 0 : 100;
    const auto oldRunning = details(false, 6, root, oldSize, oldCompleted, oldCount);
    const auto oldStopped = details(false, 0, root, oldSize, oldCompleted, oldCount);
    const auto newStopped = details(true, 0, root, 100, verified);
    enqueue(oldRunning);
    enqueue();
    enqueue(oldStopped);
    enqueue({{"torrent-added", QJsonObject{{"id", 2}, {"hashString", newHash}}}});
    if (GetParam() == 0)
        enqueue(); // torrent-rename-path до проверки файлов
    enqueue();
    enqueue(newStopped);
    enqueue(newStopped);
    enqueue();
    enqueue(oldStopped);
    enqueue();
    int finished = -2;
    replaceTorrent(&window, &rpc, oldHash, metainfo, {}, {}, [&](int id) { finished = id; });
    ASSERT_TRUE(await([&] { return window.findChild<QMessageBox*>(); }));
    auto* confirmation = window.findChild<QMessageBox*>();
    ASSERT_NE(confirmation->button(QMessageBox::Yes), nullptr);
    EXPECT_TRUE(confirmation->text().contains(verified == 0 ? QStringLiteral("Remaining to download: 200 b")
                                                          : QStringLiteral("Remaining to download: 150 b")));
    EXPECT_TRUE(confirmation->text().contains(verified == 0 ? QStringLiteral("existing files: 0 b")
                                                          : QStringLiteral("existing files: 100 b")));
    EXPECT_EQ(finished, -2);
    EXPECT_EQ(server.requests().last().rpcMethod, QString("torrent-get"));
    confirmation->button(QMessageBox::Yes)->click();
    ASSERT_TRUE(await([&] { return finished != -2; }));
    EXPECT_EQ(finished, 2);
    ASSERT_EQ(server.requestCount(), GetParam() == 0 ? 11 : 10);
    const int verifyIndex = GetParam() == 0 ? 5 : 4;
    EXPECT_EQ(server.requests().at(verifyIndex).rpcMethod, QString("torrent-verify"));
    if (GetParam() == 0) {
        const auto rename = server.requests().at(4);
        EXPECT_EQ(rename.rpcMethod, QString("torrent-rename-path"));
        EXPECT_EQ(rename.arguments.value("ids").toArray(), QJsonArray{newHash});
        EXPECT_EQ(rename.arguments.value("path").toString(), QStringLiteral("Season"));
        EXPECT_EQ(rename.arguments.value("name").toString(), root);
    }
    EXPECT_EQ(displaynames::storedName(newHash), root);
    EXPECT_EQ(server.requests().last().rpcMethod, QString("torrent-remove"));
    EXPECT_EQ(server.requests().last().arguments.value("ids").toArray(), QJsonArray{oldHash});
    EXPECT_FALSE(server.requests().last().arguments.value("delete-local-data").toBool(true));
}

INSTANTIATE_TEST_SUITE_P(ChangedPathsSizesCountOrData, TorrentReplacementUpdatedTest, testing::Range(0, 6));

TEST_F(TorrentReplacementTest, PreservesRootAndDisplayNameWithNewNestedFolders)
{
    setMetainfo({"01.mkv", "02.mkv", "Extras/03.mkv"});
    displaynames::set(oldHash, QStringLiteral("Favourite series"));
    tgui::RpcClient::Options connection;
    connection.url = server.url();
    QWidget window;
    tgui::RpcClient rpc(connection);
    const auto oldRunning = details(false, 6, "My Series");
    const auto oldStopped = details(false, 0, "My Series");
    auto replacement = details(true, 0, "My Series");
    auto torrent = replacement.value("torrents").toArray().first().toObject();
    auto files = torrent.value("files").toArray();
    auto extra = files.at(2).toObject();
    extra.insert("name", "My Series/Extras/03.mkv");
    files[2] = extra;
    torrent.insert("files", files);
    replacement.insert("torrents", QJsonArray{torrent});
    enqueue(oldRunning);
    enqueue();
    enqueue(oldStopped);
    enqueue({{"torrent-added", QJsonObject{{"id", 2}, {"hashString", newHash}}}});
    enqueue(); // rename
    enqueue(); // verify
    enqueue(replacement);
    enqueue(replacement);
    enqueue(); // settings
    enqueue(oldStopped);
    enqueue(); // remove old entry
    int finished = -2;
    replaceTorrent(&window, &rpc, oldHash, metainfo, {}, {"Season/Extras/03.mkv"},
                   [&](int id) { finished = id; });
    ASSERT_TRUE(await([&] { return window.findChild<QMessageBox*>(); }));
    auto* confirmation = window.findChild<QMessageBox*>();
    ASSERT_NE(confirmation->button(QMessageBox::Yes), nullptr);
    EXPECT_TRUE(confirmation->text().contains("New files: 1"));
    EXPECT_TRUE(displaynames::storedName(newHash).isEmpty());
    confirmation->button(QMessageBox::Yes)->click();
    ASSERT_TRUE(await([&] { return finished != -2; }));
    EXPECT_EQ(finished, 2);
    EXPECT_EQ(displaynames::storedName(newHash), QStringLiteral("Favourite series"));
    const auto requests = server.requests();
    ASSERT_EQ(requests.size(), 11);
    EXPECT_EQ(requests.at(3).arguments.value("download-dir").toString(), QStringLiteral("/series"));
    EXPECT_EQ(requests.at(3).arguments.value("metainfo").toString().toLatin1(), metainfo.toBase64());
    EXPECT_EQ(requests.at(4).rpcMethod, QString("torrent-rename-path"));
    EXPECT_EQ(requests.at(4).arguments.value("name").toString(), QStringLiteral("My Series"));
    EXPECT_EQ(requests.at(5).rpcMethod, QString("torrent-verify"));
    EXPECT_EQ(requests.at(8).arguments.value("files-unwanted").toArray(), (QJsonArray{1, 2}));
    EXPECT_EQ(requests.at(8).arguments.value("priority-high").toArray(), QJsonArray{0});
    EXPECT_EQ(requests.at(8).arguments.value("priority-low").toArray(), QJsonArray{1});
}

TEST_F(TorrentReplacementTest, FailedRootRenameKeepsOldEntryAndDisplayName)
{
    displaynames::set(oldHash, QStringLiteral("Favourite series"));
    tgui::RpcClient::Options connection;
    connection.url = server.url();
    QWidget window;
    tgui::RpcClient rpc(connection);
    enqueue(details(false, 6, "My Series"));
    enqueue();
    enqueue(details(false, 0, "My Series"));
    enqueue({{"torrent-added", QJsonObject{{"id", 2}, {"hashString", newHash}}}});
    server.enqueue({200, R"({"result":"rename failed","arguments":{}})", {}});
    enqueue(); // remove new entry without data
    enqueue(); // verify old
    enqueue(); // resume old
    int finished = -2;
    replaceTorrent(&window, &rpc, oldHash, metainfo, {}, {}, [&](int id) { finished = id; });
    ASSERT_TRUE(await([&] { return finished != -2; }));
    EXPECT_EQ(finished, -1);
    EXPECT_EQ(displaynames::storedName(oldHash), QStringLiteral("Favourite series"));
    EXPECT_TRUE(displaynames::storedName(newHash).isEmpty());
    const auto requests = server.requests();
    ASSERT_EQ(requests.size(), 8);
    EXPECT_EQ(requests.at(5).rpcMethod, QString("torrent-remove"));
    EXPECT_EQ(requests.at(5).arguments.value("ids").toArray(), QJsonArray{newHash});
    EXPECT_FALSE(requests.at(5).arguments.value("delete-local-data").toBool(true));
    EXPECT_EQ(requests.last().rpcMethod, QString("torrent-start"));
    EXPECT_EQ(requests.last().arguments.value("ids").toArray(), QJsonArray{oldHash});
}

} // namespace
