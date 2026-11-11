#pragma once

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

#include "tgui/models.h"

// Модель списка торрентов (torrent-get). Строки UI — английский (см. WORKFLOW.md 0.3).
class TorrentModel : public QAbstractTableModel {
    Q_OBJECT
public:
    // 26 колонок — порядок и семантика — по референсу (см. WORKFLOW.md).
    enum Column {
        ColName = 0,
        ColSize,           // totalSize
        ColDone,           // прогресс %
        ColStatus,
        ColSeeds,          // peersSendingToUs / seeders
        ColPeers,          // peersGettingFromUs / leechers
        ColRateDown,
        ColRateUp,
        ColEta,
        ColRatio,
        ColDownloaded,
        ColUploaded,
        ColTracker,
        ColTrackerStatus,
        ColAddedOn,
        ColCompletedOn,
        ColLastActive,
        ColPath,
        ColPriority,
        ColSizeToDownload, // sizeWhenDone
        ColId,
        ColQueuePosition,
        ColSeedingTime,
        ColSizeLeft,       // leftUntilDone
        ColPrivate,
        ColLabels,
        ColumnCount
    };

    // Статус-фильтры левой панели — семантика 1:1 с референсом (flt*, main.pas).
    enum StatusGroup {
        GroupAll = -1,     // All torrents
        GroupDownloading = 0, // Download
        GroupCompleted,    // Seed (в т.ч. Finished)
        GroupActive,       // скорости ≠ 0
        GroupInactive,     // не Active и не Stopped
        GroupStopped,      // Stopped или Finished
        GroupError,        // ошибка, торрент не остановлен
        GroupWaiting       // CheckWait/Check/DownloadWait
    };

    // Дополнительные роли для кастомного делегата строки торрентов.
    enum Roles {
        ProgressRole = Qt::UserRole + 1, // double, %
        StatusRole,                      // int, torrent status
        FinishedRole,                    // bool
        ErrorRole,                       // bool
        RateDownRole,                    // double, байт/с
        RateUpRole,                      // double, байт/с
    };

    explicit TorrentModel(QObject* parent = nullptr);

    void setTorrents(const QVector<tgui::Torrent>& torrents);
    const QVector<tgui::Torrent>& torrents() const { return m_torrents; }
    const tgui::Torrent& torrentAt(int row) const { return m_torrents.at(row); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    static QString statusText(int status, bool finished = false);
    static bool matchesGroup(const tgui::Torrent& t, int group);
    static QVariant sortKey(const tgui::Torrent& t, int column);

private:
    QVector<tgui::Torrent> m_torrents;
};

// Фильтр: статус-группа + папка + трекер + поиск по имени (референс — lvFilter/edSearch).
class TorrentFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TorrentFilterProxy(QObject* parent = nullptr);

    // TorrentModel::StatusGroup; GroupAll (-1) — без фильтра.
    void setStatusGroup(int group);
    void setFolderFilter(const QString& folder); // пусто — без фильтра
    void setLabelFilter(const QString& label);   // пусто — без фильтра
    void setTrackerFilter(const QString& announce); // пусто — без фильтра
    void setSearchText(const QString& text);

    // Торрент по строке прокси (с маппингом в исходную модель).
    const tgui::Torrent& torrentAt(int row) const;

    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    template<typename T>
    void setFilterValue(T& current, const T& value);

    int m_group = TorrentModel::GroupAll;
    QString m_folder;
    QString m_label;
    QString m_tracker;
    QString m_search;
};
