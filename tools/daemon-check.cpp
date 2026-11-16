// tgui_daemon_check — dev-инструмент: подключение к демону Transmission,
// вывод session-info + списка торрентов. (Приёмка этапа 1: «подключение к
// вашему демону и чтение списка».)
//
// Использование:
//   tgui_daemon_check <url> [--user <user>] [--pass <pass>] [--path <rpc-path>]
//   tgui_daemon_check <url> --test-add <file.torrent>  (приёмка этапа 3)
// Пример:
//   tgui_daemon_check http://127.0.0.1:9091 --user admin --pass secret
//
// --test-add: полный цикл Stage 3 по реальному демону:
//   connect → torrent-add (paused, peer-limit) → файлы (torrent-get) →
//   files-wanted/unwanted (torrent-set) → torrent-remove (без данных).
// Если файл не существует — создаётся генерируемый однофайловый .torrent.

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <cstdio>
#include <functional>

#include "tgui/bencode.h"
#include "tgui/jsonrpc.h"
#include "tgui/models.h"
#include "tgui/rpcclient.h"
#include "tgui/util.h"

namespace {

void printUsage()
{
    std::printf("Usage: tgui_daemon_check <url> [--user <user>] [--pass <pass>] "
                "[--path <rpc-path>]\n");
    std::printf("       tgui_daemon_check <url> --test-add <file.torrent> "
                "(e2e: add→files→wanted→remove)\n");
}

// Генерирует минимальный валидный однофайловый .torrent (ключи bencode — по
// возрастанию, как требует формат).
bool makeSampleTorrent(const QString& path)
{
    using namespace tgui::bencode;
    Value info = Value::dictionary({
        {"length", Value::integer(1024)},
        {"name", Value::string("tgui-e2e-test.bin")},
        {"piece length", Value::integer(16384)},
        {"pieces", Value::string(std::string(20, '\0'))},
    });
    Value torrent = Value::dictionary({
        {"announce", Value::string("http://127.0.0.1:6969/announce")},
        {"created by", Value::string("tgui_daemon_check")},
        {"info", std::move(info)},
    });
    const std::string enc = encode(torrent);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(enc.data(), static_cast<qint64>(enc.size())) == enc.size();
}

int runAddTest(const QString& url, const QString& rpcPath, const QString& user,
               const QString& pass, const QString& torrentFile)
{
    if (!QFile::exists(torrentFile) && !makeSampleTorrent(torrentFile)) {
        std::fprintf(stderr, "FAIL: не удалось создать тестовый .torrent: %s\n",
                     torrentFile.toUtf8().constData());
        return 1;
    }

    tgui::RpcClient::Options opts;
    opts.url = QUrl(url);
    opts.rpcPath = rpcPath;
    opts.username = user;
    opts.password = pass;
    tgui::RpcClient client(opts);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto wait = [&loop, &timer]() {
        timer.start(20000);
        loop.exec();
        timer.stop();
    };

    // 1. connect
    QString connErr;
    auto c1 = QObject::connect(&client, &tgui::RpcClient::connected, &loop,
                               &QEventLoop::quit);
    auto c2 = QObject::connect(&client, &tgui::RpcClient::connectionFailed, &loop,
                               [&](const QString& e) {
                                   connErr = e;
                                   loop.quit();
                               });
    client.connectToDaemon();
    wait();
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    if (!connErr.isEmpty()) {
        std::fprintf(stderr, "FAIL connect: %s\n", connErr.toUtf8().constData());
        return 1;
    }
    std::printf("OK  connect (RPC %d, dir %s)\n", client.rpcVersion(),
                client.sessionInfo().downloadDir.toUtf8().constData());

    struct Ctx {
        bool ok = false;
        QString error;
        QJsonObject args;
    };
    auto runStage = [&client, &wait, &loop](Ctx& ctx, const QString& method,
                                           const std::function<void()>& invoke) {
        ctx = Ctx{};
        auto conn = QObject::connect(
            &client, &tgui::RpcClient::requestCompleted,
            [&](const QString& m, bool ok, const QString& err, const QJsonObject& args) {
                if (m == method) {
                    ctx.ok = ok;
                    ctx.error = err;
                    ctx.args = args;
                    loop.quit();
                }
            });
        invoke();
        wait();
        QObject::disconnect(conn);
    };

    // 2. torrent-add (paused, peer-limit 50 — как в AddTorrentDialog)
    Ctx ctx;
    runStage(ctx, QStringLiteral("torrent-add"), [&] {
        tgui::RpcClient::AddTorrent p;
        p.source = tgui::RpcClient::AddTorrent::Source::File;
        p.file = torrentFile;
        p.downloadDir = client.sessionInfo().downloadDir;
        p.peerLimit = 50;
        p.start = false;
        client.addTorrent(p);
    });
    if (!ctx.ok) {
        std::fprintf(stderr, "FAIL torrent-add: %s\n", ctx.error.toUtf8().constData());
        return 1;
    }
    int id = ctx.args.value("torrent-added").toObject().value("id").toInt();
    if (id <= 0)
        id = ctx.args.value("id").toInt();
    if (id <= 0) {
        std::fprintf(stderr, "FAIL: нет id в ответе torrent-add: %s\n",
                     QJsonDocument(ctx.args).toJson(QJsonDocument::Compact).constData());
        return 1;
    }
    std::printf("OK  torrent-add (id=%d)\n", id);

    // 3. файлы (torrent-get details)
    tgui::TorrentDetails details;
    auto c3 = QObject::connect(
        &client, &tgui::RpcClient::torrentDetailsReceived, &loop,
        [&](const tgui::TorrentDetails& d) {
            details = d;
            loop.quit();
        });
    client.getTorrentDetails(id);
    wait();
    QObject::disconnect(c3);
    if (details.files.isEmpty()) {
        std::fprintf(stderr, "FAIL torrent-get: нет файлов (id=%d)\n", id);
    } else {
        for (const auto& f : details.files)
            std::printf("    file [%d] %s (%s) wanted=%d\n", f.index,
                        f.name.toUtf8().constData(), tgui::formatSize(f.size).c_str(),
                        static_cast<int>(f.wanted));
        std::printf("OK  files: %d\n", static_cast<int>(details.files.size()));
    }

    // 4. files-wanted/unwanted (torrent-set) — skip последнего файла
    if (!details.files.isEmpty()) {
        QVector<int> wanted;
        QVector<int> unwanted;
        for (qsizetype i = 0; i < details.files.size(); ++i)
            (i == details.files.size() - 1 ? unwanted : wanted)
                .push_back(details.files[i].index);
        runStage(ctx, QStringLiteral("torrent-set"),
                 [&] { client.setFilesWanted(id, wanted, unwanted); });
        if (ctx.ok)
            std::printf("OK  files-wanted (skip 1 file)\n");
        else
            std::fprintf(stderr, "FAIL torrent-set: %s\n",
                         ctx.error.toUtf8().constData());
    }

    // 5. torrent-remove (без данных) — уборка после теста
    runStage(ctx, QStringLiteral("torrent-remove"), [&] { client.removeTorrent(id, false); });
    if (!ctx.ok) {
        std::fprintf(stderr, "FAIL remove: %s\n", ctx.error.toUtf8().constData());
        return 1;
    }
    std::printf("OK  remove (без данных)\n");
    std::printf("E2E-ADD-TEST PASSED\n");
    return 0;
}

// Удаление торрентов по точному имени (очистка после теста): id+name → remove.
int runRemoveByName(const QString& url, const QString& rpcPath, const QString& user,
                    const QString& pass, const QString& name)
{
    tgui::RpcClient::Options opts;
    opts.url = QUrl(url);
    opts.rpcPath = rpcPath;
    opts.username = user;
    opts.password = pass;
    tgui::RpcClient client(opts);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto wait = [&loop, &timer]() {
        timer.start(20000);
        loop.exec();
        timer.stop();
    };

    QString connErr;
    auto c1 = QObject::connect(&client, &tgui::RpcClient::connected, &loop, &QEventLoop::quit);
    auto c2 = QObject::connect(&client, &tgui::RpcClient::connectionFailed, &loop,
                               [&](const QString& e) {
                                   connErr = e;
                                   loop.quit();
                               });
    client.connectToDaemon();
    wait();
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    if (!connErr.isEmpty()) {
        std::fprintf(stderr, "FAIL connect: %s\n", connErr.toUtf8().constData());
        return 1;
    }

    QVector<tgui::Torrent> list;
    auto c3 = QObject::connect(&client, &tgui::RpcClient::torrentsReceived, &loop,
                               [&](const QVector<tgui::Torrent>& t) {
                                   list = t;
                                   loop.quit();
                               });
    client.getTorrents({"id", "name"});
    wait();
    QObject::disconnect(c3);

    QVector<int> victims;
    for (const auto& t : list)
        if (t.name && *t.name == name)
            victims.push_back(t.id);
    if (victims.isEmpty()) {
        std::printf("NOT FOUND: %s (уже чисто?)\n", name.toUtf8().constData());
        return 0;
    }
    for (int id : victims) {
        bool ok = false;
        QString err;
        auto conn = QObject::connect(
            &client, &tgui::RpcClient::requestCompleted,
            [&ok, &err, &loop](const QString& m, bool o, const QString& e, const QJsonObject&) {
                if (m == QStringLiteral("torrent-remove")) {
                    ok = o;
                    err = e;
                    loop.quit();
                }
            });
        client.removeTorrent(id, false);
        wait();
        QObject::disconnect(conn);
        if (!ok) {
            std::fprintf(stderr, "FAIL remove #%d: %s\n", id, err.toUtf8().constData());
            return 1;
        }
        std::printf("REMOVED #%d (%s)\n", id, name.toUtf8().constData());
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QString url;
    QString user;
    QString pass;
    QString rpcPath;
    QString testAdd;
    QString removeName;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--user" && i + 1 < argc)
            user = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--pass" && i + 1 < argc)
            pass = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--path" && i + 1 < argc)
            rpcPath = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--test-add" && i + 1 < argc)
            testAdd = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--remove-name" && i + 1 < argc)
            removeName = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg.startsWith("--")) {
            std::fprintf(stderr, "Unknown option: %s\n", arg.toUtf8().constData());
            printUsage();
            return 2;
        } else {
            url = arg;
        }
    }
    if (url.isEmpty()) {
        printUsage();
        return 2;
    }
    if (!testAdd.isEmpty())
        return runAddTest(url, rpcPath, user, pass, testAdd);
    if (!removeName.isEmpty())
        return runRemoveByName(url, rpcPath, user, pass, removeName);

    tgui::JsonRpcClient rpc(url, rpcPath);
    if (!user.isEmpty())
        rpc.setUsernamePassword(user, pass);
    rpc.setTimeout(std::chrono::seconds(30));

    const tgui::JsonRpcResult session = rpc.callSync("session-get", QJsonObject{});
    if (!session.ok) {
        std::fprintf(stderr, "CONNECT FAILED: %s (HTTP %d)\n",
                     session.error.toUtf8().constData(), session.httpCode);
        return 1;
    }
    tgui::SessionInfo info;
    tgui::parseSessionInfo(session.arguments, info);
    std::printf("Connected: %s (RPC %d)\n", info.downloadDir.toUtf8().constData(),
                info.rpcVersion);
    if (info.rpcVersion >= 15) {
        const tgui::JsonRpcResult fs = rpc.callSync(
            "free-space", QJsonObject{{"path", info.downloadDir}});
        if (fs.ok)
            std::printf("Free space: %s\n",
                        tgui::formatSize(
                            fs.arguments.value("size-bytes").toDouble())
                            .c_str());
    }

    QStringList fields = {
        "id", "name", "status", "sizeWhenDone", "leftUntilDone", "rateDownload",
        "rateUpload", "eta", "peersSendingToUs", "peersGettingFromUs", "uploadRatio",
    };
    if (info.rpcVersion >= 16)
        fields << "labels";
    QJsonArray fieldArray;
    for (const QString& f : fields)
        fieldArray.append(f);
    QJsonObject args{{"fields", fieldArray}};
    if (info.rpcVersion >= 16)
        args.insert("format", "table");

    const tgui::JsonRpcResult torrents = rpc.callSync("torrent-get", args);
    if (!torrents.ok) {
        std::fprintf(stderr, "TORRENT-GET FAILED: %s\n",
                     torrents.error.toUtf8().constData());
        return 1;
    }
    const QJsonArray arr = torrents.arguments.value("torrents").toArray();
    const QVector<tgui::Torrent> list = (!arr.isEmpty() && arr.first().isArray())
                                            ? tgui::parseTorrentsTable(torrents.arguments)
                                            : tgui::parseTorrents(arr);
    std::printf("Torrents: %d\n", static_cast<int>(list.size()));
    for (const auto& t : list) {
        if (!t.name)
            continue;
        const int status = t.status.value_or(-1);
        std::string name = t.name->left(38).toStdString();
        std::string down = tgui::formatSize(t.rateDownload.value_or(0), 1);
        std::string up = tgui::formatSize(t.rateUpload.value_or(0), 1);
        std::string eta = tgui::formatEta(t.eta.value_or(-1));
        std::printf("  #%d  %-40s  status=%d  down=%s  up=%s  eta=%s\n",
                    t.id, name.data(), status, down.data(), up.data(), eta.data());
    }
    return 0;
}