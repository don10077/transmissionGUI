#pragma once

#include <QAbstractTableModel>
#include <QStandardItemModel>
#include <QIcon>
#include <QHash>
#include <QSet>

#include <functional>

#include "tgui/models.h"
#include "tgui/geoip.h"

// Модель вкладки «Файлы»: имя, размер, прогресс, приоритет, want/skip.
// Колонка «Включён» редактируется (setData) → сигнал wantChanged.
class FileListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColName = 0,
        ColSize,
        ColProgress,
        ColPriority,
        ColWanted,
        ColumnCount
    };

    explicit FileListModel(QObject* parent = nullptr);

    void setFiles(const QVector<tgui::FileStat>& files);
    const QVector<tgui::FileStat>& files() const { return m_files; }
    bool wantedAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

signals:
    // Изменён want/skip файла (строка в модели, новое значение).
    void wantChanged(int fileIndex, bool wanted);

private:
    QVector<tgui::FileStat> m_files;
};

// Модель вкладки «Пиры» (только чтение).
class PeerListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColAddress = 0,
        ColCountry,
        ColClient,
        ColProgress,
        ColRateDown,
        ColRateUp,
        ColFlags,
        ColPort,
        ColumnCount
    };

    explicit PeerListModel(QObject* parent = nullptr);
    enum Role { ProgressRole = Qt::UserRole + 1 };

    void setPeers(const QVector<tgui::Peer>& peers);
    const QVector<tgui::Peer>& peers() const { return m_peers; }

    // Установить GeoIP-базу для разрешения страны (nullptr — без страны).
    void setGeoIp(const tgui::GeoIpDb* geoip) { m_geoip = geoip; }
    const tgui::GeoIpDb* geoIp() const { return m_geoip; }
    void setShowFlags(bool show);
    bool showFlags() const { return m_showFlags; }
    void setFlagsDirectory(const QString& directory);

    // W11: Resolve Host — асинхронный reverse DNS (IP → hostname) с кешем и обработкой ошибок.
    void setResolveHost(bool on);
    bool resolveHost() const { return m_resolveHost; }
    // TST-02: внедряемый резолвер (ip → колбэк с hostname, "" — ошибка/отсутствие).
    // По умолчанию — QHostInfo::lookupHost; в тестах — детерминированный фейк.
    using Resolver = std::function<void(const QString& ip, std::function<void(QString host)>)>;
    void setResolver(Resolver resolver);
    const Resolver& resolver() const { return m_resolver; }
    // Записать результат reverse-DNS в кеш (host=="" — ошибка/отсутствие) и обновить колонку «Адрес».
    void setResolvedHost(const QString& ip, const QString& host);
    /** Проверяет, закеширован ли результат для @p ip, включая отсутствие имени. */
    bool hasHostLookupResult(const QString& ip) const { return m_hostCache.contains(ip); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<tgui::Peer> m_peers;
    const tgui::GeoIpDb* m_geoip = nullptr;
    bool m_showFlags = false;
    QString m_flagsDirectory;
    mutable QHash<QString, QIcon> m_flagCache;
    bool m_resolveHost = false;
    Resolver m_resolver;
    QHash<QString, QString> m_hostCache;   // ip → hostname ("" — ошибка/отсутствие); кеш результатов
    QSet<QString> m_pendingLookups;        // ip, по которым reverse-DNS ещё выполняется
    void startReverseLookups();
};

// Модель вкладки «Трекеры» (только чтение).
class TrackerListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColAnnounce = 0,
        ColTier,
        ColSeeders,
        ColLeechers,
        ColLastResult,
        ColUpdate,
        ColumnCount
    };

    explicit TrackerListModel(QObject* parent = nullptr);

    void setTrackers(const QVector<tgui::TrackerStat>& trackers);
    const QVector<tgui::TrackerStat>& trackers() const { return m_trackers; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<tgui::TrackerStat> m_trackers;
};
/** File hierarchy with aggregated folder sizes and stable items on refresh. */
class FileTreeModel : public QStandardItemModel {
    Q_OBJECT
public:
    enum Column { Want, Name, Size, Done, Progress, Priority, ColumnCount };
    enum Role { SourceRowsRole = Qt::UserRole + 31, PathRole };
    explicit FileTreeModel(QObject* parent = nullptr);
    void setFiles(const QVector<tgui::FileStat>& files);
    QVector<int> sourceRows(const QModelIndex& index) const;
    // Переключить Want-чекбокс (файл или папку) с каскадом и пересчётом
    // состояния родителей; результат — сигнал wantToggled (source-строки).
    void toggleWant(const QModelIndex& index);
    bool isFolder(const QModelIndex& index) const { return hasChildren(index); }
    Qt::CheckState wantState(const QModelIndex& index) const
    {
        return static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
    }

signals:
    // Состояние Want изменено: rows — индексы исходных файлов, wanted — новое состояние.
    void wantToggled(const QVector<int>& rows, bool wanted);

private:
    void setDescendants(QStandardItem* node, Qt::CheckState state);
    void refreshAncestors(QStandardItem* node);
    struct Aggregate {
        double size = 0;
        double done = 0;
        double checkDone = 0;
        double checkSize = 0;
        int wanted = 0;
        int count = 0;
        int minPriority = 1;
        int maxPriority = -2;
        bool operator==(const Aggregate&) const = default;
        void add(const Aggregate& other);
    };
    struct Node {
        QStandardItem* item;
        int parent;
        QVector<int> rows;
        Aggregate previous;
    };
    /** Собирает исходные строки только по запросу для выбранного узла. */
    void collectRows(const QStandardItem* item, QVector<int>& rows) const;
    QStringList m_paths;
    QVector<Node> m_nodes;
    QVector<int> m_fileNodes;
};
