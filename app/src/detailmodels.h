#pragma once

#include <QAbstractTableModel>

#include "tgui/models.h"

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
        ColClient,
        ColProgress,
        ColRateDown,
        ColRateUp,
        ColumnCount
    };

    explicit PeerListModel(QObject* parent = nullptr);

    void setPeers(const QVector<tgui::Peer>& peers);
    const QVector<tgui::Peer>& peers() const { return m_peers; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<tgui::Peer> m_peers;
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