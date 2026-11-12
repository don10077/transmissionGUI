// W07: OptionsDialog — значения сохраняются в QSettings (ui/*) и читаются обратно;
// язык/масштаб требуют перезапуска, интервал/трей — нет.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>

#include "fileselectdialog.h"
#include "optionsdialog.h"
#include "settingsbackup.h"
#include "torrentlabels.h"

namespace {
void resetUiKeys()
{
    QSettings s;
    for (const char* k : {"ui/language",     "ui/scale",            "ui/refreshInterval",
                          "ui/closeToTray",  "ui/minimizeToTray",   "ui/trayIcon", "ui/trayNotify"})
        s.remove(k);
    s.sync();
}

void isolateTestSettings()
{
    // Изоляция от реальных настроек пользователя (как в test_panels/test_e2e).
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));
}
}  // namespace

TEST(OptionsDialog, DefaultsAfterReset)
{
    isolateTestSettings();
    resetUiKeys();
    OptionsDialog dlg;
    EXPECT_EQ(dlg.currentLanguage(), QStringLiteral("ru"));  // по умолчанию русский
    EXPECT_EQ(dlg.scale(), 100);
    EXPECT_EQ(dlg.refreshIntervalSec(), 2);
}

TEST(OptionsDialog, SaveAndPersist)
{
    isolateTestSettings();
    resetUiKeys();
    OptionsDialog dlg;
    dlg.setLanguage("en");
    dlg.setScale(125);
    dlg.setRefreshIntervalSec(5);
    dlg.apply();

    QSettings s;
    EXPECT_EQ(s.value("ui/language").toString(), "en");
    EXPECT_EQ(s.value("ui/scale").toInt(), 125);
    EXPECT_EQ(s.value("ui/refreshInterval").toInt(), 5);
    EXPECT_TRUE(dlg.restartRequired());  // язык и масштаб изменились
}

TEST(OptionsDialog, RefreshOnlyNoRestart)
{
    isolateTestSettings();
    resetUiKeys();
    OptionsDialog dlg;
    dlg.setRefreshIntervalSec(3);  // только интервал (язык/масштаб не меняем)
    dlg.apply();
    EXPECT_FALSE(dlg.restartRequired());
    EXPECT_EQ(QSettings().value("ui/refreshInterval").toInt(), 3);
}

TEST(OptionsDialog, AddPreferencesAreOptInAndPersist)
{
    isolateTestSettings();
    QSettings settings;
    settings.remove(OptionsDialog::skipAddDialogKey);
    settings.remove(OptionsDialog::deleteSourceTorrentKey);

    OptionsDialog dialog;
    auto* skip = dialog.findChild<QCheckBox*>("skipAddDialog");
    auto* remove = dialog.findChild<QCheckBox*>("deleteSourceTorrent");
    ASSERT_NE(skip, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_FALSE(skip->isChecked());
    EXPECT_FALSE(remove->isChecked());
    skip->setChecked(true);
    remove->setChecked(true);
    ASSERT_TRUE(dialog.apply());
    OptionsDialog reopened;
    EXPECT_TRUE(reopened.findChild<QCheckBox*>("skipAddDialog")->isChecked());
    EXPECT_TRUE(reopened.findChild<QCheckBox*>("deleteSourceTorrent")->isChecked());
    settings.remove(OptionsDialog::skipAddDialogKey);
    settings.remove(OptionsDialog::deleteSourceTorrentKey);
}

TEST(SettingsBackup, ValidatesBeforeReplacingAndRoundTripsProfiles)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto valid = directory.filePath("backup.ini");
    const auto invalid = directory.filePath("foreign.ini");
    const QVariantMap exported{{"ui/refreshInterval", 3}, {"daemons/names", QStringList{"server"}},
                               {"daemons/current", "server"}, {"daemons/server/host", "localhost"}};
    ASSERT_TRUE(settingsbackup::write(valid, exported));
    ASSERT_TRUE(settingsbackup::writeFile(invalid, "[foreign]\nsetting=42\n"));
    EXPECT_FALSE(settingsbackup::read(invalid).has_value());
    const auto loaded = settingsbackup::read(valid);
    ASSERT_TRUE(loaded.has_value());
    QSettings destination(directory.filePath("destination.ini"), QSettings::IniFormat);
    destination.setValue("ui/obsolete", true);
    ASSERT_TRUE(settingsbackup::replace(destination, *loaded));
    EXPECT_FALSE(destination.contains("ui/obsolete"));
    EXPECT_EQ(destination.value("ui/refreshInterval").toInt(), 3);
    EXPECT_EQ(destination.value("daemons/server/host").toString(), "localhost");
    EXPECT_FALSE(destination.contains("backup/version"));
}

TEST(OptionsDialog, TrayDefaultsPersist)
{
    isolateTestSettings();
    resetUiKeys();
    OptionsDialog dlg;
    dlg.apply();  // записывает дефолты: closeToTray=true, minimizeToTray=false, trayIcon=true, trayNotify=false
    QSettings s;
    EXPECT_TRUE(s.value("ui/closeToTray").toBool());
    EXPECT_FALSE(s.value("ui/minimizeToTray").toBool());
    EXPECT_TRUE(s.value("ui/trayIcon").toBool());
    EXPECT_FALSE(s.value("ui/trayNotify").toBool());
}

TEST(OptionsDialog, ConfiguredLabelsAreAvailableWhenAddingTorrent)
{
    isolateTestSettings();
    QSettings settings;
    const QVariant previous = settings.value(torrentlabels::settingsKey);
    settings.setValue(torrentlabels::settingsKey, QStringList{});
    const tgui::TorrentDetails details;
    FileSelectDialog withoutLabels(details, false, 16);
    EXPECT_EQ(withoutLabels.findChild<QComboBox*>("torrentLabel"), nullptr);

    OptionsDialog options;
    auto* editor = options.findChild<QPlainTextEdit*>("labelNames");
    ASSERT_NE(editor, nullptr);
    editor->setPlainText(" Video \n\nMusic\nVideo");
    options.apply();
    EXPECT_EQ(torrentlabels::configured(), (QStringList{"Video", "Music"}));
    EXPECT_FALSE(options.restartRequired());
    OptionsDialog reopened;
    auto* savedEditor = reopened.findChild<QPlainTextEdit*>("labelNames");
    ASSERT_NE(savedEditor, nullptr);
    EXPECT_EQ(savedEditor->toPlainText(), QStringLiteral("Video\nMusic"));

    FileSelectDialog add(details, false, 16);
    auto* picker = add.findChild<QComboBox*>("torrentLabel");
    ASSERT_NE(picker, nullptr);
    EXPECT_TRUE(add.options().labels.isEmpty());
    picker->setCurrentIndex(2);
    EXPECT_EQ(add.options().labels, QStringList{"Music"});

    FileSelectDialog unsupported(details, false, 15);
    EXPECT_EQ(unsupported.findChild<QComboBox*>("torrentLabel"), nullptr);
    if (previous.isValid())
        settings.setValue(torrentlabels::settingsKey, previous);
    else
        settings.remove(torrentlabels::settingsKey);
}

TEST(OptionsDialog, DefaultLabelsCanBeRemovedAndRestored)
{
    isolateTestSettings();
    QSettings settings;
    const auto previous = settings.value(torrentlabels::settingsKey);
    settings.remove(torrentlabels::settingsKey);
    EXPECT_EQ(torrentlabels::configured(),
              (QStringList{"Video", "Serials", "Pictures", "Soft", "Music", "Books", "Games"}));
    OptionsDialog options;
    auto* editor = options.findChild<QPlainTextEdit*>("labelNames");
    auto* restore = options.findChild<QPushButton*>("restoreDefaultLabels");
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(restore, nullptr);
    EXPECT_FALSE(restore->isEnabled());

    editor->setPlainText(QStringLiteral("Custom\nMusic"));
    ASSERT_TRUE(restore->isEnabled());
    restore->click();
    EXPECT_FALSE(restore->isEnabled());
    ASSERT_TRUE(options.apply());
    EXPECT_EQ(torrentlabels::configured(),
              (QStringList{"Custom", "Music", "Video", "Serials", "Pictures", "Soft", "Books", "Games"}));
    editor->clear();
    ASSERT_TRUE(options.apply());
    EXPECT_TRUE(torrentlabels::configured().isEmpty());
    if (previous.isValid())
        settings.setValue(torrentlabels::settingsKey, previous);
    else
        settings.remove(torrentlabels::settingsKey);
}
