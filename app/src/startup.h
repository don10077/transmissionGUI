#pragma once

#include <QString>
#include <QStringList>

/** Результат разбора командной строки приложения.
 * TST-03: логика разбора вынесена из main() в тестируемую функцию. */
struct StartupOptions {
    bool ok = false;                // false — некорректные аргументы; @p error — причина
    bool showHelp = false;         // --help
    bool showVersion = false;      // --version
    bool smokeTest = false;        // --smoke-test
    bool hasUrl = false;           // указан --url
    bool hasUser = false;          // указан --user
    bool hasPassword = false;      // указан --pass
    bool passwordFromStdin = false; // указан --pass-stdin
    QString url;                   // --url (http(s), с хостом)
    QString rpcPath;               // --path
    QString username;              // --user
    QString password;              // --pass
    QStringList inputs;            // пути к .torrent и magnet-ссылки, без дублей
    QString error;
};

/** Текст справки; переводится после установки переводчика. */
QString startupHelpText();

/** Разбирает аргументы командной строки.
 * В отличие от QCommandLineParser::process не вызывает exit() и не печатает:
 * --help/--version/ошибки возвращаются полями структуры. */
StartupOptions parseStartup(const QStringList& arguments);