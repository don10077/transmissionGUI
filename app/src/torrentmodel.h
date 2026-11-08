#pragma once

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

#include "tgui/models.h"

// Модель списка торрентов (torrent-get).
class TorrentModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColName = 0,
        ColStatus,
        ColProgress,
        ColSize,
        ColRateDown,
        ColRateUp,
        ColEta,
        ColPeers,
        ColRatio,
        ColLabels,
        ColumnCount
    };

    // Группы статусов для фильтра.
    enum StatusGroup {
        GroupStopped = 0,
        GroupWaiting,
        GroupDownloading,
        GroupSeeding,
        GroupFinished
    };
    static constexpr int StatusGroupRole = Qt::UserRole + 1;

    explicit TorrentModel(QObject* parent = nullptr);

    void setTorrents(const QVector<tgui::Torrent>& torrents);
    const QVector<tgui::Torrent>& torrents() const { return m_torrents; }
    const tgui::Torrent& torrentAt(int row) const { return m_torrents.at(row); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    static QString statusText(int status);
    static int statusGroup(int status, double sizeWhenDone, double leftUntilDone);
    static QVariant sortKey(const tgui::Torrent& t, int column);

private:
    QVector<tgui::Torrent> m_torrents;
};

// Фильтр по группе статусов + поиск по имени.
class TorrentFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit TorrentFilterProxy(QObject* parent = nullptr);

    // -1 — все; иначе TorrentModel::StatusGroup.
    void setStatusGroup(int group);

    void setSearchText(const QString& text);

    // Торрент по строке прокси (с маппингом в исходную модель).
    const tgui::Torrent& torrentAt(int row) const;

    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    int m_group = -1;
    QString m_search;
};
