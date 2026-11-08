#pragma once

#include <QAbstractProxyModel>

#include "torrentmodel.h"

// Группировка списка торрентов: строки-группы + члены (по папке / лейблу /
// трекеру). Источник — уже отфильтрованный список (TorrentFilterProxy).
// Без группировки строки проходят 1:1.
class GroupedTorrentProxy : public QAbstractProxyModel {
    Q_OBJECT
public:
    enum GroupBy {
        GroupNone = -1,
        GroupFolder = 0,
        GroupLabel,
        GroupTracker
    };

    explicit GroupedTorrentProxy(QObject* parent = nullptr);

    void setSourceProxy(TorrentFilterProxy* proxy);
    void setGroupBy(GroupBy by);
    GroupBy groupBy() const { return m_groupBy; }

    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    bool hasChildren(const QModelIndex& parent) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;

private:
    struct Row {
        bool isGroup = false;
        QString key;
        int srcRow = -1;        // обычная строка
        QVector<int> members;   // строка-группа: строки источника
        double totalSize = 0.0; // строка-группа: суммарный размер
    };

    void rebuild();
    void resortMembers();
    QString groupKey(const tgui::Torrent& t) const;
    QString groupDisplayName(const QString& key) const;

    TorrentFilterProxy* m_src = nullptr;
    GroupBy m_groupBy = GroupNone;
    int m_sortCol = TorrentModel::ColName;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QVector<Row> m_rows;
};
