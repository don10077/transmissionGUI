#include "daemonprofile.h"

#include <QSettings>

namespace {
QString sec(const QString& name) { return QStringLiteral("daemons/") + name; }
}  // namespace

QUrl DaemonProfile::url() const
{
    QUrl u;
    u.setScheme(useSsl ? QStringLiteral("https") : QStringLiteral("http"));
    u.setHost(host);
    if (port > 0)
        u.setPort(port);
    return u;
}

tgui::RpcClient::Options DaemonProfile::toOptions() const
{
    tgui::RpcClient::Options o;
    o.url = url();
    o.rpcPath = rpcPath;
    o.username = username;
    o.password = password;
    o.ignoreSslErrors = ignoreSslErrors;
    if (useSsl) {
        o.clientCertificatePath = clientCertificatePath;
        o.clientKeyPath = clientKeyPath;
    }
    if (useProxy && !proxyHost.isEmpty()) {
        o.proxy = useSocks
                      ? QNetworkProxy(QNetworkProxy::Socks5Proxy, proxyHost, proxyPort, proxyUser, proxyPass)
                      : QNetworkProxy(QNetworkProxy::HttpProxy, proxyHost, proxyPort, proxyUser, proxyPass);
    } else {
        o.proxy = QNetworkProxy::NoProxy;
    }
    return o;
}

// --- ProfileStore ---

QStringList ProfileStore::names() const
{
    QSettings s;
    auto list = s.value(QStringLiteral("daemons/names")).toStringList();
    list.removeAll(QString());
    return list;
}

QString ProfileStore::currentName() const
{
    return QSettings().value(QStringLiteral("daemons/current")).toString();
}

bool ProfileStore::hasCurrent() const
{
    const QString cur = currentName();
    return !cur.isEmpty() && exists(cur);
}

void ProfileStore::setCurrent(const QString& name)
{
    QSettings().setValue(QStringLiteral("daemons/current"), name);
}

bool ProfileStore::exists(const QString& name) const
{
    return names().contains(name);
}

DaemonProfile ProfileStore::profile(const QString& name) const
{
    QSettings s;
    DaemonProfile p;
    p.name = name;
    const QString base = sec(name);
    p.host = s.value(base + "/host").toString();
    p.port = s.value(base + "/port", 9091).toInt();
    p.useSsl = s.value(base + "/useSsl", false).toBool();
    p.rpcPath = s.value(base + "/rpcPath").toString();
    p.username = s.value(base + "/username").toString();
    p.password = s.value(base + "/password").toString();
    p.ignoreSslErrors = s.value(base + "/ignoreSsl", false).toBool();
    p.clientCertificatePath = s.value(base + "/clientCertificatePath").toString();
    p.clientKeyPath = s.value(base + "/clientKeyPath").toString();
    p.useProxy = s.value(base + "/useProxy", false).toBool();
    p.useSocks = s.value(base + "/useSocks", false).toBool();
    p.proxyHost = s.value(base + "/proxyHost").toString();
    p.proxyPort = s.value(base + "/proxyPort", 8080).toInt();
    p.proxyUser = s.value(base + "/proxyUser").toString();
    p.proxyPass = s.value(base + "/proxyPass").toString();
    return p;
}

void ProfileStore::save(const DaemonProfile& p)
{
    QSettings s;
    const QString base = sec(p.name);
    s.setValue(base + "/host", p.host);
    s.setValue(base + "/port", p.port);
    s.setValue(base + "/useSsl", p.useSsl);
    s.setValue(base + "/rpcPath", p.rpcPath);
    s.setValue(base + "/username", p.username);
    s.setValue(base + "/password", p.password);
    s.setValue(base + "/ignoreSsl", p.ignoreSslErrors);
    s.setValue(base + "/clientCertificatePath", p.clientCertificatePath);
    s.setValue(base + "/clientKeyPath", p.clientKeyPath);
    s.setValue(base + "/useProxy", p.useProxy);
    s.setValue(base + "/useSocks", p.useSocks);
    s.setValue(base + "/proxyHost", p.proxyHost);
    s.setValue(base + "/proxyPort", p.proxyPort);
    s.setValue(base + "/proxyUser", p.proxyUser);
    s.setValue(base + "/proxyPass", p.proxyPass);
    s.sync();
}

void ProfileStore::add(const QString& name)
{
    if (name.isEmpty())
        return;
    auto list = names();
    if (!list.contains(name)) {
        list << name;
        QSettings().setValue(QStringLiteral("daemons/names"), list);
        DaemonProfile p;
        p.name = name;
        save(p);  // записать дефолты
    }
    setCurrent(name);
}

void ProfileStore::remove(const QString& name)
{
    auto list = names();
    if (!list.removeAll(name))
        return;
    QSettings s;
    s.setValue(QStringLiteral("daemons/names"), list);
    s.remove(sec(name));
    if (currentName() == name) {
        s.setValue(QStringLiteral("daemons/current"),
                   list.isEmpty() ? QString() : list.first());
    }
    s.sync();
}

void ProfileStore::rename(const QString& oldName, const QString& newName)
{
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return;
    auto list = names();
    if (!list.contains(oldName) || list.contains(newName))
        return;
    DaemonProfile p = profile(oldName);
    p.name = newName;
    save(p);
    list.replaceInStrings(oldName, newName);
    QSettings s;
    s.setValue(QStringLiteral("daemons/names"), list);
    s.remove(sec(oldName));
    if (currentName() == oldName)
        s.setValue(QStringLiteral("daemons/current"), newName);
    s.sync();
}

DaemonProfile ProfileStore::currentProfile() const
{
    const QString cur = currentName();
    return cur.isEmpty() ? DaemonProfile{} : profile(cur);
}
