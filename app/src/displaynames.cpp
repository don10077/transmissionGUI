#include "displaynames.h"

#include <QSettings>

namespace displaynames {

namespace {
QString keyForHash(const QString& hash)
{
    const auto normalized = hash.trimmed().toLower();
    return normalized.isEmpty() ? QString{}
                                : QString::fromLatin1(settingsPrefix) + normalized;
}
} // namespace

QString normalize(const QString& name)
{
    return name.trimmed().left(maxLength);
}

QString storedName(const QString& hash)
{
    const auto key = keyForHash(hash);
    return key.isEmpty() ? QString{} : QSettings().value(key).toString();
}

bool set(const QString& hash, const QString& name)
{
    const auto key = keyForHash(hash);
    const auto value = normalize(name);
    if (key.isEmpty() || value.isEmpty())
        return remove(hash);

    QSettings settings;
    if (settings.value(key).toString() == value)
        return false;
    settings.setValue(key, value);
    settings.sync();
    return true;
}

bool remove(const QString& hash)
{
    const auto key = keyForHash(hash);
    if (key.isEmpty())
        return false;
    QSettings settings;
    if (!settings.contains(key))
        return false;
    settings.remove(key);
    settings.sync();
    return true;
}

void apply(tgui::Torrent& torrent)
{
    torrent.displayName = torrent.hashString ? storedName(*torrent.hashString) : QString{};
}

QString effectiveName(const tgui::Torrent& torrent)
{
    const auto custom = torrent.displayName.trimmed();
    return custom.isEmpty() ? torrent.name.value_or(QString{}) : custom;
}

} // namespace displaynames
