#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QIcon>

// Запуск:
//   transmissiongui.exe [--url http://хост:порт] [--user имя] [--pass пароль]
//                        [--path /transmission/rpc]
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("transmissionGUI");
    app.setApplicationName("transmissionGUI");
    app.setApplicationVersion("0.1.0");

    tgui::theme::apply(app);
    app.setWindowIcon(QIcon(tgui::theme::appIconPixmap(64)));

    tgui::RpcClient::Options autoOpts;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        const auto next = [&i, &argc, argv]() -> QString {
            return (i + 1 < argc) ? QString::fromLocal8Bit(argv[i + 1]) : QString();
        };
        if (arg == "--url" && i + 1 < argc) {
            autoOpts.url = QUrl(next());
            ++i;
        } else if (arg == "--user" && i + 1 < argc) {
            autoOpts.username = next();
            ++i;
        } else if (arg == "--pass" && i + 1 < argc) {
            autoOpts.password = next();
            ++i;
        } else if (arg == "--path" && i + 1 < argc) {
            autoOpts.rpcPath = next();
            ++i;
        }
    }

    MainWindow window;
    window.setAutoConnect(autoOpts);
    window.show();
    return app.exec();
}
