// W09: DaemonSettings — fromSession (SessionInfo → struct) и toSessionSetJson (struct → session-set payload).
#include <gtest/gtest.h>

#include <QJsonObject>

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