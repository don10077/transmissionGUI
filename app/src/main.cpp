#include "mainwindow.h"
#include "connectdialog.h"
#include "daemonprofile.h"
#include "externalopen.h"
#include "theme.h"
#include "settingsbackup.h"
#include "tgui/rpcpassword.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QFont>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QRegularExpression>
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
        else if (inputs.isEmpty())
            m_activatePending = true;
        else
            m_pending.append(inputs);
    }

    void setOpenHandler(ExternalOpen::Handler handler)
    {
        m_handler = std::move(handler);
        if (!m_pending.isEmpty())
            open(std::exchange(m_pending, {}));
        else if (m_activatePending)
            open({});
        m_activatePending = false;
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
    bool m_activatePending = false;
    ExternalOpen::Handler m_handler;
};
}

int main(int argc, char* argv[])
{
    TorrentApplication app(argc, argv);
    app.setOrganizationName("transmissionGUI");
    app.setApplicationName("transmissionGUI");
    app.setApplicationVersion(QStringLiteral(TGUI_VERSION));
    // Изолированные настройки для --smoke-test нужны раньше, чем читаются QSettings
    // (язык интерфейса), поэтому аргументы проверяем до разбора командной строки.
    const bool smokeTest = app.arguments().contains(QStringLiteral("--smoke-test"));
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

    // i18n (W06, I18N-02): язык — QSettings("ui/language"). Значение из ini не доверяем:
    // допускаем "system" или код вида ru / pt_BR, иначе откатываемся на русский.
    // Переводы Qt (qtbase_*.qm) грузим отдельным переводчиком: без них остаются
    // английскими кнопки диалогов и контекстные меню стандартных виджетов.
    // Переводчик живёт до выхода из main (installTranslator держит указатель на него).
    QTranslator translator;
    QTranslator qtTranslator;
    QString lang = QSettings().value("ui/language", QStringLiteral("ru")).toString();
    if (lang == QLatin1String("system"))
        lang = QLocale::system().name();
    static const QRegularExpression languagePattern(QStringLiteral("^[a-z]{2}(_[A-Z]{2})?$"));
    if (!languagePattern.match(lang).hasMatch())
        lang = QStringLiteral("ru");
    {
        const QString qmPath = QStringLiteral("/translations/tgui_") + lang + QStringLiteral(".qm");
        if (translator.load(QCoreApplication::applicationDirPath() + qmPath) ||
            translator.load(QStringLiteral(":") + qmPath))
            app.installTranslator(&translator);
    }
    {
        const QLocale locale(lang);
        if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                             QLibraryInfo::path(QLibraryInfo::TranslationsPath)) ||
            qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                              QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
            app.installTranslator(&qtTranslator);
    }

    // Описания опций переводим после установки переводчика, чтобы справка была на выбранном языке.
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const auto optionText = [](const char* text) {
        return QCoreApplication::translate("main", text);
    };
    parser.addOption({"url", optionText("Transmission server URL."), optionText("url"), "url"});
    parser.addOption({"user", optionText("RPC user."), optionText("user"), "user"});
    parser.addOption({"pass", optionText("RPC password (unsafe: visible in the command line)."),
                      optionText("password"), "password"});
    parser.addOption({"pass-stdin", optionText("Read the RPC password from standard input.")});
    parser.addOption({"path", optionText("RPC path."), optionText("path"), "path"});
    parser.addOption({"smoke-test", optionText("Check startup with isolated settings.")});
    parser.addPositionalArgument("torrent", optionText("Local .torrent files or magnet links."),
                                 optionText("[torrent...]"));
    parser.process(app);

    QStringList inputs;
    for (const auto& argument : parser.positionalArguments()) {
        const auto input = externalTorrentInput(argument);
        if (!input.isEmpty() && !inputs.contains(input))
            inputs.append(input);
    }

    ExternalOpen external(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    if (!smokeTest) {
        const auto state = external.listen([&app](const auto& items) { app.open(items); });
        if (state == ExternalOpen::Result::AlreadyRunning) {
            if (parser.isSet("url"))
                qWarning("An instance is already running. The requested server was not changed.");
            if (external.forward(parser.isSet("url") ? QStringList{} : inputs))
                return 0;
            qWarning("Could not reach the running window.");
            return 1;
        }
        if (state == ExternalOpen::Result::Unavailable) {
            qWarning("Could not obtain the application instance lock.");
            return 1;
        }
    }

    bool recoveredSettings = false;
    const auto recovery = smokeTest ? QString{} : settingsbackup::recoveryPath();
    if (!recovery.isEmpty() && QFileInfo::exists(recovery)) {
        QSettings settings;
        settings.setFallbacksEnabled(false);
        if (!settingsbackup::restoreInterrupted(settings, recovery)) {
            QMessageBox::warning(nullptr, app.applicationName(),
                QCoreApplication::translate("SettingsBackup", "Could not recover settings after an interrupted import."));
            return 1;
        }
        recoveredSettings = true;
    }

    tgui::theme::apply(app);
    app.setWindowIcon(tgui::theme::icon(tgui::theme::Icon::App));

    // Масштаб интерфейса (W07): ui/scale в %; применяется к базовому шрифту до создания окна.
    // Требует перезапуска (выбирается в «Параметрах»); 100% — без изменений.
    // Значение из ini ограничиваем: слишком большой или отрицательный масштаб
    // сделал бы интерфейс нечитаемым/невидимым без возможности это исправить из UI.
    {
        const int scale = qBound(50, QSettings().value("ui/scale", 100).toInt(), 300);
        if (scale != 100) {
            QFont f = app.font();
            if (f.pointSizeF() > 0.0)
                f.setPointSizeF(f.pointSizeF() * scale / 100.0);
            else if (f.pixelSize() > 0)
                f.setPixelSize(static_cast<int>(std::lround(f.pixelSize() * scale / 100.0)));
            app.setFont(f);
        }
    }

    tgui::RpcClient::Options autoOpts;
    if (!smokeTest && (parser.isSet("url") || parser.isSet("user"))) {
        if (parser.isSet("url"))
            autoOpts.url = QUrl(parser.value("url"));
        else
            autoOpts = ProfileStore().currentProfile().toOptions();
        if (autoOpts.url.host().isEmpty()) {
            qWarning("Specify --url or configure a daemon profile before using --user.");
            return 1;
        }
        if (parser.isSet("path"))
            autoOpts.rpcPath = parser.value("path");
        if (parser.isSet("user"))
            autoOpts.username = parser.value("user");
        const auto password = tgui::startupRpcPassword(
            parser.isSet("pass") ? parser.value("pass") : QString{}, parser.isSet("pass-stdin"));
        if (!password && parser.isSet("pass-stdin")) {
            qWarning("Could not read the RPC password from standard input.");
            return 1;
        }
        if (password) {
            autoOpts.password = *password;
        } else if (parser.isSet("user")) {
            ConnectDialog credentials(autoOpts);
            if (credentials.exec() != QDialog::Accepted)
                return 0;
            autoOpts = credentials.options();
        }
    }
    MainWindow window(nullptr, smokeTest ? tgui::RpcClient::Options{} : autoOpts);
    window.show();
    if (recoveredSettings) {
        QMessageBox::information(&window, app.applicationName(),
            QCoreApplication::translate("SettingsBackup", "Settings restored after an interrupted import."));
    }
    if (!smokeTest) {
        app.setOpenHandler([window = QPointer(&window)](const auto& items) {
            if (!window)
                return;
            if (items.isEmpty()) {
                if (window->isMinimized())
                    window->showNormal();
                else
                    window->show();
                window->raise();
                window->activateWindow();
            } else {
                window->openExternalTorrents(items);
            }
        });
        if (!inputs.isEmpty())
            app.open(inputs);
    }
    if (smokeTest)
        QTimer::singleShot(250, &app, &QCoreApplication::quit);
    return app.exec();
}
