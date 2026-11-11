#pragma once

#include <QAbstractTableModel>
#include <QStandardItemModel>
#include <QHash>
#include <QSet>

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

    void setPeers(const QVector<tgui::Peer>& peers);
    const QVector<tgui::Peer>& peers() const { return m_peers; }

    // Установить GeoIP-базу для разрешения страны (nullptr — без страны).
    void setGeoIp(const tgui::GeoIpDb* geoip) { m_geoip = geoip; }
    const tgui::GeoIpDb* geoIp() const { return m_geoip; }
    void setShowFlags(bool show);
    bool showFlags() const { return m_showFlags; }
    void setFlagsDirectory(const QString& directory) { m_flagsDirectory = directory; }

    // W11: Resolve Host — асинхронный reverse DNS (IP → hostname) с кешем и обработкой ошибок.
    void setResolveHost(bool on);
    bool resolveHost() const { return m_resolveHost; }
    // Записать результат reverse-DNS в кеш (host=="" — ошибка/отсутствие) и обновить колонку «Адрес».
    void setResolvedHost(const QString& ip, const QString& host);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<tgui::Peer> m_peers;
    const tgui::GeoIpDb* m_geoip = nullptr;
    bool m_showFlags = false;
    QString m_flagsDirectory;
    bool m_resolveHost = false;
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
public:
    enum Column { Name, Size, Done, Progress, Priority, ColumnCount };
    enum Role { SourceRowsRole = Qt::UserRole + 31, PathRole };
    explicit FileTreeModel(QObject* parent = nullptr);
    void setFiles(const QVector<tgui::FileStat>& files);
    QVector<int> sourceRows(const QModelIndex& index) const;
private:
    QStringList m_paths;
    QHash<QString, QStandardItem*> m_nodes;
};
