#include "detailmodels.h"

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>
#include <QHostAddress>
#include <QHostInfo>
#include <QPointer>

#include "theme.h"
#include "tgui/geoip.h"
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
        case ColProgress: {
            // Во время проверки показываем её прогресс, а не completion
            // (у уже скачанного торрента bytesCompleted = 100%). Точность
            // та же, что в списке торрентов (одна десятая процента).
            const double progress = f.checkProgress >= 0.0 ? f.checkProgress : f.completion;
            return QString::number(progress * 100.0, 'f', f.checkProgress >= 0.0 ? 1 : 0) + "%";
        }
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
    if (m_resolveHost)
        startReverseLookups();
}

void PeerListModel::setShowFlags(bool show)
{
    if (m_showFlags == show)
        return;
    m_showFlags = show;
    if (!m_peers.isEmpty())
        emit dataChanged(index(0, ColCountry), index(m_peers.size() - 1, ColCountry),
                         {Qt::DecorationRole});
}

void PeerListModel::setResolveHost(bool on)
{
    if (m_resolveHost == on)
        return;
    m_resolveHost = on;
    if (on)
        startReverseLookups();
    // Показать текущее состояние колонки (IP или уже известный hostname).
    if (!m_peers.isEmpty())
        emit dataChanged(index(0, ColAddress), index(m_peers.size() - 1, ColAddress),
                         {Qt::DisplayRole});
}

void PeerListModel::setResolvedHost(const QString& ip, const QString& host)
{
    if (ip.isEmpty() || m_hostCache.value(ip) == host)
        return;
    m_hostCache.insert(ip, host);
    if (host.isEmpty())
        return; // кеш ошибки — показ остаётся IP, обновлять нечего
    for (int r = 0; r < m_peers.size(); ++r)
        if (m_peers.at(r).address == ip)
            emit dataChanged(index(r, ColAddress), index(r, ColAddress), {Qt::DisplayRole});
}

void PeerListModel::startReverseLookups()
{
    QSet<QString> seen;
    for (const auto& peer : m_peers) {
        const QString ip = peer.address;
        // Только IP-адреса (IPv4/IPv6); hostname не резолвим.
        if (ip.isEmpty() || QHostAddress(ip).isNull())
            continue;
        if (seen.contains(ip) || m_pendingLookups.contains(ip) || m_hostCache.contains(ip))
            continue; // кеш или уже в работе — не дублируем запрос
        seen.insert(ip);
        m_pendingLookups.insert(ip);
        QPointer<PeerListModel> self(this);
        QHostInfo::lookupHost(ip, [self, ip](QHostInfo info) {
            if (!self)
                return; // модель уничтожена — не трогаем её
            self->m_pendingLookups.remove(ip);
            QString host;
            if (info.error() == QHostInfo::NoError && !info.hostName().isEmpty() &&
                info.hostName() != ip)
                host = info.hostName();
            self->setResolvedHost(ip, host); // host=="" → кеш ошибки
        });
    }
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
    if (role != Qt::DisplayRole && role != Qt::DecorationRole)
        return {};
    if (role == Qt::DecorationRole && index.column() != ColCountry)
        return {};

    switch (index.column()) {
        case ColAddress: {
            QString shown = p.address;
            if (m_resolveHost) {
                const QString host = m_hostCache.value(p.address);
                if (!host.isEmpty())
                    shown = host; // reverse DNS найден — показываем hostname вместо IP
            }
            return shown + (p.port > 0 ? QStringLiteral(":") + QString::number(p.port) : QString());
        }
        case ColCountry: {
            if (!m_geoip || !m_geoip->isLoaded())
                return {};
            const std::string code = m_geoip->countryFor(p.address.toStdString());
            if (code.empty())
                return {};
            if (role == Qt::DecorationRole) {
                if (!m_showFlags)
                    return {};
                const QString flagsDir = m_flagsDirectory.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/flags"
                    : m_flagsDirectory;
                const QString flagPath = flagsDir + "/" +
                                         QString::fromStdString(code).toLower() + ".png";
                if (QFileInfo::exists(flagPath))
                    return QIcon(flagPath);
                return {};
            }
            // DisplayRole: full country name
            const std::string name = tgui::GeoIpDb::countryNameFor(code);
            return name.empty() ? QString::fromStdString(code)
                                : QString::fromStdString(name);
        }
        case ColFlags:
            return p.flagStr;
        case ColPort:
            return p.port;
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
            return QObject::tr("Host");
        case ColCountry:
            return QObject::tr("Country");
        case ColFlags:
            return QObject::tr("Flags");
        case ColPort:
            return QObject::tr("Port");
        case ColClient:
            return QObject::tr("Client");
        case ColProgress:
            return QObject::tr("Have");
        case ColRateDown:
            return QObject::tr("Down speed");
        case ColRateUp:
            return QObject::tr("Up speed");
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
            if (t.announceState == 2 || t.announceState == 3)
                return QObject::tr("Updating...");
            return t.lastAnnounceResult;
        case ColUpdate:
            return t.nextAnnounceTime > 0 ? QString::fromStdString(tgui::formatEta(
                qMax<qint64>(0, t.nextAnnounceTime - QDateTime::currentSecsSinceEpoch()))) :
                QStringLiteral("—");
    }
    return {};
}

QVariant TrackerListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColAnnounce:
            return QObject::tr("Name");
        case ColTier:
            return QObject::tr("Tier");
        case ColSeeders:
            return QObject::tr("Seeds");
        case ColLeechers:
            return QObject::tr("Leechers");
        case ColLastResult:
            return QObject::tr("Status");
        case ColUpdate:
            return QObject::tr("Update in");
    }
    return {};
}
FileTreeModel::FileTreeModel(QObject* parent) : QStandardItemModel(parent) {}

void FileTreeModel::setFiles(const QVector<tgui::FileStat>& files)
{
    QStringList paths;
    for (const auto& file : files)
        paths.append(file.name);
    if (paths != m_paths || columnCount() == 0) {
        clear();
        m_nodes.clear();
        m_paths = paths;
        setHorizontalHeaderLabels({tr("Want"), tr("File name"), tr("Size"), tr("Done"),
                                   QStringLiteral("%"), tr("Priority")});
        for (const auto& file : files) {
            QString path;
            QStandardItem* parent = invisibleRootItem();
            const auto parts = file.name.split('/', Qt::SkipEmptyParts);
            for (int i = 0; i < parts.size(); ++i) {
                if (!path.isEmpty()) path += '/';
                path += parts.at(i);
                if (!m_nodes.contains(path)) {
                    QList<QStandardItem*> row;
                    for (int column = 0; column < ColumnCount; ++column) {
                        auto* item = new QStandardItem;
                        item->setEditable(false);
                        item->setData(path, PathRole);
                        row.append(item);
                    }
                    // Want (чекбокс want/skip, референс — TFilesTree).
                    row[Want]->setFlags(row[Want]->flags() | Qt::ItemIsUserCheckable);
                    row[Want]->setCheckState(file.wanted ? Qt::Checked : Qt::Unchecked);
                    row[Name]->setText(parts.at(i));
                    row[Name]->setIcon(i + 1 < parts.size()
                        ? tgui::theme::icon(tgui::theme::Icon::Folder)
                        : tgui::theme::fileIcon(parts.at(i)));
                    parent->appendRow(row);
                    // Якорь дерева — ячейка Want (колонка 0): QStandardItem хранит
                    // children по ячейке, поэтому поддерево привязываем к ней.
                    m_nodes.insert(path, row[Want]);
                }
                parent = m_nodes.value(path);
            }
        }
    }
    struct Aggregate {
        double size = 0, done = 0;
        // Проверка: сумма size*checkProgress и суммарный размер файлов,
        // для которых прогресс проверки известен.
        double checkDone = 0, checkSize = 0;
        QVariantList rows;
        int priority = 0;
        bool first = true, mixed = false, allSkipped = true;
        // Агрегация Want (чекбоксы файлов): все/никакие/смешанное состояние папки.
        bool firstWant = true, allWanted = true, mixedWant = false;
    };
    QHash<QString, Aggregate> totals;
    for (int row = 0; row < files.size(); ++row) {
        const auto& file = files.at(row);
        QString path;
        for (const auto& part : file.name.split('/', Qt::SkipEmptyParts)) {
            if (!path.isEmpty()) path += '/';
            path += part;
            auto& total = totals[path];
            total.size += file.size;
            total.done += file.size * file.completion;
            if (file.checkProgress >= 0.0) {
                const double weight = qMax(1.0, file.size);
                total.checkDone += weight * file.checkProgress;
                total.checkSize += weight;
            }
            total.rows.append(row);
            const int priority = file.wanted ? file.priority : -2;
            if (!total.first && total.priority != priority) total.mixed = true;
            total.priority = priority;
            total.first = false;
            total.allSkipped &= !file.wanted;
            if (!total.firstWant && total.allWanted != file.wanted) total.mixedWant = true;
            total.allWanted &= file.wanted;
            total.firstWant = false;
        }
    }
    for (auto it = totals.cbegin(); it != totals.cend(); ++it) {
        auto* name = m_nodes.value(it.key());
        if (!name) continue;
        auto* parent = name->parent() ? name->parent() : invisibleRootItem();
        const auto& total = it.value();
        for (int column = 0; column < ColumnCount; ++column) {
            auto* item = parent->child(name->row(), column);
            item->setData(total.rows, SourceRowsRole);
            item->setToolTip(it.key());
            item->setForeground(total.allSkipped ? QBrush(Qt::gray) : QBrush());
            if (column == Want) {
                // Папка: три состояния (все/ни одно/смешанное), переключение — каскад.
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(total.mixedWant ? Qt::PartiallyChecked :
                                    total.allWanted ? Qt::Checked : Qt::Unchecked);
            }
            if (column == Size || column == Done)
                item->setText(QString::fromStdString(tgui::formatSize(
                    column == Size ? total.size : total.done)));
            if (column == Progress) {
                // Во время проверки процент — это прогресс проверки.
                const auto& first = files.at(total.rows.first().toInt());
                const double progress =
                    total.rows.size() == 1 && first.checkProgress >= 0.0
                        ? first.checkProgress * 100.0
                        : total.checkSize > 0.0
                        ? total.checkDone * 100.0 / total.checkSize
                        : (total.size > 0 ? total.done * 100.0 / total.size : 100.0);
                item->setData(progress, Qt::DisplayRole);
            }
            if (column == Priority)
                item->setText(total.mixed ? tr("Mixed") : total.priority == -2 ?
                              tr("Skip") : priorityText(total.priority));
            if (column == Size || column == Done)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

QVector<int> FileTreeModel::sourceRows(const QModelIndex& index) const
{
    QVector<int> rows;
    for (const auto& value : index.data(SourceRowsRole).toList())
        rows.append(value.toInt());
    return rows;
}

// Каскад Want по всем потомкам (референс TFilesTree: клик по папке переключает все файлы).
// Все участвующие узлы — колонка Want; child(row, col) возвращает nullptr за пределами.
void FileTreeModel::setDescendants(QStandardItem* node, Qt::CheckState state)
{
    if (!node)
        return;
    node->setCheckState(state);
    for (int row = 0; ; ++row) {
        QStandardItem* child = node->child(row, Want);
        if (!child)
            break;
        setDescendants(child, state);
    }
}

// Пересчёт три-состояния Want у всех родителей после изменения листа/поддерева.
void FileTreeModel::refreshAncestors(QStandardItem* node)
{
    QStandardItem* parent = node ? node->parent() : nullptr;
    while (parent) {
        int checked = 0, unchecked = 0;
        for (int row = 0; ; ++row) {
            QStandardItem* child = parent->child(row, Want);
            if (!child)
                break;
            if (child->checkState() == Qt::Checked)
                ++checked;
            else if (child->checkState() == Qt::Unchecked)
                ++unchecked;
        }
        parent->setCheckState(checked == 0 ? Qt::Unchecked :
                              unchecked == 0 ? Qt::Checked : Qt::PartiallyChecked);
        parent = parent->parent();
    }
}

void FileTreeModel::toggleWant(const QModelIndex& index)
{
    if (!index.isValid() || index.column() != Want)
        return;
    QStandardItem* node = itemFromIndex(index);
    if (!node)
        return;
    const bool wanted = node->checkState() != Qt::Checked;
    // Лист: меняем его; папка: каскад на все потомки.
    setDescendants(node, wanted ? Qt::Checked : Qt::Unchecked);
    refreshAncestors(node);
    emit wantToggled(sourceRows(index), wanted);
}
