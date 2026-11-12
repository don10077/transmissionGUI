// W08: DaemonProfile (url/toOptions: auth, TLS, прокси) + ProfileStore (CRUD в QSettings).
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSettings>
#include <QLineEdit>

#include "daemonprofile.h"
#include "optionsdialog.h"

namespace {
void isolate()
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));
}
void clearStore()
{
    QSettings s;
    s.remove(QStringLiteral("daemons"));
    s.sync();
}
}  // namespace

TEST(DaemonProfile, UrlHttpAndHttps)
{
    DaemonProfile p;
    p.host = "192.168.1.10";
    p.port = 9091;
    p.useSsl = false;
    EXPECT_EQ(p.url().scheme(), "http");
    EXPECT_EQ(p.url().host(), "192.168.1.10");
    EXPECT_EQ(p.url().port(), 9091u);
    p.useSsl = true;
    EXPECT_EQ(p.url().scheme(), "https");
}

TEST(DaemonProfile, ToOptionsAuthRpcPathAndNoProxy)
{
    DaemonProfile p;
    p.host = "daemon.local";
    p.port = 1234;
    p.rpcPath = "/web/rpc";
    p.username = "alice";
    p.password = "secret";
    p.ignoreSslErrors = true;
    auto o = p.toOptions();
    EXPECT_EQ(o.url.host(), "daemon.local");
    EXPECT_EQ(o.url.port(), 1234u);
    EXPECT_EQ(o.rpcPath, "/web/rpc");
    EXPECT_EQ(o.username, "alice");
    EXPECT_EQ(o.password, "secret");
    EXPECT_TRUE(o.ignoreSslErrors);
    EXPECT_EQ(o.proxy.type(), QNetworkProxy::NoProxy);
}

TEST(DaemonProfile, ToOptionsHttpProxy)
{
    DaemonProfile p;
    p.host = "daemon.local";
    p.useProxy = true;
    p.proxyHost = "proxy.local";
    p.proxyPort = 8080;
    p.proxyUser = "pu";
    p.proxyPass = "pp";
    auto o = p.toOptions();
    EXPECT_EQ(o.proxy.type(), QNetworkProxy::HttpProxy);
    EXPECT_EQ(o.proxy.hostName(), "proxy.local");
    EXPECT_EQ(o.proxy.port(), 8080u);
    EXPECT_EQ(o.proxy.user(), "pu");
    EXPECT_EQ(o.proxy.password(), "pp");
}

TEST(DaemonProfile, ToOptionsSocksProxy)
{
    DaemonProfile p;
    p.host = "daemon.local";
    p.useProxy = true;
    p.useSocks = true;
    p.proxyHost = "socks.local";
    p.proxyPort = 1080;
    auto o = p.toOptions();
    EXPECT_EQ(o.proxy.type(), QNetworkProxy::Socks5Proxy);
    EXPECT_EQ(o.proxy.hostName(), "socks.local");
    EXPECT_EQ(o.proxy.port(), 1080u);
}

TEST(ProfileStore, AddSaveReadRoundTrip)
{
    isolate();
    clearStore();
    ProfileStore store;
    store.add("home");
    EXPECT_TRUE(store.exists("home"));
    EXPECT_EQ(store.currentName(), "home");
    EXPECT_TRUE(store.hasCurrent());

    DaemonProfile p = store.profile("home");
    p.host = "10.0.0.5";
    p.port = 9091;
    p.useSsl = true;
    p.username = "u";
    p.clientCertificatePath = "client.pem";
    p.clientKeyPath = "client.key";
    store.save(p);

    DaemonProfile r = store.profile("home");
    EXPECT_EQ(r.host, "10.0.0.5");
    EXPECT_TRUE(r.useSsl);
    EXPECT_EQ(r.username, "u");
    EXPECT_EQ(r.toOptions().clientCertificatePath, "client.pem");
    EXPECT_EQ(r.toOptions().clientKeyPath, "client.key");
    EXPECT_FALSE(r.toOptions().clientKeyPassphrase.has_value());
    r.useSsl = false;
    EXPECT_TRUE(r.toOptions().clientCertificatePath.isEmpty());
    EXPECT_TRUE(r.toOptions().clientKeyPath.isEmpty());
    EXPECT_EQ(store.currentProfile().host, "10.0.0.5");
}

TEST(ProfileStore, LegacyPreferencesMigrateAndStayIndependentAfterRename)
{
    isolate();
    clearStore();
    QSettings settings;
    ProfileStore store;
    DaemonProfile first;
    first.name = "first";
    first.host = "daemon.local";
    store.add(first.name);
    store.save(first);
    store.add("second");
    settings.remove("daemons/preferencesMigrated"); // Имитируем настройки до обновления.
    const auto options = first.toOptions();
    const auto destinationsKey = QStringLiteral("downloads/destinations/") + QString::fromLatin1(
        QUrl::toPercentEncoding(options.url.toString(QUrl::RemoveUserInfo) + options.rpcPath));
    const auto cleanup = qScopeGuard([&] {
        for (const auto& key : {QString("paths/mappings"), QString("paths/moveDirs"),
                                QString("downloads/downloadSpeeds"), destinationsKey})
            settings.remove(key);
        clearStore();
    });
    settings.setValue("paths/mappings", QStringList{"/remote=/local"});
    settings.setValue("paths/moveDirs", QStringList{"/old"});
    settings.setValue("downloads/downloadSpeeds", "80,100");
    const QVariantMap templates{{"/archive", "*.mkv"}};
    settings.setValue(destinationsKey, templates);

    const ProfilePreferences a(first.name), b("second");
    EXPECT_EQ(a.mappings(), QStringList{"/remote=/local"});
    EXPECT_EQ(b.mappings(), a.mappings());
    EXPECT_EQ(b.moveHistory(), QStringList{"/old"});
    EXPECT_EQ(a.destinations(&options), templates);
    EXPECT_EQ(b.speedPresets(true), (QList<int>{80, 100}));

    OptionsDialog edit(nullptr, 0, nullptr, first.name);
    edit.setPathMappingsText("/remote=/first");
    auto* presets = edit.findChild<QLineEdit*>("downloadSpeedPresets");
    ASSERT_NE(presets, nullptr);
    presets->setText("200, 50 200");
    ASSERT_TRUE(edit.apply());
    a.rememberMove("/new");
    a.setDestinations(&options, {{"/new", "*.iso"}});
    EXPECT_EQ(b.mappings(), QStringList{"/remote=/local"});
    EXPECT_EQ(b.moveHistory(), QStringList{"/old"});
    EXPECT_TRUE(b.destinations(nullptr).isEmpty());
    EXPECT_EQ(b.speedPresets(true), (QList<int>{80, 100}));
    EXPECT_EQ(ProfilePreferences().mappings(), QStringList{"/remote=/local"});

    store.rename(first.name, "renamed");
    settings.sync();
    const ProfilePreferences reopened("renamed");
    EXPECT_EQ(reopened.mappings(), QStringList{"/remote=/first"});
    EXPECT_EQ(reopened.moveHistory(), (QStringList{"/new", "/old"}));
    EXPECT_EQ(reopened.destinations(&options), (QVariantMap{{"/new", "*.iso"}}));
    EXPECT_EQ(reopened.speedPresets(true), (QList<int>{50, 200}));

    store.add("new-profile");
    EXPECT_TRUE(ProfilePreferences("new-profile").mappings().isEmpty());
}

TEST(ProfileStore, AddDuplicateIgnored)
{
    isolate();
    clearStore();
    ProfileStore store;
    store.add("home");
    store.add("work");
    store.add("home");  // дубль — не должен добавить повторно
    const auto names = store.names();
    EXPECT_EQ(names.size(), 2);
    EXPECT_TRUE(names.contains("home"));
    EXPECT_TRUE(names.contains("work"));
}

TEST(ProfileStore, RemoveSwitchesCurrent)
{
    isolate();
    clearStore();
    ProfileStore store;
    store.add("home");
    store.add("work");
    store.setCurrent("work");
    store.remove("work");
    EXPECT_FALSE(store.exists("work"));
    EXPECT_TRUE(store.hasCurrent());
    EXPECT_NE(store.currentName(), "work");
}

TEST(ProfileStore, RenameKeepsDataAndCurrent)
{
    isolate();
    clearStore();
    ProfileStore store;
    store.add("a");
    DaemonProfile p = store.profile("a");
    p.host = "x";
    p.username = "user";
    store.save(p);

    store.rename("a", "b");
    EXPECT_FALSE(store.exists("a"));
    EXPECT_TRUE(store.exists("b"));
    const DaemonProfile r = store.profile("b");
    EXPECT_EQ(r.host, "x");
    EXPECT_EQ(r.username, "user");
    EXPECT_EQ(store.currentName(), "b");
}
