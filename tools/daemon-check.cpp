// tgui_daemon_check — dev-инструмент: подключение к демону Transmission,
// вывод session-info + списка торрентов. (Приёмка этапа 1: «подключение к
// вашему демону и чтение списка».)
//
// Использование:
//   tgui_daemon_check <url> [--user <user>] [--pass <pass>] [--path <rpc-path>]
// Пример:
//   tgui_daemon_check http://127.0.0.1:9091 --user admin --pass secret

#include <QCoreApplication>
#include <QJsonArray>

#include <cstdio>

#include "tgui/jsonrpc.h"
#include "tgui/models.h"
#include "tgui/util.h"

namespace {

void printUsage()
{
    std::printf("Usage: tgui_daemon_check <url> [--user <user>] [--pass <pass>] "
                "[--path <rpc-path>]\n");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QString url;
    QString user;
    QString pass;
    QString rpcPath;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--user" && i + 1 < argc)
            user = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--pass" && i + 1 < argc)
            pass = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--path" && i + 1 < argc)
            rpcPath = QString::fromLocal8Bit(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg.startsWith("--")) {
            std::fprintf(stderr, "Unknown option: %ls\n", qPrintable(arg));
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

    tgui::JsonRpcClient rpc(url, rpcPath);
    if (!user.isEmpty())
        rpc.setUsernamePassword(user, pass);
    rpc.setTimeout(std::chrono::seconds(30));

    const tgui::JsonRpcResult session = rpc.callSync("session-get", QJsonObject{});
    if (!session.ok) {
        std::fprintf(stderr, "CONNECT FAILED: %ls (HTTP %d)\n",
                     qPrintable(session.error), session.httpCode);
        return 1;
    }
    tgui::SessionInfo info;
    tgui::parseSessionInfo(session.arguments, info);
    std::printf("Connected: %ls (RPC %d)\n", qPrintable(info.downloadDir),
                info.rpcVersion);
    if (info.rpcVersion >= 15) {
        const tgui::JsonRpcResult fs = rpc.callSync(
            "free-space", QJsonObject{{"path", info.downloadDir}});
        if (fs.ok)
            std::printf("Free space: %s\n",
                        tgui::formatSize(
                            fs.arguments.value("size-bytes").toVariant().toDouble())
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
        std::fprintf(stderr, "TORRENT-GET FAILED: %ls\n", qPrintable(torrents.error));
        return 1;
    }
    const QJsonArray arr = torrents.arguments.value("torrents").toArray();
    const QVector<tgui::Torrent> list = (!arr.isEmpty() && arr.first().isArray())
                                            ? tgui::parseTorrentsTable(torrents.arguments)
                                            : tgui::parseTorrents(arr);
    std::printf("Torrents: %d\n", list.size());
    for (const auto& t : list) {
        if (!t.name)
            continue;
        const int status = t.status.value_or(-1);
        double size = t.sizeWhenDone.value_or(0);
        double left = t.leftUntilDone.value_or(0);
        std::printf(
            "  #%d  %-40s  status=%d  %s  down=%s  up=%s  eta=%s\n", t.id,
            tgui::formatSize(size).c_str(), status,
            tgui::formatSize(t.rateDownload.value_or(0), 1).c_str(),
            tgui::formatSize(t.rateUpload.value_or(0), 1).c_str(),
            tgui::formatEta(t.eta.value_or(-1)).c_str());
        (void)left;
    }
    return 0;
}