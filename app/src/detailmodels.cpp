#include "detailmodels.h"

#include <algorithm>

#include "tgui/util.h"

// --- FileListModel ---

FileListModel::FileListModel(QObject* parent) : QAbstractTableModel(parent) {}

void FileListModel::setFiles(const QVector<tgui::FileStat>& files)
{
    beginResetModel();
    m_files = files;
    endResetModel();
}

bool FileListModel::wantedAt(int row) const
{
    return row >= 0 && row < m_files.size() && m_files.at(row).wanted;
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_files.size());
}

int FileListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

static QString priorityText(int priority)
{
    switch (priority) {
        case -1:
            return QObject::tr("Low");
        case 1:
            return QObject::tr("High");
        default:
            return QObject::tr("Normal");
    }
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_files.size())
        return {};
    const tgui::FileStat& f = m_files.at(index.row());

    if (role == Qt::TextAlignmentRole && index.column() >= ColSize && index.column() <= ColPriority)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::CheckStateRole && index.column() == ColWanted)
        return f.wanted ? Qt::Checked : Qt::Unchecked;
    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
        case ColName:
            return f.name;
        case ColSize:
            return QString::fromStdString(tgui::formatSize(f.size));
        case ColProgress:
            return QString::number(f.completion * 100.0, 'f', 0) + "%";
        case ColPriority:
            return priorityText(f.priority);
        case ColWanted:
            return f.wanted ? QObject::tr("on") : QObject::tr("skip");
    }
    return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColName:
            return QObject::tr("Name");
        case ColSize:
            return QObject::tr("Size");
        case ColProgress:
            return QObject::tr("Progress");
        case ColPriority:
            return QObject::tr("Priority");
        case ColWanted:
            return QObject::tr("Wanted");
    }
    return {};
}

Qt::ItemFlags FileListModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags base = QAbstractItemModel::flags(index);
    if (index.isValid() && index.column() == ColWanted)
        base |= Qt::ItemIsUserCheckable | Qt::ItemIsEditable;
    return base;
}

bool FileListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.column() != ColWanted)
        return false;
    if (index.row() >= m_files.size())
        return false;
    if (role == Qt::CheckStateRole || role == Qt::EditRole) {
        const bool wanted = (value.toInt() != Qt::Unchecked);
        if (m_files[index.row()].wanted == wanted)
            return true;
        m_files[index.row()].wanted = wanted;
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::CheckStateRole});
        emit wantChanged(m_files[index.row()].index, wanted);
        return true;
    }
    return false;
}

// --- PeerListModel ---

PeerListModel::PeerListModel(QObject* parent) : QAbstractTableModel(parent) {}

void PeerListModel::setPeers(const QVector<tgui::Peer>& peers)
{
    beginResetModel();
    m_peers = peers;
    endResetModel();
}

int PeerListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_peers.size());
}

int PeerListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PeerListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_peers.size())
        return {};
    const tgui::Peer& p = m_peers.at(index.row());

    if (role == Qt::TextAlignmentRole && index.column() >= ColProgress)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
        case ColAddress:
            return p.address + (p.port > 0 ? QStringLiteral(":") + QString::number(p.port)
                                           : QString());
        case ColClient:
            return p.clientName;
        case ColProgress:
            return QString::number(p.progress, 'f', 0) + "%";
        case ColRateDown:
            return QString::fromStdString(tgui::formatRate(p.rateToClient));
        case ColRateUp:
            return QString::fromStdString(tgui::formatRate(p.rateToPeer));
    }
    return {};
}

QVariant PeerListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColAddress:
            return QObject::tr("Address");
        case ColClient:
            return QObject::tr("Client");
        case ColProgress:
            return QObject::tr("Progress");
        case ColRateDown:
            return QObject::tr("↓ Speed");
        case ColRateUp:
            return QObject::tr("↑ Speed");
    }
    return {};
}

// --- TrackerListModel ---

TrackerListModel::TrackerListModel(QObject* parent) : QAbstractTableModel(parent) {}

void TrackerListModel::setTrackers(const QVector<tgui::TrackerStat>& trackers)
{
    beginResetModel();
    m_trackers = trackers;
    endResetModel();
}

int TrackerListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_trackers.size());
}

int TrackerListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TrackerListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_trackers.size())
        return {};
    const tgui::TrackerStat& t = m_trackers.at(index.row());

    if (role == Qt::TextAlignmentRole && index.column() >= ColTier && index.column() <= ColLeechers)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
        case ColAnnounce:
            return t.announce;
        case ColTier:
            return t.tier;
        case ColSeeders:
            return t.seederCount >= 0 ? QString::number(t.seederCount) : QObject::tr("—");
        case ColLeechers:
            return t.leecherCount >= 0 ? QString::number(t.leecherCount) : QObject::tr("—");
        case ColLastResult:
            return t.lastAnnounceResult;
    }
    return {};
}

QVariant TrackerListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColAnnounce:
            return QObject::tr("Announce");
        case ColTier:
            return QObject::tr("Tier");
        case ColSeeders:
            return QObject::tr("Seeders");
        case ColLeechers:
            return QObject::tr("Leechers");
        case ColLastResult:
            return QObject::tr("Last result");
    }
    return {};
}