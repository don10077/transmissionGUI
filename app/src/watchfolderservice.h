#pragma once

#include <QDeadlineTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QFutureWatcher>
#include <QTimer>

#include <functional>
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

    struct Candidate {
        QString folder;
        QString name;
        QString version;
    };
    /** Устанавливает проверку готовности очереди к приёму файла. */
    void setCanAccept(std::function<bool()> canAccept);
    /** Перечитывает настройку каталога и обновляет наблюдение. */
    void updateWatchFolder(bool connected);
    /** Останавливает наблюдение и при смене сессии сбрасывает список файлов. */
    void stop(bool clearCandidates = false);
    /** Фиксирует версию файла на момент отправки демону. */
    Candidate candidate(const QString& path) const;
    /** Сохраняет результат отправки и устанавливает срок повтора при ошибке. */
    void finishWatch(const Candidate& file, bool completed, bool invalid);
    /** Меняет наблюдаемый каталог. Пустой путь отключает наблюдение. */
    void setDirectory(const QString& path);
    /** Запрашивает снимок после затихания файловых событий. */
    void scheduleScan();

signals:
    void snapshotReady(const Snapshot& files);
    void fileReady(const QString& path);

private:
    void applyWatchSnapshot(const Snapshot& files);
    void scan();
    void applyScan();
    static std::optional<Snapshot> readDirectory(const QString& path);

    struct WatchRetry {
        QString version;
        QDeadlineTimer due;
    };
    QString m_watchFolder;
    QHash<QString, QString> m_watchCandidates;
    QHash<QString, WatchRetry> m_watchRetries;
    std::function<bool()> m_canAccept;
    QString m_path;
    quint64 m_generation = 0;
    quint64 m_scanGeneration = 0;
    bool m_pending = false;
    QFileSystemWatcher m_events;
    QTimer m_debounce;
    QTimer m_fallback;
    QFutureWatcher<std::optional<Snapshot>> m_scan;
};
