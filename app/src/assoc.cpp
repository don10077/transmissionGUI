#include "assoc.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

QString defaultClassesRoot()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes");
}

QString defaultSoftwareRoot()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software");
}

namespace {

QString handlerCommand()
{
    const QSettings setup(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\transmissionGUI",
        QSettings::NativeFormat);
    const auto location = setup.value("InstallLocation").toString();
    const QDir installed(location);
    const auto installedExe = installed.filePath(QStringLiteral("TransmissionGUI.exe"));
    const bool useInstalled = !location.isEmpty() && QFileInfo(installedExe).isFile();
    const auto executable = useInstalled ? installedExe : QCoreApplication::applicationFilePath();
    return QStringLiteral("\"%1\" -- \"%2\"")
        .arg(QDir::toNativeSeparators(executable), QStringLiteral("%1"));
}

} // namespace

bool torrentHandlerRegistered(const QString& classesRoot)
{
    QSettings classes(classesRoot, QSettings::NativeFormat);
    return classes.contains(QStringLiteral("transmissionGUI.Torrent/shell/open/command/Default")) &&
           classes.contains(QStringLiteral(".torrent/OpenWithProgids/transmissionGUI.Torrent"));
}

bool magnetHandlerRegistered(const QString& classesRoot)
{
    QSettings classes(classesRoot, QSettings::NativeFormat);
    return classes.contains(QStringLiteral("transmissionGUI.Magnet/shell/open/command/Default")) &&
           classes.contains(QStringLiteral("transmissionGUI.Magnet/URL Protocol"));
}

bool applyTorrentHandlers(bool torrent, bool magnet, const QString& classesRoot,
                          const QString& softwareRoot)
{
    QSettings classes(classesRoot, QSettings::NativeFormat);
    const auto command = handlerCommand();
    if (torrent) {
        classes.setValue("transmissionGUI.Torrent/Default", "Torrent file");
        classes.setValue("transmissionGUI.Torrent/shell/open/command/Default", command);
        classes.setValue(".torrent/OpenWithProgids/transmissionGUI.Torrent", QStringLiteral(""));
    } else {
        classes.remove("transmissionGUI.Torrent");
        classes.remove(".torrent/OpenWithProgids/transmissionGUI.Torrent");
    }
    if (magnet) {
        classes.setValue("transmissionGUI.Magnet/Default", "Magnet link");
        classes.setValue("transmissionGUI.Magnet/URL Protocol", QStringLiteral(""));
        classes.setValue("transmissionGUI.Magnet/shell/open/command/Default", command);
    } else {
        classes.remove("transmissionGUI.Magnet");
    }
    classes.sync();
    if (classes.status() != QSettings::NoError)
        return false;

    QSettings software(softwareRoot, QSettings::NativeFormat);
    software.setValue("transmissionGUI/Capabilities/ApplicationName", QStringLiteral(TGUI_DISPLAY_NAME));
    software.setValue("transmissionGUI/Capabilities/ApplicationDescription",
                      "Manage torrents on a Transmission server");
    if (torrent)
        software.setValue("transmissionGUI/Capabilities/FileAssociations/.torrent",
                          "transmissionGUI.Torrent");
    else
        software.remove("transmissionGUI/Capabilities/FileAssociations/.torrent");
    if (magnet)
        software.setValue("transmissionGUI/Capabilities/UrlAssociations/magnet",
                          "transmissionGUI.Magnet");
    else
        software.remove("transmissionGUI/Capabilities/UrlAssociations/magnet");
    if (torrent || magnet)
        software.setValue("RegisteredApplications/transmissionGUI",
                          "Software\\transmissionGUI\\Capabilities");
    else
        software.remove("RegisteredApplications/transmissionGUI");
    software.sync();
    if (software.status() != QSettings::NoError)
        return false;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}
#endif // Q_OS_WIN