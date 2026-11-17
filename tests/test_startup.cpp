// TST-03: юнит-тесты разбора аргументов командной строки (parseStartup).
#include <gtest/gtest.h>

#include <QFileInfo>

#include "startup.h"

TEST(ParseStartup, UrlWithoutSchemeIsRejected)
{
    const auto r = parseStartup({"transmissiongui", "--url", "127.0.0.1:9091"});
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.isEmpty());
    EXPECT_FALSE(r.hasUrl);
}

TEST(ParseStartup, ValidUrlIsAccepted)
{
    const auto r = parseStartup({"transmissiongui", "--url", "http://127.0.0.1:9091", "--path", "/transmission/rpc"});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.hasUrl);
    EXPECT_EQ(r.url, "http://127.0.0.1:9091");
    EXPECT_EQ(r.rpcPath, "/transmission/rpc");
}

TEST(ParseStartup, PositionalAfterDoubleDash)
{
    // Путь со spaces после «--» — как из проводника/командной строки Windows.
    const auto r = parseStartup({"transmissiongui", "--", "C:\\a b.torrent"});
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.inputs.size(), 1);
    EXPECT_EQ(QFileInfo(r.inputs.first()).absoluteFilePath(),
              QFileInfo("C:\\a b.torrent").absoluteFilePath());
}

TEST(ParseStartup, MagnetLinkIsAccepted)
{
    const char* magnet =
        "magnet:?xt=urn:btih:561a2475350a617b377677d6269e0e327f1735f1&dn=example";
    const auto r = parseStartup({"transmissiongui", magnet});
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.inputs.size(), 1);
    EXPECT_EQ(r.inputs.first(), QString::fromLatin1(magnet));
    // Magnet без xt — отбрасывается, приложение запускается без ввода.
    const auto bad = parseStartup({"transmissiongui", "magnet:?dn=example"});
    EXPECT_TRUE(bad.ok);
    EXPECT_TRUE(bad.inputs.isEmpty());
}

TEST(ParseStartup, MultipleFilesDeduplicated)
{
    const auto r = parseStartup({"transmissiongui",
                                 "one.torrent",
                                 "two.torrent",
                                 "one.torrent"});
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.inputs.size(), 2);
    EXPECT_EQ(QFileInfo(r.inputs.at(0)).absoluteFilePath(), QFileInfo("one.torrent").absoluteFilePath());
    EXPECT_EQ(QFileInfo(r.inputs.at(1)).absoluteFilePath(), QFileInfo("two.torrent").absoluteFilePath());
}

TEST(ParseStartup, UnicodePath)
{
    const auto r = parseStartup({"transmissiongui", "C:\\Данные\\фильм.torrent"});
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.inputs.size(), 1);
    EXPECT_EQ(QFileInfo(r.inputs.first()).absoluteFilePath(),
              QFileInfo(QStringLiteral("C:\\Данные\\фильм.torrent")).absoluteFilePath());
}

TEST(ParseStartup, CredentialsAndFlags)
{
    const auto r = parseStartup({"transmissiongui",
                                 "--url", "http://127.0.0.1:9091",
                                 "--user", "bob",
                                 "--pass", "secret",
                                 "--smoke-test"});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.hasUser);
    EXPECT_EQ(r.username, "bob");
    EXPECT_TRUE(r.hasPassword);
    EXPECT_EQ(r.password, "secret");
    EXPECT_TRUE(r.smokeTest);
    EXPECT_FALSE(r.passwordFromStdin);

    const auto stdinPass = parseStartup({"transmissiongui", "--pass-stdin"});
    EXPECT_TRUE(stdinPass.ok);
    EXPECT_TRUE(stdinPass.passwordFromStdin);
    EXPECT_FALSE(stdinPass.hasPassword);
}

TEST(ParseStartup, HelpAndVersionAreFlagged)
{
    const auto help = parseStartup({"transmissiongui", "--help"});
    EXPECT_TRUE(help.showHelp);
    EXPECT_FALSE(help.ok);
    const auto version = parseStartup({"transmissiongui", "--version"});
    EXPECT_TRUE(version.showVersion);
    EXPECT_FALSE(version.ok);
}