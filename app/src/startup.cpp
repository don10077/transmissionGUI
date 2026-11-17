#include "startup.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QUrl>

#include "externalopen.h"

namespace {

void configureParser(QCommandLineParser& parser)
{
    // Литералы даём lupdate напрямую в QCoreApplication::translate (I18N-03):
    // через лямбду-обёртку он их не видел, и описания оставались без перевода.
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"url",
                      QCoreApplication::translate("main", "Transmission server URL."),
                      QCoreApplication::translate("main", "url"), "url"});
    parser.addOption({"user",
                      QCoreApplication::translate("main", "RPC user."),
                      QCoreApplication::translate("main", "user"), "user"});
    parser.addOption({"pass",
                      QCoreApplication::translate(
                          "main", "RPC password (unsafe: visible in the command line)."),
                      QCoreApplication::translate("main", "password"), "password"});
    parser.addOption(
        {"pass-stdin", QCoreApplication::translate("main", "Read the RPC password from standard input.")});
    parser.addOption({"path",
                      QCoreApplication::translate("main", "RPC path."),
                      QCoreApplication::translate("main", "path"), "path"});
    parser.addOption({"smoke-test", QCoreApplication::translate("main", "Check startup with isolated settings.")});
    parser.addPositionalArgument("torrent",
                                 QCoreApplication::translate("main", "Local .torrent files or magnet links."),
                                 QCoreApplication::translate("main", "[torrent...]"));
}

} // namespace

QString startupHelpText()
{
    QCommandLineParser parser;
    configureParser(parser);
    return parser.helpText();
}

StartupOptions parseStartup(const QStringList& arguments)
{
    StartupOptions result;
    QCommandLineParser parser;
    configureParser(parser);
    parser.parse(arguments);
    if (parser.isSet("help")) {
        result.showHelp = true;
        return result;
    }
    if (parser.isSet("version")) {
        result.showVersion = true;
        return result;
    }
    result.smokeTest = parser.isSet("smoke-test");
    if (parser.isSet("url")) {
        const QUrl url(parser.value("url"), QUrl::StrictMode);
        const bool valid = (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) &&
                           !url.host().isEmpty();
        if (!valid) {
            result.error = "Specify a valid --url (http://host:port).";
            return result;
        }
        result.hasUrl = true;
        result.url = parser.value("url");
    }
    if (parser.isSet("path"))
        result.rpcPath = parser.value("path");
    if (parser.isSet("user")) {
        result.hasUser = true;
        result.username = parser.value("user");
    }
    if (parser.isSet("pass")) {
        result.hasPassword = true;
        result.password = parser.value("pass");
    }
    result.passwordFromStdin = parser.isSet("pass-stdin");
    for (const auto& argument : parser.positionalArguments()) {
        const auto input = externalTorrentInput(argument);
        if (!input.isEmpty() && !result.inputs.contains(input))
            result.inputs.append(input);
    }
    result.ok = true;
    return result;
}