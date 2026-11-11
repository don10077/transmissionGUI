// W07: OptionsDialog — значения сохраняются в QSettings (ui/*) и читаются обратно;
// язык/масштаб требуют перезапуска, интервал/трей — нет.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QSettings>

#include "fileselectdialog.h"
#include "optionsdialog.h"
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
    settings.remove(torrentlabels::settingsKey);
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
