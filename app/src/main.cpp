#include "mainwindow.h"
#include "externalopen.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QFont>
#include <QIcon>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <optional>
#include <utility>

namespace {
/** Буферизует системные события открытия, которые macOS может послать до создания окна. */
class TorrentApplication : public QApplication {
public:
    using QApplication::QApplication;

    void open(const QStringList& inputs)
    {
        if (m_handler)
            m_handler(inputs);
        else
            m_pending.append(inputs);
    }

    void setOpenHandler(ExternalOpen::Handler handler)
    {
        m_handler = std::move(handler);
        if (!m_pending.isEmpty())
            open(std::exchange(m_pending, {}));
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() != QEvent::FileOpen)
            return QApplication::event(event);
        const auto* openEvent = static_cast<QFileOpenEvent*>(event);
        open({openEvent->file().isEmpty() ? openEvent->url().toString() : openEvent->file()});
        return true;
    }

private:
    QStringList m_pending;
    ExternalOpen::Handler m_handler;
};
}

int main(int argc, char* argv[])
{
    TorrentApplication app(argc, argv);
    app.setOrganizationName("transmissionGUI");
    app.setApplicationName("transmissionGUI");
    app.setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"url", "Transmission server URL.", "url"});
    parser.addOption({"user", "RPC user.", "user"});
    parser.addOption({"pass", "RPC password.", "password"});
    parser.addOption({"path", "RPC path.", "path"});
    parser.addOption({"smoke-test", "Check startup with isolated settings."});
    parser.addPositionalArgument("torrent", "Local .torrent files or magnet links.", "[torrent...]");
    parser.process(app);
    const bool smokeTest = parser.isSet("smoke-test");
    std::optional<QTemporaryDir> smokeSettings;
    if (smokeTest) {
        smokeSettings.emplace();
        if (!smokeSettings->isValid())
            return 1;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, smokeSettings->path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, smokeSettings->path());
        QSettings().setValue("ui/trayIcon", false);
    }

    tgui::theme::apply(app);
    app.setWindowIcon(QIcon(tgui::theme::appIconPixmap(64)));

    // i18n (W06): русский по умолчанию, язык — QSettings("ui/language");
    // Сначала внешний .qm рядом с exe, затем встроенный перевод.
    // Переводчик живёт до выхода из main (installTranslator держит указатель на него).
    QTranslator translator;
    {
        const QString lang = QSettings().value("ui/language", QStringLiteral("ru")).toString();
        const QString qmPath = QStringLiteral("/translations/tgui_") + lang + QStringLiteral(".qm");
        if (translator.load(QCoreApplication::applicationDirPath() + qmPath) ||
            translator.load(QStringLiteral(":") + qmPath))
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
    autoOpts.url = QUrl(parser.value("url"));
    autoOpts.username = parser.value("user");
    autoOpts.password = parser.value("pass");
    autoOpts.rpcPath = parser.value("path");
    QStringList inputs;
    for (const auto& argument : parser.positionalArguments()) {
        const auto input = externalTorrentInput(argument);
        if (!input.isEmpty() && !inputs.contains(input))
            inputs.append(input);
    }

    ExternalOpen external(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    if (!smokeTest && external.listen([&app](const auto& items) { app.open(items); }) ==
            ExternalOpen::Result::AlreadyRunning && !inputs.isEmpty() && autoOpts.url.isEmpty()) {
        if (external.forward(inputs))
            return 0;
        QMessageBox::warning(nullptr, app.applicationName(),
            QCoreApplication::translate("ExternalOpen", "Could not send torrents to the running window."));
        return 1;
    }
    MainWindow window(nullptr, smokeTest ? tgui::RpcClient::Options{} : autoOpts);
    window.show();
    if (!smokeTest) {
        app.setOpenHandler([window = QPointer(&window)](const auto& items) {
            if (window)
                window->openExternalTorrents(items);
        });
        app.open(inputs);
    }
    if (smokeTest)
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    return app.exec();
}
