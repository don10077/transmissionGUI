#pragma once

#include "daemonprofile.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
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
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto key = it.key();
        const auto normalized = key.toLower();
        if (normalized == "connection/password" ||
            (normalized.startsWith("daemons/") && (normalized.endsWith("/password") || normalized.endsWith("/proxypass"))))
            continue;
        if (normalized == "connection/url") {
            out.setValue(key, QUrl(it.value().toString()).toString(QUrl::RemovePassword));
            continue;
        }
        out.setValue(key, it.value());
    }
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
    QStringList uniqueNames;
    for (const auto& name : profiles) {
        if (!ProfileStore::isValidName(name) || uniqueNames.contains(name, Qt::CaseInsensitive) ||
            !values.contains("daemons/" + name + "/host"))
            return std::nullopt;
        uniqueNames.append(name);
    }
    return values;
}

/** Заменяет настройки после проверки и восстанавливает снимок при ошибке записи. */
inline bool replace(QSettings& settings, const QVariantMap& values)
{
    const auto before = snapshot(settings);
    auto restored = values;
    const auto matches = [&](const QString& base, const QStringList& identity) {
        return std::all_of(identity.cbegin(), identity.cend(), [&](const auto& field) {
            return before.value(base + field).toString() == restored.value(base + field).toString();
        });
    };
    const auto preserve = [&](const QString& base, const QString& secret, const QStringList& identity) {
        const auto key = base + secret;
        if (!restored.contains(key) && before.contains(key) && matches(base, identity))
            restored.insert(key, before.value(key));
    };
    const QUrl oldUrl(before.value("connection/url").toString());
    const QUrl newUrl(restored.value("connection/url").toString());
    if (!newUrl.isEmpty() && oldUrl.matches(newUrl, QUrl::RemovePassword) &&
        matches("connection/", {"rpcPath", "ignoreSsl"})) {
        preserve("connection/", "password", {"user"});
        if (newUrl.password().isEmpty() && !oldUrl.password().isEmpty())
            restored.insert("connection/url", oldUrl.toString());
    }
    for (const auto& name : restored.value("daemons/names").toStringList()) {
        const auto base = QStringLiteral("daemons/") + name + '/';
        preserve(base, "password", {"host", "port", "useSsl", "rpcPath", "username", "ignoreSsl",
                                    "useProxy", "useSocks", "proxyHost", "proxyPort", "proxyUser"});
        preserve(base, "proxyPass", {"useProxy", "useSocks", "proxyHost", "proxyPort", "proxyUser"});
    }
    const auto store = [&settings](const QVariantMap& data) {
        settings.clear();
        for (auto it = data.cbegin(); it != data.cend(); ++it)
            settings.setValue(it.key(), it.value());
        settings.sync();
    };
    store(restored);
    if (settings.status() == QSettings::NoError)
        return true;
    store(before);
    return false;
}
}
