// Отображаемые имена торрентов: локальное хранилище по info-hash,
// нормализация и подстановка в модель без изменения данных демона.
#include <QSettings>
#include <QString>

#include <gtest/gtest.h>

#include "displaynames.h"
#include "torrentmodel.h"

namespace {
class DisplayNamesTest : public ::testing::Test {
protected:
    void SetUp() override { QSettings().remove(QString::fromLatin1(displaynames::settingsPrefix)); }
    void TearDown() override { QSettings().remove(QString::fromLatin1(displaynames::settingsPrefix)); }
};
} // namespace

TEST_F(DisplayNamesTest, NormalizesTrimsAndLimitsLength)
{
    EXPECT_EQ(displaynames::normalize(QStringLiteral("  spaced  ")), QStringLiteral("spaced"));
    EXPECT_EQ(displaynames::normalize(QStringLiteral("   ")), QString{});
    const QString longName(300, QLatin1Char('a'));
    EXPECT_EQ(displaynames::normalize(longName).size(), displaynames::maxLength);
    EXPECT_EQ(displaynames::normalize(QStringLiteral("日本語")), QStringLiteral("日本語"));
}

TEST_F(DisplayNamesTest, StoresNameByCaseInsensitiveHash)
{
    EXPECT_TRUE(displaynames::set(QStringLiteral("ABCDEF"), QStringLiteral("  Custom name  ")));
    EXPECT_EQ(displaynames::storedName(QStringLiteral("abcdef")), QStringLiteral("Custom name"));
    EXPECT_EQ(displaynames::storedName(QStringLiteral("AbCdEf")), QStringLiteral("Custom name"));

    // Повторная запись того же значения не считается изменением.
    EXPECT_FALSE(displaynames::set(QStringLiteral("abcdef"), QStringLiteral("Custom name")));
    EXPECT_TRUE(displaynames::set(QStringLiteral("abcdef"), QStringLiteral("Other")));
    EXPECT_EQ(displaynames::storedName(QStringLiteral("ABCDEF")), QStringLiteral("Other"));
}

TEST_F(DisplayNamesTest, EmptyNameRemovesStoredValue)
{
    ASSERT_TRUE(displaynames::set(QStringLiteral("hash"), QStringLiteral("Name")));
    EXPECT_TRUE(displaynames::remove(QStringLiteral("HASH")));
    EXPECT_TRUE(displaynames::storedName(QStringLiteral("hash")).isEmpty());
    EXPECT_FALSE(displaynames::remove(QStringLiteral("hash")));

    ASSERT_TRUE(displaynames::set(QStringLiteral("hash"), QStringLiteral("Name")));
    EXPECT_TRUE(displaynames::set(QStringLiteral("hash"), QString{}));
    EXPECT_TRUE(displaynames::storedName(QStringLiteral("hash")).isEmpty());
}

TEST_F(DisplayNamesTest, MissingHashIsIgnored)
{
    EXPECT_FALSE(displaynames::set(QString{}, QStringLiteral("Name")));
    EXPECT_FALSE(displaynames::remove(QStringLiteral("   ")));
    EXPECT_TRUE(displaynames::storedName(QString{}).isEmpty());
}

TEST_F(DisplayNamesTest, EffectiveNameFallsBackToMetadata)
{
    tgui::Torrent torrent;
    torrent.hashString = QStringLiteral("HASH");
    torrent.name = QStringLiteral("On disk");
    EXPECT_EQ(displaynames::effectiveName(torrent), QStringLiteral("On disk"));

    ASSERT_TRUE(displaynames::set(*torrent.hashString, QStringLiteral("Shown")));
    displaynames::apply(torrent);
    EXPECT_EQ(torrent.displayName, QStringLiteral("Shown"));
    EXPECT_EQ(displaynames::effectiveName(torrent), QStringLiteral("Shown"));

    // Имя на диске не меняется.
    EXPECT_EQ(torrent.name.value(), QStringLiteral("On disk"));

    ASSERT_TRUE(displaynames::remove(*torrent.hashString));
    displaynames::apply(torrent);
    EXPECT_TRUE(torrent.displayName.isEmpty());
    EXPECT_EQ(displaynames::effectiveName(torrent), QStringLiteral("On disk"));
}

TEST_F(DisplayNamesTest, TorrentWithoutHashHasNoStoredName)
{
    tgui::Torrent torrent;
    torrent.name = QStringLiteral("On disk");
    displaynames::apply(torrent);
    EXPECT_TRUE(torrent.displayName.isEmpty());
    EXPECT_EQ(displaynames::effectiveName(torrent), QStringLiteral("On disk"));
}

TEST_F(DisplayNamesTest, ModelShowsDisplayNameAndSearchMatchesBoth)
{
    tgui::Torrent torrent;
    torrent.id = 1;
    torrent.name = QStringLiteral("Disk name");
    torrent.hashString = QStringLiteral("HASH");
    ASSERT_TRUE(displaynames::set(*torrent.hashString, QStringLiteral("Shown name")));
    displaynames::apply(torrent);

    TorrentModel model;
    model.setTorrents({torrent});
    ASSERT_EQ(model.rowCount(), 1);
    const auto nameIndex = model.index(0, TorrentModel::ColName);
    EXPECT_EQ(model.data(nameIndex, Qt::DisplayRole).toString(), QStringLiteral("Shown name"));
    EXPECT_EQ(model.data(nameIndex, TorrentModel::SortRole).toString(), QStringLiteral("Shown name"));
    EXPECT_TRUE(model.data(nameIndex, Qt::ToolTipRole).toString().contains(QStringLiteral("Disk name")));

    TorrentFilterProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setSearchText(QStringLiteral("Shown"));
    EXPECT_EQ(proxy.rowCount(), 1);
    proxy.setSearchText(QStringLiteral("Disk"));
    EXPECT_EQ(proxy.rowCount(), 1);
    proxy.setSearchText(QStringLiteral("missing"));
    EXPECT_EQ(proxy.rowCount(), 0);
}
