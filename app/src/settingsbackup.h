#pragma once

#include "daemonprofile.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>
#include <algorithm>
#include <optional>

namespace settingsbackup {
inline constexpr qint64 maxBytes = 8 * 1024 * 1024;
inline constexpr quint32 recoveryMagic = 0x54475549;
inline constexpr qint64 maxRecoveryBytes = 64LL * 1024 * 1024;

/** Возвращает файл восстановления в каталоге настроек текущего пользователя. */
inline QString recoveryPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath("settings-recovery.dat");
}

/** Определяет устаревшие параметры соединения и непереносимые секреты профиля. */
inline bool isLocalCredential(const QString& key)
{
    const auto normalized = key.toLower();
    return normalized.startsWith("connection/") ||
        (normalized.startsWith("daemons/") &&
         (normalized.endsWith("/password") || normalized.endsWith("/proxypass") ||
          normalized.endsWith("/protectedpassword") || normalized.endsWith("/protectedproxypass")));
}

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
        if (!isLocalCredential(key))
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
    for (auto it = values.begin(); it != values.end();) {
        switch (it.value().typeId()) {
            case QMetaType::QString:
            case QMetaType::QStringList:
            case QMetaType::Bool:
            case QMetaType::Int:
            case QMetaType::LongLong:
            case QMetaType::Double:
            case QMetaType::QByteArray:
                ++it;
                break;
            default:
                it = values.erase(it);
        }
    }
    if (values.isEmpty())
        return std::nullopt;
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

/** Записывает все ключи и дожидается сохранения настроек. */
inline bool storeSnapshot(QSettings& settings, const QVariantMap& data)
{
    settings.clear();
    for (auto it = data.cbegin(); it != data.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.sync();
    return settings.status() == QSettings::NoError;
}

/** Атомарно записывает локальный снимок перед заменой настроек. */
inline bool writeRecovery(const QString& path, const QVariantMap& data)
{
    if (QFile::exists(path) || !QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << recoveryMagic << data;
    return stream.status() == QDataStream::Ok && file.size() <= maxRecoveryBytes && file.commit();
}

/** Восстанавливает настройки после прерванного импорта. */
inline bool restoreInterrupted(QSettings& settings, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxRecoveryBytes)
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    stream >> magic;
    if (magic != recoveryMagic)
        return false;
    QVariantMap before;
    stream >> before;
    if (stream.status() != QDataStream::Ok || !stream.atEnd())
        return false;
    file.close();
    return storeSnapshot(settings, before) && QFile::remove(path);
}

/** Заменяет настройки и сохраняет возможность восстановления после сбоя. */
inline bool replace(QSettings& settings, const QVariantMap& values, const QString& journal = {})
{
    const auto before = snapshot(settings);
    auto restored = values;
    for (auto it = restored.begin(); it != restored.end();) {
        if (isLocalCredential(it.key()))
            it = restored.erase(it);
        else
            ++it;
    }
    if (restored.isEmpty())
        return false;
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
    for (const auto& name : restored.value("daemons/names").toStringList()) {
        const auto base = QStringLiteral("daemons/") + name + '/';
        const QStringList connection{"host", "port", "useSsl", "rpcPath", "username", "ignoreSsl",
                                     "useProxy", "useSocks", "proxyHost", "proxyPort", "proxyUser"};
        const QStringList proxy{"useProxy", "useSocks", "proxyHost", "proxyPort", "proxyUser"};
        preserve(base, "password", connection);
        preserve(base, "protectedPassword", connection);
        preserve(base, "proxyPass", proxy);
        preserve(base, "protectedProxyPass", proxy);
    }
    if (!journal.isEmpty() && !writeRecovery(journal, before))
        return false;
    if (storeSnapshot(settings, restored) && (journal.isEmpty() || QFile::remove(journal)))
        return true;
    if (storeSnapshot(settings, before) && !journal.isEmpty())
        QFile::remove(journal);
    return false;
}
}
