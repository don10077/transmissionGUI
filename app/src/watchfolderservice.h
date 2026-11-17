#pragma once

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>

#include <optional>

/** Наблюдает за каталогом и отдаёт снимки из фонового потока в GUI. */
class WatchFolderService final : public QObject {
    Q_OBJECT
public:
    struct File {
        QString name;
        QString version;
        bool readable;
    };
    using Snapshot = QList<File>;

    WatchFolderService();
    /** Меняет наблюдаемый каталог. Пустой путь отключает наблюдение. */
    void setDirectory(const QString& path);
    /** Запрашивает снимок после затихания файловых событий. */
    void scheduleScan();

signals:
    void snapshotReady(const Snapshot& files);

private:
    void scan();
    void applyScan();
    static std::optional<Snapshot> readDirectory(const QString& path);

    QString m_path;
    quint64 m_generation = 0;
    quint64 m_scanGeneration = 0;
    bool m_pending = false;
    QFileSystemWatcher m_events;
    QTimer m_debounce;
    QTimer m_fallback;
    QFutureWatcher<std::optional<Snapshot>> m_scan;
};
