#pragma once

#include <QString>
#include <QStringList>

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