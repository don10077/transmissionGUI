#pragma once

#include <QNetworkProxy>
#include <QStringList>
#include <QVariantMap>

#include "tgui/rpcclient.h"

// W08: профиль демона (одна запись ConnOptions) + хранилище имённых профилей.
// Образец: transgui/connoptions.pas (секция Connection.<name> в INI). Здесь — QSettings.

// Параметры соединения с одним демоном Transmission (+ прокси).
struct DaemonProfile {
    QString name;
    QString host;          // имя/адрес (без схемы)
    int port = 9091;
    bool useSsl = false;   // https вместо http
    QString rpcPath;       // пусто → "/transmission/rpc"
    QString username;
    QString password;
    bool ignoreSslErrors = false;
    QString clientCertificatePath;
    QString clientKeyPath;
    // Прокси (HTTP или SOCKS5).
    bool useProxy = false;
    bool useSocks = false;
    QString proxyHost;
    int proxyPort = 8080;
    QString proxyUser;
    QString proxyPass;

    QUrl url() const;
    tgui::RpcClient::Options toOptions() const;
};

// CRUD имённых профилей в QSettings (раздел daemons/*, daemons/current).
class ProfileStore {
public:
    QStringList names() const;
    QString currentName() const;
    bool hasCurrent() const;
    void setCurrent(const QString& name);
    bool exists(const QString& name) const;
    DaemonProfile profile(const QString& name) const;
    void save(const DaemonProfile& p);
    void add(const QString& name);  // создать (если нет) и сделать текущим
    void remove(const QString& name);
    void rename(const QString& oldName, const QString& newName);
    DaemonProfile currentProfile() const;
};

/** Настройки путей и меню скоростей одного профиля. Пустое имя — подключение без профиля. */
class ProfilePreferences {
public:
    explicit ProfilePreferences(const QString& name = {});
    QStringList mappings() const;
    void setMappings(const QStringList& mappings) const;
    QStringList moveHistory() const;
    void rememberMove(const QString& directory) const;
    QVariantMap destinations(const tgui::RpcClient::Options* options) const;
    void setDestinations(const tgui::RpcClient::Options* options, const QVariantMap& destinations) const;
    QList<int> speedPresets(bool download) const;
    void setSpeedPresets(bool download, const QList<int>& speeds) const;
    static std::optional<QList<int>> parseSpeeds(const QString& text);

private:
    QVariant value(const QString& key, const QString& legacy, const QVariant& fallback = {}) const;
    void setValue(const QString& key, const QString& legacy, const QVariant& value) const;
    QString m_name;
};
