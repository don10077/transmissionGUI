#include <gtest/gtest.h>

#include "openpath.h"
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

TEST(PathMap, LocalPathWithoutMappingHasNoRemotePath)
{
    const QStringList mappings{"/downloads=C:/shared"};
    EXPECT_EQ(tgui::mapLocalToRemote("C:/other", mappings), std::nullopt);
    EXPECT_EQ(tgui::mapLocalToRemote("C:/shared/movies", mappings).value_or(QString()),
              QStringLiteral("/downloads/movies"));
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

TEST(PathMap, LongestMatchingRuleWinsAndRejectsTraversal)
{
    const QStringList m = {"/data=X:", "/data/music=Y:", "C:/Data=Z:/shared=music"};
    EXPECT_EQ(tgui::mapRemoteToLocal("/data/music/a", m), QStringLiteral("Y:/a"));
    EXPECT_EQ(tgui::mapRemoteToLocal("c:/data/Album", m), QStringLiteral("Z:/shared=music/Album"));
    EXPECT_EQ(tgui::mapRemoteToLocal("/data/music/../../Windows", m), std::nullopt);
    EXPECT_EQ(tgui::mapRemoteToLocal("/data/.. /Windows", m), std::nullopt);
}

TEST(OpenPath, RecognizesProgramsAndShortcutsWithoutBlockingMovies)
{
    EXPECT_TRUE(tgui::isPotentiallyExecutable(QFileInfo("setup.exe")));
    EXPECT_TRUE(tgui::isPotentiallyExecutable(QFileInfo("a.LNK")));
    EXPECT_FALSE(tgui::isPotentiallyExecutable(QFileInfo("movie.mkv")));
}

namespace {

tgui::FileStat file(const QString& name)
{
    tgui::FileStat stat;
    stat.name = name;
    return stat;
}

}  // namespace

TEST(OpenPath, SingleFileOpensFileOrSelectsIt)
{
    const QVector<tgui::FileStat> files{file("movie.mkv")};
    bool selectFile = false;
    EXPECT_EQ(tgui::torrentOpenRelativePath(files, /*containingFolder=*/false, &selectFile),
              QStringLiteral("movie.mkv"));
    EXPECT_FALSE(selectFile);
    EXPECT_EQ(tgui::torrentOpenRelativePath(files, /*containingFolder=*/true, &selectFile),
              QStringLiteral("movie.mkv"));
    EXPECT_TRUE(selectFile);
}

TEST(OpenPath, MultiFileOpensCommonRoot)
{
    const QVector<tgui::FileStat> files{
        file("Renamed Root/a.mkv"), file("Renamed Root/sub/b.srt")};
    bool selectFile = true;
    EXPECT_EQ(tgui::torrentOpenRelativePath(files, /*containingFolder=*/false, &selectFile),
              QStringLiteral("Renamed Root/"));
    EXPECT_FALSE(selectFile);
    EXPECT_EQ(tgui::commonFileDirectory(files), QStringLiteral("Renamed Root/"));
}

TEST(OpenPath, MultiFileWithoutCommonRootFallsBackToDownloadDir)
{
    const QVector<tgui::FileStat> files{file("first.mkv"), file("second.mkv")};
    EXPECT_TRUE(tgui::commonFileDirectory(files).isEmpty());
    EXPECT_TRUE(tgui::torrentOpenRelativePath(files, false, nullptr).isEmpty());
}

}  // namespace
