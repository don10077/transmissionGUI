#pragma once

#include <QDir>
#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <algorithm>

#include <optional>

namespace tgui {

/** Нормализует разделители и отбрасывает пути с переходом в родительский каталог. */
inline std::optional<QString> safeMappingPath(const QString& path)
{
    auto normalized = QString(path).replace('\\', '/');
    const auto components = normalized.split('/');
    const bool traversal = std::ranges::any_of(components, [](const QString& part) {
        return part.startsWith("..") && std::ranges::all_of(part, [](QChar ch) {
            return ch == '.' || ch == ' ';
        });
    });
    if (normalized.isEmpty() || traversal)
        return std::nullopt;
    const bool unc = normalized.startsWith("//");
    normalized = QDir::cleanPath(normalized);
    if (unc && !normalized.startsWith("//"))
        normalized.prepend('/');
    if (normalized.size() == 2 && normalized[0].isLetter() && normalized[1] == ':')
        normalized.append('/');
    return normalized;
}

/** Выбирает самое точное правило. Первый знак '=' отделяет путь демона от локального. */
inline std::optional<QString> mappedPath(const QString& path, const QStringList& mappings, bool reverse)
{
    const auto normalized = safeMappingPath(path);
    if (!normalized)
        return std::nullopt;
    qsizetype bestLength = 0;
    std::optional<QString> result;
    for (const auto& mapping : mappings) {
        const int eq = mapping.indexOf('=');
        if (eq <= 0)
            continue;
        const auto remote = safeMappingPath(mapping.left(eq).trimmed());
        const auto local = safeMappingPath(mapping.mid(eq + 1).trimmed());
        const auto& source = reverse ? local : remote;
        const auto& destination = reverse ? remote : local;
        if (!source || source->size() <= bestLength)
            continue;
        bool windowsSource = source->startsWith("//") ||
            (source->size() >= 3 && source->at(0).isLetter() && source->mid(1, 2) == ":/");
#ifdef Q_OS_WIN
        windowsSource = windowsSource || reverse;
#endif
        const auto sensitivity = windowsSource ? Qt::CaseInsensitive : Qt::CaseSensitive;
        const auto prefix = source->endsWith('/') ? *source : *source + '/';
        const bool exact = normalized->compare(*source, sensitivity) == 0;
        if (!exact && !normalized->startsWith(prefix, sensitivity))
            continue;
        bestLength = source->size();
        result.reset();
        if (!destination)
            continue;
        if (exact) {
            result = *destination;
        } else {
            const auto root = destination->endsWith('/') ? *destination : *destination + '/';
            auto candidate = safeMappingPath(root + normalized->mid(prefix.size()));
            if (candidate && candidate->startsWith(root))
                result = std::move(candidate);
        }
    }
    if (bestLength)
        return result;
    return reverse ? std::nullopt : normalized;
}

/** Возвращает локальный путь для @p path или пустой результат для небезопасного пути. */
inline std::optional<QString> mapRemoteToLocal(const QString& path, const QStringList& mappings)
{
    return mappedPath(path, mappings, false);
}

/** Возвращает путь демона для @p path по @p mappings или пустой результат. */
inline std::optional<QString> mapLocalToRemote(const QString& path, const QStringList& mappings)
{
    return mappedPath(path, mappings, true);
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
