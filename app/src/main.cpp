#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QSettings>
#include <QTranslator>
#include <cmath>

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

    // i18n (W06): русский по умолчанию, язык — QSettings("ui/language");
    // .qm ищется рядом с exe (translations/tgui_<lang>.qm); если нет — fallback на английский.
    // Переводчик живёт до выхода из main (installTranslator держит указатель на него).
    QTranslator translator;
    {
        const QString lang = QSettings().value("ui/language", QStringLiteral("ru")).toString();
        const QString qmPath = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/translations/tgui_") + lang + QStringLiteral(".qm");
        if (translator.load(qmPath))
            app.installTranslator(&translator);
    }

    // Масштаб интерфейса (W07): ui/scale в %; применяется к базовому шрифту до создания окна.
    // Требует перезапуска (выбирается в «Параметрах»); 100% — без изменений.
    {
        const int scale = QSettings().value("ui/scale", 100).toInt();
        if (scale > 0 && scale != 100) {
            QFont f = app.font();
            if (f.pointSizeF() > 0.0)
                f.setPointSizeF(f.pointSizeF() * scale / 100.0);
            else if (f.pixelSize() > 0)
                f.setPixelSize(static_cast<int>(std::lround(f.pixelSize() * scale / 100.0)));
            app.setFont(f);
        }
    }

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
