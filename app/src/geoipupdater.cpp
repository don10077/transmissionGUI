#include "geoipupdater.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QtConcurrentRun>

#include "boundeddownload.h"
#include "detailmodels.h"
#include "flagimageloader.h"
#include "tgui/util.h"

GeoIpUpdater::GeoIpUpdater(QMainWindow& window, PeerListModel& peers, FlagImageLoader& flags)
    : m_window(window), m_peerModel(peers), m_flags(flags)
{
}

void GeoIpUpdater::initialize()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/GeoIP.dat");
    if (QFileInfo::exists(path))
        m_geoip.load(path.toStdString());
    m_peerModel.setGeoIp(&m_geoip);
    m_peerModel.setResolveHost(QSettings().value("peers/resolveHost", false).toBool());
    const bool showFlags = QSettings().value("peers/showFlag", false).toBool();
    if (showFlags)
        m_flags.loadFlagImages(isLoaded());
    m_peerModel.setShowFlags(showFlags);
}

void GeoIpUpdater::onUpdateGeoIp()
{
    if (m_geoIpDownloading)
        return;
    m_geoIpDownloading = true;
    const auto answer = QMessageBox::question(&m_window, tr("Update GeoIP database"),
        tr("GeoIP database is needed to resolve country by IP address.\n"
           "Download this database now?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        m_geoIpDownloading = false;
        return;
    }

    constexpr qint64 limit = 64LL * 1024 * 1024;
    constexpr int timeout = 30000;
    QNetworkRequest request{QUrl{"https://dl.miyuru.lk/geoip/maxmind/country/maxmind4.dat.gz"}};
    request.setTransferTimeout(timeout);
    request.setMaximumRedirectsAllowed(5);
    auto* reply = m_nam.get(request);
    boundDownload(reply, limit, timeout);
    auto* progress = new QProgressDialog(tr("Downloading GeoIP database…"), tr("Cancel"), 0, 100, &m_window);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);
    connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 received, qint64 total) {
        if (total > 0)
            progress->setValue(static_cast<int>(received * 100 / total));
    });
    connect(progress, &QProgressDialog::canceled, reply, [reply] {
        reply->setProperty("userCancelled", true);
        reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, progress, limit] {
        reply->deleteLater();
        progress->hide();
        progress->deleteLater();
        if (reply->error() != QNetworkReply::NoError || reply->bytesAvailable() > limit) {
            m_geoIpDownloading = false;
            if (!reply->property("userCancelled").toBool()) {
                QMessageBox::warning(&m_window, tr("Update GeoIP database"),
                    tr("Download failed: %1").arg(reply->errorString()));
            }
            return;
        }
        const auto appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        enum class Failure { None, Decompression, InvalidDatabase, Save };
        using Result = std::pair<Failure, tgui::GeoIpDb>;
        auto* watcher = new QFutureWatcher<Result>(this);
        connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
            auto [error, database] = watcher->future().takeResult();
            watcher->deleteLater();
            m_geoIpDownloading = false;
            if (error != Failure::None) {
                const auto message = error == Failure::Decompression ? tr("Decompression failed.") :
                    error == Failure::InvalidDatabase ? tr("Database is invalid.") : tr("Save failed.");
                QMessageBox::warning(&m_window, tr("Update GeoIP database"), message);
                return;
            }
            m_geoip = std::move(database);
            if (QSettings().value("peers/showFlag", false).toBool())
                m_flags.loadFlagImages(true);
            const auto currentPeers = m_peerModel.peers();
            if (!currentPeers.isEmpty())
                m_peerModel.setPeers(currentPeers);
            m_window.statusBar()->showMessage(tr("GeoIP database loaded."), 5000);
        });
        watcher->setFuture(QtConcurrent::run([raw = reply->readAll(), appDir, limit]() mutable -> Result {
            if (raw.startsWith("\x1f\x8b")) {
                auto decoded = tgui::gunzipData(raw, limit);
                if (!decoded)
                    return {Failure::Decompression, {}};
                raw = std::move(*decoded);
            }
            tgui::GeoIpDb database;
            std::vector<unsigned char> bytes(raw.cbegin(), raw.cend());
            if (!database.load(std::move(bytes)))
                return {Failure::InvalidDatabase, {}};
            if (appDir.isEmpty() || !QDir().mkpath(appDir))
                return {Failure::Save, {}};
            QSaveFile file(QDir(appDir).filePath("GeoIP.dat"));
            if (!file.open(QIODevice::WriteOnly) || file.write(raw) != raw.size() || !file.commit())
                return {Failure::Save, {}};
            return {Failure::None, std::move(database)};
        }));
    });
}
