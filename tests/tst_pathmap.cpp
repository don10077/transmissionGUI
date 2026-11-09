#include <gtest/gtest.h>

#include "pathmap.h"

namespace {

TEST(PathMap, ExactMatch)
{
    const QStringList m = {"/var/music=Z:/music"};
    EXPECT_EQ(tgui::mapRemoteToLocal("/var/music", m), "Z:/music");
}

TEST(PathMap, PrefixMatch)
{
    const QStringList m = {"/var/downloads/music=Z:/music"};
    EXPECT_EQ(tgui::mapRemoteToLocal("/var/downloads/music/artist/track.mp3", m),
              "Z:/music/artist/track.mp3");
}

TEST(PathMap, NoMatchReturnsOriginal)
{
    const QStringList m = {"/var/music=Z:/music"};
    EXPECT_EQ(tgui::mapRemoteToLocal("/other/file.mp4", m), "/other/file.mp4");
}

TEST(PathMap, WindowsSeparatorsNormalized)
{
    const QStringList m = {QStringLiteral("C:\\music=D:/music")};
    EXPECT_EQ(tgui::mapRemoteToLocal(QStringLiteral("C:\\music\\a.mp4"), m), "D:/music/a.mp4");
}

TEST(PathMap, TrailingSlashInRule)
{
    const QStringList m = {"/var/music/=Z:/music/"};
    EXPECT_EQ(tgui::mapRemoteToLocal("/var/music/x.mp4", m), "Z:/music/x.mp4");
}

TEST(PathMap, EmptyMappings)
{
    EXPECT_EQ(tgui::mapRemoteToLocal("/x/y", {}), "/x/y");
}

TEST(PathMap, FirstMatchingRuleWins)
{
    const QStringList m = {"/a=1", "/a/b=2"};
    // Правила проверяются по порядку: /a совпадает префиксно первым → "1/b".
    EXPECT_EQ(tgui::mapRemoteToLocal("/a/b", m), "1/b");
}

}  // namespace