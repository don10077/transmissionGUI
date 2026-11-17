#include "updatechecker.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QVersionNumber>

#include <utility>

#include "tgui/util.h"

namespace {
constexpr int kUpdateCheckWakeMs = 24 * 60 * 60 * 1000;
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
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl::fromEncoded(
                "https://github.com/don10077/transmissionGUI/releases/tag/" + QUrl::toPercentEncoding(tag)));
    });
}
