#include "updatechecker.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QVersionNumber>

#include <utility>

#include "tgui/util.h"
#include "theme.h"

namespace {
constexpr int kUpdateCheckWakeMs = 24 * 60 * 60 * 1000;

/** Возвращает имя артефакта, публикуемого для этой платформы. */
QString updatePackage(const QString& version)
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("transmissiongui-%1-windows-x64-setup.exe").arg(version);
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("transmissiongui-%1-Darwin.dmg").arg(version);
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("transmissiongui_%1_amd64.deb").arg(version);
#else
    Q_UNUSED(version);
    return {};
#endif
}

/** Ищет единственную корректную запись для точного имени пакета в SHA256SUMS.txt. */
QByteArray packageChecksum(const QByteArray& sums, const QString& package)
{
    static const QRegularExpression entry(QStringLiteral(R"(^([0-9a-fA-F]{64}) [ *](.+)$)"));
    QByteArray checksum;
    for (const auto& line : sums.split('\n')) {
        const auto match = entry.match(QString::fromUtf8(line.endsWith('\r') ? line.chopped(1) : line));
        if (match.hasMatch() && match.captured(2) == package) {
            if (!checksum.isEmpty())
                return {};
            checksum = QByteArray::fromHex(match.captured(1).toLatin1());
        }
    }
    return checksum;
}

#ifdef Q_OS_WIN
/** Запускает установщик после освобождения файлов текущим процессом. */
bool launchAfterExit(const QString& package)
{
    // В PowerShell типографские кавычки также распознаются как разделители строк.
    // Путь кодируем отдельно, чтобы он всегда оставался данными.
    const auto encode = [](const QString& value) {
        return QByteArray(reinterpret_cast<const char*>(value.utf16()), value.size() * 2).toBase64();
    };
    const auto script = QStringLiteral(
        "$package = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String('%1')); "
        "$app = Get-Process -Id %2 -ErrorAction SilentlyContinue; "
        "if ($app) { $app.WaitForExit() }; Start-Process -FilePath $package")
        .arg(QString::fromLatin1(encode(QDir::toNativeSeparators(package)))).arg(QCoreApplication::applicationPid());
    const auto shell = QDir(qEnvironmentVariable("SystemRoot"))
        .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    return QFile::exists(shell) && QProcess::startDetached(shell,
        {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-WindowStyle"),
         QStringLiteral("Hidden"), QStringLiteral("-EncodedCommand"), QString::fromLatin1(encode(script))});
}
#endif
}

UpdateChecker::UpdateChecker(QMainWindow& window, Notify notify)
    : m_window(window), m_notify(std::move(notify))
{
    m_updateCheckTimer.setObjectName(QStringLiteral("updateCheckTimer"));
    m_updateCheckTimer.setSingleShot(true);
    connect(&m_updateCheckTimer, &QTimer::timeout, this, [this] {
        if (m_updateTimerIsWake)
            updateCheckSchedule();
        else
            checkUpdate(false);
    });
}

void UpdateChecker::suspend()
{
    m_settingsImported = true;
    m_updateCheckTimer.stop();
}

/** Перепланирует фоновую проверку по времени последней попытки и успешного ответа. */
void UpdateChecker::updateCheckSchedule()
{
    const QSettings settings;
    m_updateCheckTimer.stop();
    if (m_settingsImported || m_checkingUpdate ||
        !settings.value("ui/updateCheckEnabled", true).toBool())
        return;

    constexpr qint64 hourMs = 60 * 60 * 1000;
    const auto now = QDateTime::currentDateTimeUtc();
    const auto success = settings.value("ui/lastUpdateCheck").toDateTime();
    const auto attempt = settings.value("ui/lastUpdateAttempt").toDateTime();
    const auto days = qBound(1, settings.value("ui/updateCheckDays", 7).toInt(), 365);
    const auto scheduled = success.isValid() ? qMin(success, now).addDays(days) : now;
    const auto retry = attempt.isValid() ? qMin(attempt, now).addMSecs(hourMs) : now;
    const auto delay = qBound(qint64(15000), now.msecsTo(qMax(scheduled, retry)), qint64(kUpdateCheckWakeMs));
    m_updateTimerIsWake = delay == kUpdateCheckWakeMs;
    m_updateCheckTimer.start(static_cast<int>(delay));
}

/** Запрашивает выпуск, объединяя ручную и фоновую проверку при совпадении по времени. */
void UpdateChecker::checkUpdate(bool requestedManually)
{
    if (m_downloadingUpdate)
        return;
    if (!requestedManually && (m_settingsImported || !QSettings().value("ui/updateCheckEnabled", true).toBool()))
        return;
    if (requestedManually) {
        m_manualUpdateCheck = true;
        m_window.statusBar()->showMessage(tr("Checking for updates…"));
    }
    if (m_checkingUpdate)
        return;
    m_checkingUpdate = true;
    m_updateCheckTimer.stop();
    QSettings().setValue("ui/lastUpdateAttempt", QDateTime::currentDateTimeUtc());
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/don10077/transmissionGUI/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "transmissionGUI");
    request.setTransferTimeout(10000);
    auto* reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_checkingUpdate = false;
        const bool manual = std::exchange(m_manualUpdateCheck, false);
        if (manual)
            m_window.statusBar()->clearMessage();
        reply->deleteLater();
        if (m_settingsImported)
            return;
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto title = tr("Check for updates");
        if (http == 404) {
            QSettings().setValue("ui/lastUpdateCheck", QDateTime::currentDateTimeUtc());
            updateCheckSchedule();
            if (manual)
                QMessageBox::information(&m_window, title, tr("No published release was found."));
            return;
        }
        updateCheckSchedule();
        if (reply->error() != QNetworkReply::NoError) {
            if (manual)
                QMessageBox::warning(&m_window, title, tr("Could not check for updates: %1").arg(reply->errorString()));
            return;
        }
        const auto document = QJsonDocument::fromJson(reply->readAll());
        const auto tag = document.object().value("tag_name").toString();
        const auto latest = tgui::parseLatestRelease(document);
        if (!latest) {
            if (manual)
                QMessageBox::warning(&m_window, title, tr("Could not read the published version."));
            return;
        }
        QSettings settings;
        settings.setValue("ui/lastUpdateCheck", QDateTime::currentDateTimeUtc());
        updateCheckSchedule();
        const auto current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        if (latest->normalized() <= current.normalized()) {
            if (manual)
                QMessageBox::information(&m_window, title, tr("You have the latest version."));
            return;
        }
        if (!manual) {
            const auto message = tr("Version %1 is available.").arg(latest->toString());
            m_notify(title, message);
            return;
        }
        QMessageBox box(QMessageBox::Information, title, tr("Version %1 is available.").arg(latest->toString()),
                        QMessageBox::Close, &m_window);
        auto* open = box.addButton(tr("Open release"), QMessageBox::AcceptRole);
        const auto package = updatePackage(latest->toString());
        bool hasPackage = false;
        bool hasChecksums = false;
        const auto assets = document.object().value("assets").toArray();
        for (const auto& asset : assets) {
            const auto name = asset.toObject().value("name").toString();
            hasPackage |= !package.isEmpty() && name == package;
            hasChecksums |= name == QLatin1String("SHA256SUMS.txt");
        }
        QAbstractButton* download = nullptr;
        if (hasPackage && hasChecksums) {
#ifdef Q_OS_WIN
            download = box.addButton(tr("Download and install"), QMessageBox::ActionRole);
#else
            download = box.addButton(tr("Download and open"), QMessageBox::ActionRole);
#endif
        }
        tgui::theme::fitMessageBoxButtons(box);
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl::fromEncoded(
                "https://github.com/don10077/transmissionGUI/releases/tag/" + QUrl::toPercentEncoding(tag)));
        else if (download && box.clickedButton() == download)
            downloadUpdate("https://github.com/don10077/transmissionGUI/releases/download/" +
                               QUrl::toPercentEncoding(tag) + '/', package);
    });
}

void UpdateChecker::downloadUpdate(const QByteArray& releasePath, const QString& package)
{
    const QScopedValueRollback downloading(m_downloadingUpdate, true);
    const auto title = tr("Check for updates");
    QProgressDialog progress(tr("Downloading update…"), tr("Cancel"), 0, 0, &m_window);
    progress.setWindowTitle(title);
    progress.setWindowModality(Qt::WindowModal);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    const auto transfer = [&](const QString& name, QIODevice& output, qint64 limit) {
        QNetworkRequest request(QUrl::fromEncoded(releasePath + QUrl::toPercentEncoding(name)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(30000);
        auto* reply = m_nam.get(request);
        QEventLoop loop;
        qint64 written = 0;
        bool writeFailed = false;
        const auto read = [&] {
            const auto bytes = reply->readAll();
            if (bytes.size() > limit - written || output.write(bytes) != bytes.size()) {
                writeFailed = true;
                reply->abort();
            } else {
                written += bytes.size();
            }
        };
        connect(reply, &QNetworkReply::readyRead, &loop, read);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&progress, &QProgressDialog::canceled, &loop, [reply] { reply->abort(); });
        if (name == package) {
            connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 received, qint64 total) {
                if (total > 0 && total <= limit) {
                    progress.setMaximum(static_cast<int>(total));
                    progress.setValue(static_cast<int>(received));
                }
            });
        }
        if (!reply->isFinished())
            loop.exec();
        if (!reply->isFinished())
            reply->abort();
        if (reply->error() == QNetworkReply::NoError && !writeFailed)
            read();
        const bool ok = !writeFailed && reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        return ok && !progress.wasCanceled();
    };
    const auto fail = [&](const QString& message) {
        progress.hide();
        if (!progress.wasCanceled())
            QMessageBox::warning(&m_window, title, message);
    };

    QBuffer sums;
    sums.open(QIODevice::WriteOnly);
    if (!transfer(QStringLiteral("SHA256SUMS.txt"), sums, 128 * 1024))
        return fail(tr("Could not download the update checksum."));
    const auto expected = packageChecksum(sums.data(), package);
    if (expected.isEmpty())
        return fail(tr("The release has no valid checksum for this package."));

    const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(downloads);
    QTemporaryDir directory(QDir(downloads).filePath(QStringLiteral("TransmissionGUI-update-XXXXXX")));
    if (!directory.isValid())
        return fail(tr("Could not save the update package."));
    const auto path = directory.filePath(package);
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return fail(tr("Could not save the update package."));
    if (!transfer(package, file, 512 * 1024 * 1024))
        return fail(tr("Could not download the update package."));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.flush() || !file.seek(0) || !hash.addData(&file) || hash.result() != expected)
        return fail(tr("The update package did not pass SHA-256 verification."));
    file.close();
    progress.hide();
    directory.setAutoRemove(false);
#ifdef Q_OS_WIN
    if (launchAfterExit(path)) {
        QCoreApplication::quit();
        return;
    }
#else
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return;
#endif
    fail(tr("Could not open the update package. It was saved to %1.").arg(QDir::toNativeSeparators(path)));
}
