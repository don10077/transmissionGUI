#pragma once

#include <QNetworkProxy>
#include <QStringList>

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