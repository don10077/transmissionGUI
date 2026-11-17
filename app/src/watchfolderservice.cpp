#include "watchfolderservice.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>
#include <QtConcurrentRun>

#include <utility>

#include "optionsdialog.h"

namespace {
constexpr int kDebounceMs = 500;          // дебаунс событий файловой системы
constexpr int kFallbackRetryMs = 30000;   // резервный период сканирования
}

WatchFolderService::WatchFolderService()
{
    connect(this, &WatchFolderService::snapshotReady, this, &WatchFolderService::applyWatchSnapshot);
    m_debounce.setInterval(kDebounceMs);
    m_debounce.setSingleShot(true);
    m_fallback.setInterval(kFallbackRetryMs);
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

/** Применяет настройку наблюдения при подключении или сохранении параметров. */
void WatchFolderService::updateWatchFolder(bool connected)
{
    const auto path = QSettings().value(OptionsDialog::watchFolderKey).toString();
    const auto folder = path.isEmpty() ? QString() : QDir(path).absolutePath();
    if (m_watchFolder != folder) {
        m_watchFolder = folder;
        m_watchCandidates.clear();
        m_watchRetries.clear();
    }
    setDirectory(connected ? m_watchFolder : QString{});
}

/** Ключ журнала обработанных версий файлов в заданной папке. */
static QString watchCompletedKey(const QString& folder)
{
    return QStringLiteral("downloads/watchHistory/") +
        QString::fromLatin1(QUrl::toPercentEncoding(folder));
}

/** Передаёт в очередь один стабильный файл, ожидающий добавления. */
void WatchFolderService::applyWatchSnapshot(const Snapshot& files)
{
    if (!m_canAccept || !m_canAccept() || m_watchFolder.isEmpty())
        return;
    QSettings settings;
    const auto historyKey = watchCompletedKey(m_watchFolder);
    auto history = settings.value(historyKey).toMap();
    const auto previousHistory = history;
    QHash<QString, QString> candidates;
    QString selected;
    bool needsStableCheck = false;
    for (const auto& file : files) {
        const auto& name = file.name;
        const auto& stamp = file.version;
        candidates.insert(name, stamp);
        if (!file.readable || history.value(name).toString() == stamp)
            continue;
        const auto retry = m_watchRetries.constFind(name);
        if (retry != m_watchRetries.constEnd() && retry->version == stamp && !retry->due.hasExpired())
            continue;
        if (m_watchCandidates.value(name) != stamp)
            needsStableCheck = true;
        else if (selected.isEmpty())
            selected = name;
    }
    if (needsStableCheck)
        scheduleScan();
    m_watchCandidates = std::move(candidates);
    for (auto it = m_watchRetries.begin(); it != m_watchRetries.end();) {
        if (m_watchCandidates.value(it.key()) != it->version)
            it = m_watchRetries.erase(it);
        else
            ++it;
    }
    for (auto it = history.begin(); it != history.end();) {
        if (!m_watchCandidates.contains(it.key()))
            it = history.erase(it);
        else
            ++it;
    }
    if (history != previousHistory)
        settings.setValue(historyKey, history);
    if (!selected.isEmpty())
        emit fileReady(QDir(m_watchFolder).filePath(selected));
}

void WatchFolderService::setCanAccept(std::function<bool()> canAccept)
{
    m_canAccept = std::move(canAccept);
}

void WatchFolderService::stop(bool clearCandidates)
{
    setDirectory({});
    if (clearCandidates)
        m_watchCandidates.clear();
}

WatchFolderService::Candidate WatchFolderService::candidate(const QString& path) const
{
    const auto name = QFileInfo(path).fileName();
    return {m_watchFolder, name, m_watchCandidates.value(name)};
}

void WatchFolderService::finishWatch(const Candidate& file, bool completed, bool invalid)
{
    if (file.version.isEmpty())
        return;
    if (completed) {
        QSettings settings;
        const auto key = watchCompletedKey(file.folder);
        auto history = settings.value(key).toMap();
        history.insert(file.name, file.version);
        settings.setValue(key, history);
    }
    if (file.folder != m_watchFolder)
        return;
    if (completed)
        m_watchRetries.remove(file.name);
    else
        m_watchRetries.insert(file.name, {file.version, invalid ? QDeadlineTimer(QDeadlineTimer::Forever)
                                                                  : QDeadlineTimer(30000)});
}
