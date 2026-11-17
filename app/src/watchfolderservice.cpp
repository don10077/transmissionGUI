#include "watchfolderservice.h"

#include <QDateTime>
#include <QDir>
#include <QtConcurrentRun>

#include <utility>

WatchFolderService::WatchFolderService()
{
    m_debounce.setInterval(500);
    m_debounce.setSingleShot(true);
    m_fallback.setInterval(30000);
    connect(&m_debounce, &QTimer::timeout, this, &WatchFolderService::scan);
    connect(&m_fallback, &QTimer::timeout, this, &WatchFolderService::scheduleScan);
    connect(&m_events, &QFileSystemWatcher::directoryChanged, this, &WatchFolderService::scheduleScan);
    connect(&m_scan, &QFutureWatcherBase::finished, this, &WatchFolderService::applyScan);
}

void WatchFolderService::setDirectory(const QString& path)
{
    if (m_path == path)
        return;
    ++m_generation;
    m_path = path;
    m_pending = false;
    m_debounce.stop();
    m_fallback.stop();
    const auto watched = m_events.directories();
    if (!watched.isEmpty())
        m_events.removePaths(watched);
    if (!m_path.isEmpty()) {
        m_fallback.start();
        scheduleScan();
    }
}

void WatchFolderService::scheduleScan()
{
    if (!m_path.isEmpty())
        m_debounce.start();
}

void WatchFolderService::scan()
{
    if (m_scanGeneration != 0) {
        m_pending = true;
        return;
    }
    m_scanGeneration = m_generation;
    m_scan.setFuture(QtConcurrent::run(&WatchFolderService::readDirectory, m_path));
}

void WatchFolderService::applyScan()
{
    const bool current = m_scanGeneration == m_generation;
    m_scanGeneration = 0;
    if (current) {
        if (const auto snapshot = m_scan.result()) {
            // Повторный снимок закрывает промежуток между чтением и подпиской.
            if (!m_events.directories().contains(m_path) && m_events.addPath(m_path))
                scheduleScan();
            emit snapshotReady(*snapshot);
        }
    }
    if (std::exchange(m_pending, false))
        scheduleScan();
}

/** Читает метаданные вне GUI. Недоступный каталог не считается пустым. */
std::optional<WatchFolderService::Snapshot> WatchFolderService::readDirectory(const QString& path)
{
    const QDir directory(path);
    if (!directory.exists() || !directory.isReadable())
        return std::nullopt;
    const auto entries = directory.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    if (entries.isEmpty() && (!directory.exists() || !directory.isReadable()))
        return std::nullopt;
    Snapshot files;
    files.reserve(entries.size());
    for (const auto& file : entries) {
        const auto name = file.fileName();
        if (name.endsWith(".torrent", Qt::CaseInsensitive)) {
            const auto version = QString::number(file.size()) + '/' +
                QString::number(file.lastModified().toMSecsSinceEpoch());
            files.append({name, version, file.size() > 0 && file.isReadable()});
        }
    }
    return files;
}
