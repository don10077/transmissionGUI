// W07: OptionsDialog — значения сохраняются в QSettings (ui/*) и читаются обратно;
// язык/масштаб требуют перезапуска, интервал/трей — нет.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSettings>

#include "optionsdialog.h"

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