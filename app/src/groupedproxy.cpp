#include "groupedproxy.h"

#include <QColor>
#include <QFont>
#include <QMap>

#include <algorithm>

#include "tgui/util.h"

GroupedTorrentProxy::GroupedTorrentProxy(QObject* parent)
    : QAbstractProxyModel(parent)
{
}

void GroupedTorrentProxy::setSourceProxy(TorrentFilterProxy* proxy)
{
    beginResetModel();
    if (m_src)
        disconnect(m_src);
    m_src = proxy;
    if (m_src) {
        // Любой пересчёт фильтра/обновление списка → пересборка структуры.
        connect(m_src, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&,
                                                                        int, int) { rebuild(); });
        connect(m_src, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex&,
                                                                      int, int) { rebuild(); });
        // Полный сброс источника (getTorrents → setTorrents) — основной путь!
        connect(m_src, &QAbstractItemModel::modelReset, this, [this] { rebuild(); });
        // Сортировка/переупорядочивание источника.
        connect(m_src, &QAbstractItemModel::layoutChanged, this, [this] { rebuild(); });
    }
    m_rows.clear();
    endResetModel();
}

void GroupedTorrentProxy::setGroupBy(GroupBy by)
{
    if (m_groupBy == by)
        return;
    m_groupBy = by;
    rebuild();
}

QModelIndex GroupedTorrentProxy::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= m_rows.size() || column < 0 ||
        column >= columnCount())
        return QModelIndex();
    return createIndex(row, column);
}

QModelIndex GroupedTorrentProxy::parent(const QModelIndex&) const
{
    return QModelIndex();
}

int GroupedTorrentProxy::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int GroupedTorrentProxy::columnCount(const QModelIndex&) const
{
    return m_src ? m_src->columnCount() : TorrentModel::ColumnCount;
}

bool GroupedTorrentProxy::hasChildren(const QModelIndex& parent) const
{
    // Плоский список: корень имеет строки, строки — не имеют детей.
    // (Важно для QTreeView::layout(): если hasChildren(root) == false,
    // виджет вообще не запрашивает строки — таблица пустая.)
    if (!parent.isValid())
        return m_rows.size() > 0;
    return false;
}

QVariant GroupedTorrentProxy::headerData(int section, Qt::Orientation orientation,
                                         int role) const
{
    // Явный перенос заголовков от источника (через цепочку прокси).
    if (m_src)
        return m_src->headerData(section, orientation, role);
    return {};
}

QVariant GroupedTorrentProxy::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_src || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const Row& r = m_rows.at(index.row());
    if (!r.isGroup)
        return m_src->data(m_src->index(r.srcRow, index.column()), role);

    // Строка-группа.
    if (role == Qt::FontRole && index.column() == TorrentModel::ColName) {
        QFont f;
        f.setBold(true);
        return f;
    }
    if (role == Qt::BackgroundRole)
        return QColor(235, 241, 248);
    if (role != Qt::DisplayRole)
        return QVariant();
    switch (index.column()) {
        case TorrentModel::ColName:
            return groupDisplayName(r.key);
        case TorrentModel::ColStatus:
            return tr("%1").arg(r.members.size());
        case TorrentModel::ColSize:
            return QString::fromStdString(tgui::formatSize(r.totalSize));
        default:
            return QVariant();
    }
}

Qt::ItemFlags GroupedTorrentProxy::flags(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return Qt::NoItemFlags;
    const Row& r = m_rows.at(index.row());
    if (r.isGroup)
        return Qt::ItemIsEnabled; // не выделяется
    return m_src ? m_src->flags(m_src->index(r.srcRow, index.column())) : Qt::NoItemFlags;
}

void GroupedTorrentProxy::sort(int column, Qt::SortOrder order)
{
    m_sortCol = column;
    m_sortOrder = order;
    if (!m_src)
        return;
    if (m_groupBy == GroupNone) {
        m_src->sort(column, order);
        return;
    }
    beginResetModel();
    resortMembers();
    endResetModel();
}

QModelIndex GroupedTorrentProxy::mapToSource(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid() || !m_src || proxyIndex.row() >= m_rows.size())
        return QModelIndex();
    const Row& r = m_rows.at(proxyIndex.row());
    if (r.isGroup)
        return QModelIndex();
    return m_src->index(r.srcRow, proxyIndex.column());
}

QModelIndex GroupedTorrentProxy::mapFromSource(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid() || !m_src)
        return QModelIndex();
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows.at(i);
        if (!r.isGroup && r.srcRow == sourceIndex.row())
            return createIndex(i, sourceIndex.column());
    }
    return QModelIndex();
}

void GroupedTorrentProxy::rebuild()
{
    beginResetModel();
    m_rows.clear();
    if (!m_src) {
        endResetModel();
        return;
    }
    if (m_groupBy == GroupNone) {
        for (int i = 0; i < m_src->rowCount(); ++i) {
            Row r;
            r.srcRow = i;
            m_rows.append(r);
        }
        endResetModel();
        return;
    }

    // QMap — ключи автоматически по возрастанию.
    QMap<QString, int> indexOf;
    for (int i = 0; i < m_src->rowCount(); ++i) {
        const auto& t = m_src->torrentAt(i);
        const QString key = groupKey(t);
        const double size = t.sizeWhenDone.value_or(t.totalSize.value_or(0.0));
        auto it = indexOf.constFind(key);
        if (it == indexOf.constEnd()) {
            Row g;
            g.isGroup = true;
            g.key = key;
            g.members.append(i);
            g.totalSize = size;
            indexOf.insert(key, m_rows.size());
            m_rows.append(g);
        } else {
            Row& g = m_rows[it.value()];
            g.members.append(i);
            g.totalSize += size;
        }
    }
    resortMembers();
    endResetModel();
}

void GroupedTorrentProxy::resortMembers()
{
    if (!m_src || m_groupBy == GroupNone)
        return;
    for (auto& g : m_rows) {
        if (!g.isGroup)
            continue;
        std::sort(g.members.begin(), g.members.end(), [this](int a, int b) {
            const QVariant va = TorrentModel::sortKey(m_src->torrentAt(a), m_sortCol);
            const QVariant vb = TorrentModel::sortKey(m_src->torrentAt(b), m_sortCol);
            int c = 0;
            if (va.canConvert<double>() && vb.canConvert<double>()) {
                const double da = va.toDouble();
                const double db = vb.toDouble();
                c = (da < db) ? -1 : (da > db) ? 1 : 0;
            } else {
                c = QString::localeAwareCompare(va.toString(), vb.toString());
            }
            if (c == 0)
                return a < b; // устойчиво
            return m_sortOrder == Qt::AscendingOrder ? c < 0 : c > 0;
        });
    }
}

QString GroupedTorrentProxy::groupKey(const tgui::Torrent& t) const
{
    switch (m_groupBy) {
        case GroupFolder:
            return t.downloadDir.value_or(QString());
        case GroupLabel:
            if (t.labels && !t.labels->isEmpty())
                return t.labels->first();
            return QString();
        case GroupTracker:
            if (!t.trackerStats.isEmpty())
                return t.trackerStats.first().announce;
            return QString();
        default:
            break;
    }
    return QStringLiteral("—");
}

QString GroupedTorrentProxy::groupDisplayName(const QString& key) const
{
    if (!key.isEmpty())
        return key;
    switch (m_groupBy) {
        case GroupFolder:
            return tr("(no folder)");
        case GroupLabel:
            return tr("(no label)");
        case GroupTracker:
            return tr("(no tracker)");
        default:
            return QStringLiteral("—");
    }
}