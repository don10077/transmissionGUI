#pragma once

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>
#include <algorithm>
#include <optional>

namespace settingsbackup {
inline constexpr qint64 maxBytes = 8 * 1024 * 1024;

/** Записывает файл целиком с атомарной заменой. */
inline bool writeFile(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

/** Снимает копию всех ключей текущего хранилища. */
inline QVariantMap snapshot(QSettings& settings)
{
    QVariantMap result;
    for (const auto& key : settings.allKeys())
        result.insert(key, settings.value(key));
    return result;
}

/** Формирует версионированную копию настроек. */
inline bool write(const QString& path, const QVariantMap& values)
{
    if (values.isEmpty())
        return false;
    QTemporaryDir stage;
    if (!stage.isValid())
        return false;
    QSettings out(stage.filePath("settings.ini"), QSettings::IniFormat);
    out.setValue("backup/application", "transmissionGUI");
    out.setValue("backup/version", 1);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        out.setValue(it.key(), it.value());
    out.sync();
    QFile generated(out.fileName());
    return out.status() == QSettings::NoError && generated.open(QIODevice::ReadOnly) &&
           writeFile(path, generated.readAll());
}

/** Читает новый или прежний экспорт клиента и проверяет его до записи настроек. */
inline std::optional<QVariantMap> read(const QString& path)
{
    const QFileInfo file(path);
    if (!file.isFile() || file.size() <= 0 || file.size() > maxBytes)
        return std::nullopt;
    QSettings in(path, QSettings::IniFormat);
    auto values = snapshot(in);
    if (in.status() != QSettings::NoError)
        return std::nullopt;
    const bool marked = values.contains("backup/application") || values.contains("backup/version");
    if (marked && (values.take("backup/application").toString() != "transmissionGUI" ||
                   values.take("backup/version").toString() != "1"))
        return std::nullopt;
    if (values.isEmpty())
        return std::nullopt;
    const QStringList groups{"ui/", "connection/", "daemons/", "paths/", "downloads/",
                             "columns/", "files/", "trackers/", "peers/", "labels/"};
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!it.value().isValid() || (it.key() != "lastAddLink" &&
            std::none_of(groups.cbegin(), groups.cend(), [&](const auto& group) { return it.key().startsWith(group); })))
            return std::nullopt;
    }
    const QStringList legacySignatures{"ui/refreshInterval", "ui/language", "ui/trayIcon", "ui/autoRefresh",
                                       "connection/url", "daemons/names", "paths/mappings", "columns/order"};
    if (!marked && std::none_of(legacySignatures.cbegin(), legacySignatures.cend(),
                               [&](const auto& key) { return values.contains(key); }))
        return std::nullopt;
    const auto profiles = values.value("daemons/names").toStringList();
    const auto selected = values.value("daemons/current").toString();
    if (!selected.isEmpty() && !profiles.contains(selected))
        return std::nullopt;
    for (const auto& name : profiles)
        if (name.isEmpty() || !values.contains("daemons/" + name + "/host"))
            return std::nullopt;
    return values;
}

/** Заменяет настройки после проверки и восстанавливает снимок при ошибке записи. */
inline bool replace(QSettings& settings, const QVariantMap& values)
{
    const auto before = snapshot(settings);
    const auto store = [&settings](const QVariantMap& data) {
        settings.clear();
        for (auto it = data.cbegin(); it != data.cend(); ++it)
            settings.setValue(it.key(), it.value());
        settings.sync();
    };
    store(values);
    if (settings.status() == QSettings::NoError)
        return true;
    store(before);
    return false;
}
}
