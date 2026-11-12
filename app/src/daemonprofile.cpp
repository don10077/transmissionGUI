#include "daemonprofile.h"

#include <QSettings>
#include <QRegularExpression>
#include <algorithm>

namespace {
QString sec(const QString& name) { return QStringLiteral("daemons/") + name; }
QString pref(const QString& name, const QString& key) { return sec(name) + "/preferences/" + key; }

QString legacyDestinations(const tgui::RpcClient::Options* options)
{
    QString key = QStringLiteral("downloads/destinations/");
    if (options)
        key += QString::fromLatin1(QUrl::toPercentEncoding(
            options->url.toString(QUrl::RemoveUserInfo) + options->rpcPath));
    return key;
}

/** Снимок прежних общих настроек для профилей, существовавших к моменту обновления. */
void migratePreferences()
{
    QSettings settings;
    const auto marker = QStringLiteral("daemons/preferencesMigrated");
    if (settings.value(marker).toBool())
        return;
    ProfileStore store;
    for (const auto& name : store.names()) {
        const auto options = store.profile(name).toOptions();
        for (const auto& [key, legacy] : {
                std::pair{QStringLiteral("mappings"), QStringLiteral("paths/mappings")},
                std::pair{QStringLiteral("moveDirs"), QStringLiteral("paths/moveDirs")},
                std::pair{QStringLiteral("downloadSpeeds"), QStringLiteral("downloads/downloadSpeeds")},
                std::pair{QStringLiteral("uploadSpeeds"), QStringLiteral("downloads/uploadSpeeds")},
                std::pair{QStringLiteral("destinations"), legacyDestinations(&options)}}) {
            const auto target = pref(name, key);
            if (!settings.contains(target) && settings.contains(legacy))
                settings.setValue(target, settings.value(legacy));
        }
    }
    settings.setValue(marker, true);
    settings.sync();
}
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
    migratePreferences();
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
    QSettings s;
    s.beginGroup(sec(oldName));
    QVariantMap values;
    for (const auto& key : s.allKeys())
        values.insert(key, s.value(key));
    s.endGroup();
    s.beginGroup(sec(newName));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        s.setValue(it.key(), it.value());
    s.endGroup();
    list[list.indexOf(oldName)] = newName;
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

ProfilePreferences::ProfilePreferences(const QString& name) : m_name(name)
{
    migratePreferences();
}

QVariant ProfilePreferences::value(const QString& key, const QString& legacy, const QVariant& fallback) const
{
    return QSettings().value(m_name.isEmpty() ? legacy : pref(m_name, key), fallback);
}

void ProfilePreferences::setValue(const QString& key, const QString& legacy, const QVariant& value) const
{
    if (m_name.isEmpty() || ProfileStore().exists(m_name))
        QSettings().setValue(m_name.isEmpty() ? legacy : pref(m_name, key), value);
}

QStringList ProfilePreferences::mappings() const
{
    return value("mappings", "paths/mappings").toStringList();
}

void ProfilePreferences::setMappings(const QStringList& mappings) const
{
    setValue("mappings", "paths/mappings", mappings);
}

QStringList ProfilePreferences::moveHistory() const
{
    return value("moveDirs", "paths/moveDirs").toStringList();
}

void ProfilePreferences::rememberMove(const QString& directory) const
{
    auto history = moveHistory();
    history.removeAll(directory);
    history.prepend(directory);
    while (history.size() > 10)
        history.removeLast();
    setValue("moveDirs", "paths/moveDirs", history);
}

QVariantMap ProfilePreferences::destinations(const tgui::RpcClient::Options* options) const
{
    return value("destinations", legacyDestinations(options)).toMap();
}

void ProfilePreferences::setDestinations(const tgui::RpcClient::Options* options, const QVariantMap& destinations) const
{
    setValue("destinations", legacyDestinations(options), destinations);
}

std::optional<QList<int>> ProfilePreferences::parseSpeeds(const QString& text)
{
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    QList<int> speeds;
    for (const auto& token : text.split(separators, Qt::SkipEmptyParts)) {
        bool ok = false;
        const int speed = token.toInt(&ok);
        if (!ok || speed < 0)
            return std::nullopt;
        speeds.append(speed);
    }
    std::sort(speeds.begin(), speeds.end());
    speeds.erase(std::unique(speeds.begin(), speeds.end()), speeds.end());
    return speeds;
}

QList<int> ProfilePreferences::speedPresets(bool download) const
{
    const auto key = download ? QStringLiteral("downloadSpeeds") : QStringLiteral("uploadSpeeds");
    const QString defaults = QStringLiteral("0,10,25,50,100,250,500,750,1000,2500,5000,7000");
    return parseSpeeds(value(key, "downloads/" + key, defaults).toString()).value_or(*parseSpeeds(defaults));
}

void ProfilePreferences::setSpeedPresets(bool download, const QList<int>& speeds) const
{
    const auto key = download ? QStringLiteral("downloadSpeeds") : QStringLiteral("uploadSpeeds");
    QStringList values;
    for (const int speed : speeds)
        values.append(QString::number(speed));
    setValue(key, "downloads/" + key, values.join(","));
}
