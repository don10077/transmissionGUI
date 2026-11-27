// W07: OptionsDialog — значения сохраняются в QSettings (ui/*) и читаются обратно;
// язык/масштаб требуют перезапуска, интервал/трей — нет.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>

#include "fileselectdialog.h"
#include "optionsdialog.h"
#include "settingsbackup.h"
#include "torrentlabels.h"

namespace {
void resetUiKeys()
{
    QSettings s;
    for (const char* k : {"ui/language",     "ui/scale",            "ui/refreshInterval",
                          "ui/closeToTray",  "ui/minimizeToTray",   "ui/trayIcon", "ui/trayNotify",
                          "ui/updateCheckEnabled", "ui/updateCheckDays"})
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
    EXPECT_EQ(dlg.currentLanguage(), QStringLiteral("en"));  // по умолчанию английский
    EXPECT_EQ(dlg.scale(), 100);
    EXPECT_EQ(dlg.refreshIntervalSec(), 2);
}

TEST(OptionsDialog, TabsAreInsideDecoratedDialogBody)
{
    isolateTestSettings();
    OptionsDialog dialog;
    const auto* body = dialog.findChild<QWidget*>(QStringLiteral("dialogBody"));
    ASSERT_NE(body, nullptr);
    const auto* tabs = body->findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    EXPECT_GT(tabs->count(), 0);
}

TEST(OptionsDialog, ProgressColorsApplyAndResetWithoutRestart)
{
    using namespace tgui::theme;
    isolateTestSettings();
    resetUiKeys();
    QSettings settings;
    const auto& defaults = defaultProgressColors();
    saveProgressColors(settings, defaults);
    const auto restore = qScopeGuard([&] { saveProgressColors(settings, defaults); });
    const auto defaultDownload = progressColor(ProgressState::Downloading);
    const QColor custom(230, 218, 180);
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [] {
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            ADD_FAILURE() << "The progress color dialog did not finish";
            modal->reject();
        }
    });
    watchdog.start(3000);

    OptionsDialog options;
    auto* settingsButton = options.findChild<QPushButton*>("progressColorsButton");
    ASSERT_NE(settingsButton, nullptr);
    QTimer::singleShot(0, &options, [&] {
        auto* dialog = options.findChild<QDialog*>("tguiDialog");
        ASSERT_NE(dialog, nullptr);
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        ASSERT_NE(buttons, nullptr);
        QPushButton* colorButton = nullptr;
        for (auto* button : dialog->findChildren<QPushButton*>())
            if (button->text() == defaultDownload.name().toUpper())
                colorButton = button;
        EXPECT_NE(colorButton, nullptr);
        if (colorButton) {
            QTimer::singleShot(0, dialog, [&] {
                auto* picker = dialog->findChild<QColorDialog*>();
                ASSERT_NE(picker, nullptr);
                picker->setCurrentColor(custom);
                EXPECT_EQ(progressColor(ProgressState::Downloading), custom);
                EXPECT_EQ(loadProgressColors(QSettings()).front(), defaultDownload);
                picker->accept();
            });
            colorButton->click();
        }
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    settingsButton->click();

    EXPECT_EQ(progressColor(ProgressState::Downloading), custom);
    EXPECT_EQ(loadProgressColors(QSettings()).front(), defaultDownload);
    ASSERT_TRUE(options.apply());
    EXPECT_FALSE(options.restartRequired());
    EXPECT_EQ(progressColor(ProgressState::Downloading), custom);
    EXPECT_EQ(loadProgressColors(QSettings()).front(), custom);

    OptionsDialog reopened;
    auto* reopenButton = reopened.findChild<QPushButton*>("progressColorsButton");
    ASSERT_NE(reopenButton, nullptr);
    QTimer::singleShot(0, &reopened, [&] {
        auto* dialog = reopened.findChild<QDialog*>("tguiDialog");
        ASSERT_NE(dialog, nullptr);
        auto* resetButton = dialog->findChild<QPushButton*>("resetProgressColors");
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (resetButton && buttons) {
            resetButton->click();
            buttons->button(QDialogButtonBox::Ok)->click();
        } else {
            ADD_FAILURE() << "Missing progress color dialog controls";
            dialog->reject();
        }
    });
    reopenButton->click();
    ASSERT_TRUE(reopened.apply());
    EXPECT_EQ(progressColor(ProgressState::Downloading), defaultDownload);
    EXPECT_EQ(loadProgressColors(QSettings()), defaults);
}

TEST(OptionsDialog, SaveAndPersist)
{
    isolateTestSettings();
    resetUiKeys();
    OptionsDialog dlg;
    dlg.setLanguage("ru");
    dlg.setScale(125);
    dlg.setRefreshIntervalSec(5);
    dlg.apply();

    QSettings s;
    EXPECT_EQ(s.value("ui/language").toString(), "ru");
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
    // В макете флажок «Показывать окно…» включён по умолчанию (skip = false).
    EXPECT_TRUE(skip->isChecked());
    EXPECT_FALSE(remove->isChecked());
    skip->setChecked(false);
    remove->setChecked(true);
    ASSERT_TRUE(dialog.apply());
    OptionsDialog reopened;
    EXPECT_FALSE(reopened.findChild<QCheckBox*>("skipAddDialog")->isChecked());
    EXPECT_TRUE(reopened.findChild<QCheckBox*>("deleteSourceTorrent")->isChecked());
    settings.remove(OptionsDialog::skipAddDialogKey);
    settings.remove(OptionsDialog::deleteSourceTorrentKey);
}

TEST(OptionsDialog, TabsFollowMockupOrder)
{
    isolateTestSettings();
    OptionsDialog dialog;
    auto* tabs = dialog.findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    ASSERT_GE(tabs->count(), 7);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("General"));
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("Interface"));
    EXPECT_EQ(tabs->tabText(2), QStringLiteral("Sequential download"));
    EXPECT_EQ(tabs->tabText(3), QStringLiteral("Labels"));
    // Дополнительные вкладки сохраняются после основных.
    EXPECT_EQ(tabs->tabText(4), QStringLiteral("Paths"));
    EXPECT_EQ(tabs->tabText(5), QStringLiteral("Speed limits"));
    EXPECT_EQ(tabs->tabText(6), QStringLiteral("Adding torrents"));
}

TEST(OptionsDialog, UpdateCheckSettingsPersist)
{
    isolateTestSettings();
    QSettings settings;
    settings.remove("ui/updateCheckEnabled");
    settings.remove("ui/updateCheckDays");
    OptionsDialog dialog;
    auto* enabled = dialog.findChild<QCheckBox*>("checkUpdates");
    auto* days = dialog.findChild<QSpinBox*>("updateIntervalDays");
    ASSERT_NE(enabled, nullptr);
    ASSERT_NE(days, nullptr);
    EXPECT_TRUE(enabled->isChecked());
    EXPECT_EQ(days->value(), 7);
    EXPECT_TRUE(days->isEnabled());

    enabled->setChecked(false);
    EXPECT_FALSE(days->isEnabled());
    days->setValue(14);
    ASSERT_TRUE(dialog.apply());
    EXPECT_FALSE(settings.value("ui/updateCheckEnabled").toBool());
    EXPECT_EQ(settings.value("ui/updateCheckDays").toInt(), 14);
    settings.remove("ui/updateCheckEnabled");
    settings.remove("ui/updateCheckDays");
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

TEST(SettingsBackup, SkipsPasswordsAndPreservesThemOnlyForMatchingConnections)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.filePath("backup.ini");
    const QVariantMap exported{
        {"connection/url", "https://user:url-secret@example.test"}, {"connection/password", "rpc-secret"},
        {"daemons/names", QStringList{"server"}}, {"daemons/server/host", "example.test"},
        {"daemons/server/password", "profile-secret"}, {"daemons/server/proxyHost", "proxy.test"},
        {"daemons/server/proxyPass", "proxy-secret"},
        {"daemons/server/protectedPassword", QByteArray("protected-rpc")},
        {"daemons/server/protectedProxyPass", QByteArray("protected-proxy")}};
    ASSERT_TRUE(settingsbackup::write(path, exported));
    const auto loaded = settingsbackup::read(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->contains("connection/url"));
    EXPECT_FALSE(loaded->contains("connection/password"));
    EXPECT_FALSE(loaded->contains("daemons/server/password"));
    EXPECT_FALSE(loaded->contains("daemons/server/proxyPass"));
    EXPECT_FALSE(loaded->contains("daemons/server/protectedPassword"));
    EXPECT_FALSE(loaded->contains("daemons/server/protectedProxyPass"));

    QSettings destination(directory.filePath("destination.ini"), QSettings::IniFormat);
    ASSERT_TRUE(settingsbackup::replace(destination, exported));
    EXPECT_FALSE(destination.contains("connection/password"));
    EXPECT_FALSE(destination.contains("daemons/server/password"));
    EXPECT_FALSE(destination.contains("daemons/server/protectedPassword"));
    destination.setValue("daemons/server/protectedPassword", QByteArray("local-rpc"));
    destination.setValue("daemons/server/protectedProxyPass", QByteArray("local-proxy"));
    ASSERT_TRUE(settingsbackup::replace(destination, *loaded));
    EXPECT_EQ(destination.value("daemons/server/protectedPassword").toByteArray(), QByteArray("local-rpc"));
    EXPECT_EQ(destination.value("daemons/server/protectedProxyPass").toByteArray(), QByteArray("local-proxy"));

    auto differentConnection = *loaded;
    differentConnection.insert("connection/url", "https://user@other.test");
    differentConnection.insert("daemons/server/host", "other.test");
    differentConnection.insert("daemons/server/proxyHost", "other-proxy.test");
    ASSERT_TRUE(settingsbackup::replace(destination, differentConnection));
    EXPECT_FALSE(destination.contains("connection/password"));
    EXPECT_FALSE(destination.contains("daemons/server/password"));
    EXPECT_FALSE(destination.contains("daemons/server/proxyPass"));
    EXPECT_FALSE(destination.contains("daemons/server/protectedPassword"));
    EXPECT_FALSE(destination.contains("daemons/server/protectedProxyPass"));
}

TEST(SettingsBackup, RestoresInterruptedImportAndIgnoresUnsupportedTypes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto journal = directory.filePath("recovery.dat");
    const auto backup = directory.filePath("backup.ini");
    const QVariantMap before{{"ui/refreshInterval", 3}};
    QSettings settings(directory.filePath("local.ini"), QSettings::IniFormat);
    ASSERT_TRUE(settingsbackup::writeRecovery(journal, before));
    settings.setValue("ui/refreshInterval", 20);
    ASSERT_TRUE(settingsbackup::restoreInterrupted(settings, journal));
    EXPECT_EQ(settings.value("ui/refreshInterval").toInt(), 3);
    EXPECT_FALSE(QFileInfo::exists(journal));
    ASSERT_TRUE(settingsbackup::write(backup, {{"ui/refreshInterval", 4}, {"ui/size", QSize(1, 2)}}));
    const auto loaded = settingsbackup::read(backup);
    ASSERT_TRUE(loaded);
    EXPECT_FALSE(loaded->contains("ui/size"));
    ASSERT_TRUE(settingsbackup::replace(settings, *loaded, journal));
    EXPECT_EQ(settings.value("ui/refreshInterval").toInt(), 4);
    EXPECT_FALSE(QFileInfo::exists(journal));
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
    auto* editor = options.findChild<QListWidget*>("labelList");
    ASSERT_NE(editor, nullptr);
    editor->clear();
    editor->addItems(QStringList{" Video ", "Music", "Video"});
    options.apply();
    EXPECT_EQ(torrentlabels::configured(), (QStringList{"Video", "Music"}));
    EXPECT_FALSE(options.restartRequired());
    OptionsDialog reopened;
    auto* savedEditor = reopened.findChild<QListWidget*>("labelList");
    ASSERT_NE(savedEditor, nullptr);
    ASSERT_EQ(savedEditor->count(), 2);
    EXPECT_EQ(savedEditor->item(0)->text(), QStringLiteral("Video"));
    EXPECT_EQ(savedEditor->item(1)->text(), QStringLiteral("Music"));

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
    auto* editor = options.findChild<QListWidget*>("labelList");
    auto* restore = options.findChild<QPushButton*>("restoreDefaultLabels");
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(restore, nullptr);
    EXPECT_FALSE(restore->isEnabled());

    editor->clear();
    editor->addItems(QStringList{"Custom", "Music"});
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
