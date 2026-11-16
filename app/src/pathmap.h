#pragma once

#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace tgui {

// Маппинг remote→local (аналог OLD MapRemoteToLocal).
// mappings — строки вида "remote=local" (напр. "/var/music=Z:/music" или "/share=\\\\nas").
// Возвращает local-путь при точном или префиксном совпадении; иначе исходный path.
// Разделители нормализуются в '/', поэтому правила работают и для Windows-путей.
inline QString mapRemoteToLocal(const QString& path, const QStringList& mappings)
{
    const QString fn = path.trimmed().replace('\\', '/');
    for (const QString& mapping : mappings) {
        const int eq = mapping.indexOf('=');
        if (eq <= 0)
            continue;
        const QString remote0 = mapping.left(eq).trimmed().replace('\\', '/');
        if (remote0.isEmpty())
            continue;
        QString remote = remote0;
        while (remote.endsWith('/'))
            remote.chop(1);
        if (remote.isEmpty())
            continue;
        QString local = mapping.mid(eq + 1).trimmed();
        while (local.endsWith('/'))
            local.chop(1);
        if (remote == fn)
            return local;
        if (fn.startsWith(remote + QLatin1Char('/')))
            return local + QLatin1Char('/') + fn.mid(remote.length() + 1);
    }
    return path;
}

/** Возвращает путь демона для @p path по @p mappings или пустой результат. */
inline std::optional<QString> mapLocalToRemote(const QString& path, const QStringList& mappings)
{
    const QString localPath = QString(path).replace('\\', '/');
    for (const auto& mapping : mappings) {
        const int eq = mapping.indexOf('=');
        if (eq <= 0) continue;
        QString local = mapping.mid(eq + 1).trimmed().replace('\\', '/');
        QString remote = mapping.left(eq).trimmed().replace('\\', '/');
        while (local.endsWith('/')) local.chop(1);
        while (remote.endsWith('/') && remote.size() > 1) remote.chop(1);
        if (local.isEmpty() || remote.isEmpty()) continue;
#ifdef Q_OS_WIN
        const auto sensitivity = Qt::CaseInsensitive;
#else
        const auto sensitivity = Qt::CaseSensitive;
#endif
        if (localPath.compare(local, sensitivity) == 0) return remote;
        if (localPath.startsWith(local + '/', sensitivity))
            return remote + (remote.endsWith('/') ? QString() : QStringLiteral("/")) + localPath.mid(local.size() + 1);
    }
    return std::nullopt;
}

/** Проверяет, указан ли в @p url адрес локального демона. */
inline bool isLocalDaemon(const QUrl& url)
{
    const auto host = url.host();
    return host.compare("localhost", Qt::CaseInsensitive) == 0 ||
        host.compare("localhost.", Qt::CaseInsensitive) == 0 || QHostAddress(host).isLoopback();
}

// Разбирает текст настроек (одна строка "remote=local" в строке) в список правил.
inline QStringList pathMappingsFromText(const QString& text)
{
    QStringList out;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

// Обратное: список правил → текст (для редактирования в диалоге).
inline QString pathMappingsToText(const QStringList& mappings)
{
    return mappings.join(QLatin1Char('\n'));
}

}  // namespace tgui
