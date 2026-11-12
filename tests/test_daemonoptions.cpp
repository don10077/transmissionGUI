// W09: DaemonSettings — fromSession (SessionInfo → struct) и toSessionSetJson (struct → session-set payload).
#include <gtest/gtest.h>

#include <QJsonObject>
#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>

#include "daemonoptionsdialog.h"
#include "tgui/models.h"

TEST(DaemonSettings, FromSessionMapsFields)
{
    tgui::SessionInfo s;
    s.speedLimitDownEnabled = true;
    s.speedLimitDown = 500;
    s.speedLimitUpEnabled = false;
    s.speedLimitUp = 200;
    s.altSpeedEnabled = true;
    s.altSpeedDown = 50;
    s.altSpeedUp = 60;
    s.dhtEnabled = true;
    s.pexEnabled = false;
    s.lpdEnabled = true;
    s.utpEnabled = true;
    s.peerPort = 60000;
    s.downloadDir = "/dl";
    s.startAddedTorrents = false;
    s.ratioLimitEnabled = true;
    s.ratioLimit = 3.5;
    s.blocklistEnabled = true;
    s.blocklistUrl = "http://bl.list";

    const DaemonSettings d = DaemonSettings::fromSession(s);
    EXPECT_TRUE(d.speedLimitDownEnabled);
    EXPECT_EQ(d.speedLimitDown, 500);
    EXPECT_FALSE(d.speedLimitUpEnabled);
    EXPECT_TRUE(d.altSpeedEnabled);
    EXPECT_EQ(d.altSpeedDown, 50);
    EXPECT_EQ(d.altSpeedUp, 60);
    EXPECT_TRUE(d.dht);
    EXPECT_FALSE(d.pex);
    EXPECT_TRUE(d.lpd);
    EXPECT_TRUE(d.utp);
    EXPECT_EQ(d.peerPort, 60000);
    EXPECT_EQ(d.downloadDir, "/dl");
    EXPECT_FALSE(d.startAddedTorrents);
    EXPECT_TRUE(d.ratioLimitEnabled);
    EXPECT_DOUBLE_EQ(d.ratioLimit, 3.5);
    EXPECT_TRUE(d.blocklistEnabled);
    EXPECT_EQ(d.blocklistUrl, "http://bl.list");
}

TEST(DaemonSettings, ToSessionSetJsonKeys)
{
    tgui::SessionInfo s;
    s.speedLimitDownEnabled = true;
    s.speedLimitDown = 500;
    s.speedLimitUpEnabled = true;
    s.speedLimitUp = 300;
    s.altSpeedEnabled = true;
    s.altSpeedDown = 50;
    s.altSpeedUp = 60;
    s.dhtEnabled = true;
    s.pexEnabled = true;
    s.lpdEnabled = false;
    s.utpEnabled = true;
    s.peerPort = 12345;
    s.downloadDir = "/downloads";
    s.startAddedTorrents = true;
    s.ratioLimitEnabled = true;
    s.ratioLimit = 2.0;
    s.blocklistEnabled = true;
    s.blocklistUrl = "http://example/blist.txt";

    const QJsonObject j = DaemonSettings::fromSession(s).toSessionSetJson();
    EXPECT_TRUE(j.value("speed-limit-down-enabled").toBool());
    EXPECT_EQ(j.value("speed-limit-down").toInt(), 500);
    EXPECT_TRUE(j.value("speed-limit-up-enabled").toBool());
    EXPECT_EQ(j.value("speed-limit-up").toInt(), 300);
    EXPECT_TRUE(j.value("alt-speed-enabled").toBool());
    EXPECT_EQ(j.value("alt-speed-down").toInt(), 50);
    EXPECT_EQ(j.value("alt-speed-up").toInt(), 60);
    EXPECT_TRUE(j.value("dht-enabled").toBool());
    EXPECT_TRUE(j.value("pex-enabled").toBool());
    EXPECT_FALSE(j.value("lpd-enabled").toBool());
    EXPECT_TRUE(j.value("utp-enabled").toBool());
    EXPECT_EQ(j.value("peer-port").toInt(), 12345);
    EXPECT_EQ(j.value("download-dir").toString(), "/downloads");
    EXPECT_TRUE(j.value("start-added-torrents").toBool());
    EXPECT_TRUE(j.value("ratio-limit-enabled").toBool());
    EXPECT_DOUBLE_EQ(j.value("ratio-limit").toDouble(), 2.0);
    EXPECT_TRUE(j.value("blocklist-enabled").toBool());
    EXPECT_EQ(j.value("blocklist-url").toString(), "http://example/blist.txt");
}

TEST(DaemonOptionsDialog, RoundTripSettings)
{
    tgui::SessionInfo s;
    s.speedLimitDown = 777;
    s.altSpeedDown = 333;
    s.peerPort = 22222;
    s.downloadDir = "/tmp/dl";

    DaemonOptionsDialog dlg(s);
    const DaemonSettings d = dlg.settings();
    EXPECT_EQ(d.speedLimitDown, 777);
    EXPECT_EQ(d.altSpeedDown, 333);
    EXPECT_EQ(d.peerPort, 22222);
    EXPECT_EQ(d.downloadDir, "/tmp/dl");

    const QJsonObject j = dlg.sessionSet();
    EXPECT_EQ(j.value("speed-limit-down").toInt(), 777);
    EXPECT_EQ(j.value("alt-speed-down").toInt(), 333);
    EXPECT_EQ(j.value("peer-port").toInt(), 22222);
    EXPECT_EQ(j.value("download-dir").toString(), "/tmp/dl");
}

TEST(DaemonSettings, FromSessionMapsExtendedFields)
{
    tgui::SessionInfo s;
    s.rpcVersion = 15;
    s.altSpeedTimeEnabled = true;
    s.altSpeedTimeBegin = 120;
    s.altSpeedTimeEnd = 1440;
    s.altSpeedTimeDay = 2 | 4; // Mon + Tue
    s.encryption = "tolerated";
    s.peerLimitGlobal = 200;
    s.peerLimitPerTorrent = 50;
    s.portForwardingEnabled = true;
    s.peerPortRandomOnStart = true;
    s.peerPortRandomLow = 49152;
    s.peerPortRandomHigh = 65535;
    s.incompleteDir = "/incomplete";
    s.incompleteDirEnabled = true;
    s.renamePartialFiles = true;
    s.downloadQueueEnabled = true;
    s.downloadQueueSize = 4;
    s.seedQueueEnabled = true;
    s.seedQueueSize = 6;
    s.queueStalledEnabled = true;
    s.queueStalledMinutes = 30;
    s.idleSeedingLimitEnabled = true;
    s.idleSeedingLimit = 60;
    s.cacheSizeMb = 64;
    s.hasCacheSize = true;

    const DaemonSettings d = DaemonSettings::fromSession(s);
    EXPECT_TRUE(d.altSpeedTimeEnabled);
    EXPECT_EQ(d.altSpeedTimeBegin, 120);
    EXPECT_EQ(d.altSpeedTimeEnd, 1440);
    EXPECT_EQ(d.altSpeedTimeDay, 6);
    EXPECT_EQ(d.encryption, "tolerated");
    EXPECT_EQ(d.peerLimitGlobal, 200);
    EXPECT_EQ(d.peerLimitPerTorrent, 50);
    EXPECT_TRUE(d.portForwardingEnabled);
    EXPECT_TRUE(d.peerPortRandomOnStart);
    EXPECT_EQ(d.peerPortRandomLow, 49152);
    EXPECT_EQ(d.peerPortRandomHigh, 65535);
    EXPECT_EQ(d.incompleteDir, "/incomplete");
    EXPECT_TRUE(d.incompleteDirEnabled);
    EXPECT_TRUE(d.renamePartialFiles);
    EXPECT_TRUE(d.downloadQueueEnabled);
    EXPECT_EQ(d.downloadQueueSize, 4);
    EXPECT_TRUE(d.seedQueueEnabled);
    EXPECT_EQ(d.seedQueueSize, 6);
    EXPECT_TRUE(d.queueStalledEnabled);
    EXPECT_EQ(d.queueStalledMinutes, 30);
    EXPECT_TRUE(d.idleSeedingLimitEnabled);
    EXPECT_EQ(d.idleSeedingLimit, 60);
    EXPECT_EQ(d.cacheSizeMb, 64);
    EXPECT_TRUE(d.hasCacheSize);
    EXPECT_EQ(d.rpcVersion, 15);
}

TEST(DaemonSettings, ExtendedSessionSetKeys)
{
    DaemonSettings d;
    d.altSpeedTimeEnabled = true;
    d.altSpeedTimeBegin = 90;
    d.altSpeedTimeEnd = 1440;
    d.altSpeedTimeDay = 127;
    d.encryption = "required";
    d.peerLimitGlobal = 300;
    d.peerLimitPerTorrent = 70;
    d.portForwardingEnabled = true;
    d.peerPortRandomOnStart = true;
    d.peerPortRandomLow = 50000;
    d.peerPortRandomHigh = 60000;
    d.incompleteDir = "/inc";
    d.incompleteDirEnabled = true;
    d.renamePartialFiles = true;
    d.downloadQueueEnabled = true;
    d.downloadQueueSize = 3;
    d.seedQueueEnabled = true;
    d.seedQueueSize = 5;
    d.queueStalledEnabled = true;
    d.queueStalledMinutes = 20;
    d.idleSeedingLimitEnabled = true;
    d.idleSeedingLimit = 45;
    d.cacheSizeMb = 128;
    d.hasCacheSize = true;

    const QJsonObject j = d.toSessionSetJson();
    EXPECT_TRUE(j.value("alt-speed-time-enabled").toBool());
    EXPECT_EQ(j.value("alt-speed-time-begin").toInt(), 90);
    EXPECT_EQ(j.value("alt-speed-time-end").toInt(), 1440);
    EXPECT_EQ(j.value("alt-speed-time-day").toInt(), 127);
    EXPECT_EQ(j.value("encryption").toString(), "required");
    EXPECT_EQ(j.value("peer-limit-global").toInt(), 300);
    EXPECT_EQ(j.value("peer-limit-per-torrent").toInt(), 70);
    EXPECT_TRUE(j.value("port-forwarding-enabled").toBool());
    EXPECT_TRUE(j.value("peer-port-random-on-start").toBool());
    EXPECT_EQ(j.value("peer-port-random-low").toInt(), 50000);
    EXPECT_EQ(j.value("peer-port-random-high").toInt(), 60000);
    EXPECT_EQ(j.value("incomplete-dir").toString(), "/inc");
    EXPECT_TRUE(j.value("incomplete-dir-enabled").toBool());
    EXPECT_TRUE(j.value("rename-partial-files").toBool());
    EXPECT_TRUE(j.value("download-queue-enabled").toBool());
    EXPECT_EQ(j.value("download-queue-size").toInt(), 3);
    EXPECT_TRUE(j.value("seed-queue-enabled").toBool());
    EXPECT_EQ(j.value("seed-queue-size").toInt(), 5);
    EXPECT_TRUE(j.value("queue-stalled-enabled").toBool());
    EXPECT_EQ(j.value("queue-stalled-minutes").toInt(), 20);
    EXPECT_TRUE(j.value("idle-seeding-limit-enabled").toBool());
    EXPECT_EQ(j.value("idle-seeding-limit").toInt(), 45);
    EXPECT_EQ(j.value("cache-size-mb").toInt(), 128);
}

TEST(DaemonSettings, ChangesFromOnlyChangedFields)
{
    tgui::SessionInfo s;
    s.rpcVersion = 15;
    s.downloadQueueSize = 2;
    s.peerLimitGlobal = 100;
    s.ratioLimit = 1.5;
    s.ratioLimitEnabled = true;
    const DaemonSettings base = DaemonSettings::fromSession(s);

    DaemonSettings changed = base;
    changed.downloadQueueSize = 7;
    const QJsonObject diff = changed.changesFrom(base);
    ASSERT_EQ(diff.size(), 1);
    EXPECT_EQ(diff.value("download-queue-size").toInt(), 7);
    EXPECT_FALSE(diff.contains("peer-limit-global"));
    EXPECT_FALSE(diff.contains("ratio-limit"));
}

TEST(DaemonOptionsDialog, ChangedSessionSetOnlyIncludesEditedField)
{
    tgui::SessionInfo s;
    s.rpcVersion = 15;
    s.downloadQueueSize = 2;
    s.peerLimitGlobal = 100;
    DaemonOptionsDialog dlg(s);
    auto* size = dlg.findChild<QSpinBox*>(QStringLiteral("downloadQueueSize"));
    ASSERT_NE(size, nullptr);
    size->setValue(7);
    const QJsonObject diff = dlg.changedSessionSet();
    ASSERT_EQ(diff.size(), 1);
    EXPECT_EQ(diff.value("download-queue-size").toInt(), 7);
}

TEST(DaemonOptionsDialog, VersionGatesDisableUnsupportedGroups)
{
    tgui::SessionInfo old;
    old.rpcVersion = 13;
    old.hasCacheSize = true;
    DaemonOptionsDialog oldDialog(old);
    // Очереди требуют RPC >= 14; incomplete-dir доступен с 7; кеш — с 10 при наличии поля.
    EXPECT_FALSE(oldDialog.findChild<QGroupBox*>(QStringLiteral("queueGroup"))->isEnabled());
    EXPECT_TRUE(oldDialog.findChild<QGroupBox*>(QStringLiteral("incompleteGroup"))->isEnabled());
    EXPECT_TRUE(oldDialog.findChild<QGroupBox*>(QStringLiteral("cacheGroup"))->isEnabled());
    EXPECT_TRUE(oldDialog.findChild<QGroupBox*>(QStringLiteral("scheduleGroup"))->isEnabled());

    tgui::SessionInfo modern;
    modern.rpcVersion = 15;
    modern.hasCacheSize = false;
    DaemonOptionsDialog modernDialog(modern);
    EXPECT_TRUE(modernDialog.findChild<QGroupBox*>(QStringLiteral("queueGroup"))->isEnabled());
    // Демон не вернул cache-size-mb — блок кеша недоступен.
    EXPECT_FALSE(modernDialog.findChild<QGroupBox*>(QStringLiteral("cacheGroup"))->isEnabled());

    tgui::SessionInfo ancient;
    ancient.rpcVersion = 4;
    ancient.hasCacheSize = true;
    DaemonOptionsDialog ancientDialog(ancient);
    EXPECT_FALSE(ancientDialog.findChild<QGroupBox*>(QStringLiteral("scheduleGroup"))->isEnabled());
    EXPECT_FALSE(ancientDialog.findChild<QGroupBox*>(QStringLiteral("incompleteGroup"))->isEnabled());
    EXPECT_FALSE(ancientDialog.findChild<QGroupBox*>(QStringLiteral("idleGroup"))->isEnabled());
}