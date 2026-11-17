#include "flagimageloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrentRun>

#include <utility>

#include "detailmodels.h"
#include "flagarchive.h"

FlagImageLoader::FlagImageLoader(PeerListModel& peers, std::function<void(const QString&)> notify)
    : m_peerModel(peers), m_notify(std::move(notify))
{
}

/** Находит готовые флаги либо запускает загрузку и распаковку. */
void FlagImageLoader::loadFlagImages(bool allowDownload, bool reportFailure)
{
    m_reportFlagFailure |= reportFailure;
    if (m_loadingFlags)
        return;
    m_loadingFlags = true;
    const auto appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDir.isEmpty()) {
        finishFlagImages();
        return;
    }
    for (const auto& dir : {appDir + "/country-flags", appDir + "/flags"}) {
        if (!QDir(dir).entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty()) {
            finishFlagImages(dir);
            return;
        }
    }
    if (QFileInfo(appDir + "/flags.zip").isFile())
        unpackFlagImages(appDir, {}, allowDownload);
    else if (allowDownload)
        downloadFlagImages(appDir);
    else
        finishFlagImages();
}

/** Загружает ограниченный по размеру архив асинхронно. */
void FlagImageLoader::downloadFlagImages(const QString& appDir)
{
    QNetworkRequest request{QUrl{"https://raw.githubusercontent.com/transmission-remote-gui/transgui/master/flags.zip"}};
    request.setTransferTimeout(15000);
    auto* reply = m_nam.get(request);
    reply->setReadBufferSize(flagarchive::maxArchiveBytes + 1);
    connect(reply, &QNetworkReply::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > flagarchive::maxArchiveBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, appDir] {
        reply->deleteLater();
        const auto zip = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
        if (zip.isEmpty() || zip.size() > flagarchive::maxArchiveBytes)
            finishFlagImages();
        else
            unpackFlagImages(appDir, zip, false);
    });
}

/** Читает старый ZIP-кэш и распаковывает флаги вне потока интерфейса. */
void FlagImageLoader::unpackFlagImages(const QString& appDir, const QByteArray& zip, bool retryDownload)
{
    const auto directory = appDir + "/country-flags";
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, directory, appDir, retryDownload, cached = zip.isEmpty()] {
        const bool extracted = watcher->result();
        watcher->deleteLater();
        if (extracted)
            finishFlagImages(directory);
        else if (cached && (retryDownload || m_reportFlagFailure))
            downloadFlagImages(appDir);
        else
            finishFlagImages();
    });
    watcher->setFuture(QtConcurrent::run([appDir, zip, directory] {
        if (!zip.isEmpty())
            return flagarchive::extract(zip, directory);
        QFile cached(appDir + "/flags.zip");
        return cached.open(QIODevice::ReadOnly) &&
            flagarchive::extract(cached.read(flagarchive::maxArchiveBytes + 1), directory);
    }));
}

/** Применяет готовый каталог или сообщает об ошибке ручной загрузки. */
void FlagImageLoader::finishFlagImages(const QString& directory)
{
    m_loadingFlags = false;
    const bool reportFailure = std::exchange(m_reportFlagFailure, false);
    if (!directory.isEmpty())
        m_peerModel.setFlagsDirectory(directory);
    else if (reportFailure && !m_settingsImported && QSettings().value("peers/showFlag").toBool()) {
        QSettings().setValue("peers/showFlag", false);
        m_peerModel.setShowFlags(false);
        m_notify(tr("Unable to load country flag images."));
    }
}
