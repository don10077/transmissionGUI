// W08: DaemonProfile (url/toOptions: auth, TLS, прокси) + ProfileStore (CRUD в QSettings).
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QNetworkProxy>
#include <QSettings>

#include "daemonprofile.h"

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
    store.save(p);

    DaemonProfile r = store.profile("home");
    EXPECT_EQ(r.host, "10.0.0.5");
    EXPECT_TRUE(r.useSsl);
    EXPECT_EQ(r.username, "u");
    EXPECT_EQ(store.currentProfile().host, "10.0.0.5");
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