#include "detailmodels.h"

#include <QBrush>
#include <algorithm>
#include <QCoreApplication>
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
    const bool sameRows = std::ranges::equal(m_files, files, [](const auto& a, const auto& b) {
        return a.index == b.index && a.name == b.name;
    });
    if (!sameRows) {
        beginResetModel();
        m_files = files;
        endResetModel();
        return;
    }

    int first = -1;
    int last = -1;
    for (int row = 0; row < files.size(); ++row) {
        if (m_files.at(row) != files.at(row)) {
            if (first < 0)
                first = row;
            last = row;
        }
    }
    if (first >= 0) {
        m_files = files;
        emit dataChanged(index(first, 0), index(last, ColumnCount - 1),
                         {Qt::DisplayRole, Qt::CheckStateRole});
    }
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
    // Свободная функция: lupdate кладёт QObject::tr в контекст «QObject»,
    // поэтому контекст указываем явно (I18N-03).
    switch (priority) {
        case -1:
            return QCoreApplication::translate("FileListModel", "Low");
        case 1:
            return QCoreApplication::translate("FileListModel", "High");
        default:
            return QCoreApplication::translate("FileListModel", "Normal");
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
            return f.wanted ? tr("on") : tr("skip");
    }
    return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColName:
            return tr("Name");
        case ColSize:
            return tr("Size");
        case ColProgress:
            return tr("Progress");
        case ColPriority:
            return tr("Priority");
        case ColWanted:
            return tr("Wanted");
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

/** Обновляет флаги на уже показанных строках после фоновой распаковки. */
void PeerListModel::setFlagsDirectory(const QString& directory)
{
    m_flagsDirectory = directory;
    m_flagCache.clear();
    if (m_showFlags && !m_peers.isEmpty())
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
    const auto existing = m_hostCache.constFind(ip);
    if (ip.isEmpty() || (existing != m_hostCache.cend() && *existing == host))
        return;
    const bool changed = existing == m_hostCache.cend() ? !host.isEmpty() : *existing != host;
    constexpr qsizetype maxCachedHosts = 4096;
    const bool expired = existing == m_hostCache.cend() && m_hostCache.size() >= maxCachedHosts;
    if (expired)
        m_hostCache.clear();
    m_hostCache.insert(ip, host);
    if (expired && !m_peers.isEmpty()) {
        emit dataChanged(index(0, ColAddress), index(m_peers.size() - 1, ColAddress), {Qt::DisplayRole});
        return;
    }
    if (!changed)
        return;
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
        if (seen.contains(ip) || m_pendingLookups.contains(ip) || hasHostLookupResult(ip))
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

    if (role == ProgressRole && index.column() == ColProgress)
        return p.progress;
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
                return role == Qt::DisplayRole ? QVariant(QStringLiteral("—")) : QVariant{};
            const std::string code = m_geoip->countryFor(p.address.toStdString());
            if (code.empty())
                return role == Qt::DisplayRole ? QVariant(QStringLiteral("—")) : QVariant{};
            if (role == Qt::DecorationRole) {
                if (!m_showFlags)
                    return {};
                const auto country = QString::fromStdString(code).toLower();
                auto flag = m_flagCache.constFind(country);
                if (flag == m_flagCache.cend()) {
                    const QString directory = m_flagsDirectory.isEmpty()
                        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/flags"
                        : m_flagsDirectory;
                    const QString path = directory + '/' + country + ".png";
                    flag = m_flagCache.insert(country, QFileInfo::exists(path) ? QIcon(path) : QIcon{});
                }
                if (!flag->isNull())
                    return *flag;
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
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole &&
        (section == ColRateDown || section == ColRateUp))
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColAddress:
            return tr("Host");
        case ColCountry:
            return tr("Country");
        case ColFlags:
            return tr("Flags");
        case ColPort:
            return tr("Port");
        case ColClient:
            return tr("Client");
        case ColProgress:
            return tr("Have");
        case ColRateDown:
            return tr("Down speed");
        case ColRateUp:
            return tr("Up speed");
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

    if (role == Qt::TextAlignmentRole &&
        ((index.column() >= ColTier && index.column() <= ColLeechers) || index.column() == ColUpdate))
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (index.column() == ColLastResult && t.hasAnnounced && !t.lastAnnounceSucceeded &&
        !t.lastAnnounceResult.isEmpty() &&
        t.lastAnnounceResult.compare(QLatin1String("Success"), Qt::CaseInsensitive) != 0) {
        if (role == Qt::ForegroundRole)
            return QVariant::fromValue(QBrush(tgui::theme::palette().red));
        if (role == Qt::ToolTipRole)
            return t.lastAnnounceResult;
    }
    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
        case ColAnnounce:
            return t.announce;
        case ColTier:
            return t.tier;
        case ColSeeders:
            return t.seederCount >= 0 ? QString::number(t.seederCount) : tr("—");
        case ColLeechers:
            return t.leecherCount >= 0 ? QString::number(t.leecherCount) : tr("—");
        case ColLastResult:
            if (t.announceState == 2 || t.announceState == 3)
                return tr("Updating...");
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
    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole &&
        ((section >= ColTier && section <= ColLeechers) || section == ColUpdate))
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColAnnounce:
            return tr("Tracker");
        case ColTier:
            return tr("Tier");
        case ColSeeders:
            return tr("Seeds");
        case ColLeechers:
            return tr("Leechers");
        case ColLastResult:
            return tr("Status");
        case ColUpdate:
            return tr("Update in");
    }
    return {};
}
FileTreeModel::FileTreeModel(QObject* parent) : QStandardItemModel(parent) {}

void FileTreeModel::Aggregate::add(const Aggregate& other)
{
    size += other.size;
    done += other.done;
    checkDone += other.checkDone;
    checkSize += other.checkSize;
    wanted += other.wanted;
    count += other.count;
    minPriority = qMin(minPriority, other.minPriority);
    maxPriority = qMax(maxPriority, other.maxPriority);
}

void FileTreeModel::setFiles(const QVector<tgui::FileStat>& files)
{
    const bool rebuild = columnCount() == 0 || m_paths.size() != files.size() ||
        !std::ranges::equal(m_paths, files, {}, std::identity{}, &tgui::FileStat::name);
    if (rebuild) {
        clear();
        m_paths.clear();
        m_nodes.clear();
        m_fileNodes.clear();
        m_paths.reserve(files.size());
        m_fileNodes.reserve(files.size());
        setHorizontalHeaderLabels({tr("Want"), tr("File name"), tr("Size"), tr("Done"),
                                   QStringLiteral("%"), tr("Priority")});
        QHash<QString, int> nodesByPath;
        for (const auto& file : files) {
            m_paths.append(file.name);
            QString path;
            int parentNode = -1;
            const auto parts = file.name.split('/', Qt::SkipEmptyParts);
            for (int depth = 0; depth < parts.size(); ++depth) {
                if (!path.isEmpty())
                    path += '/';
                path += parts.at(depth);
                int node = nodesByPath.value(path, -1);
                if (node < 0) {
                    QList<QStandardItem*> row;
                    for (int column = 0; column < ColumnCount; ++column) {
                        auto* item = new QStandardItem;
                        item->setEditable(false);
                        item->setData(path, PathRole);
                        item->setToolTip(path);
                        row.append(item);
                    }
                    row[Want]->setFlags(row[Want]->flags() | Qt::ItemIsUserCheckable);
                    row[Name]->setText(parts.at(depth));
                    row[Name]->setIcon(depth + 1 < parts.size()
                        ? tgui::theme::icon(tgui::theme::Icon::Folder)
                        : tgui::theme::fileIcon(parts.at(depth)));
                    row[Size]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    row[Done]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    auto* parent = parentNode < 0 ? invisibleRootItem() : m_nodes.at(parentNode).item;
                    parent->appendRow(row);
                    node = static_cast<int>(m_nodes.size());
                    row[Want]->setData(node, SourceRowsRole);
                    m_nodes.append({row[Want], parentNode, {}, {}});
                    nodesByPath.insert(path, node);
                }
                parentNode = node;
            }
            if (parentNode >= 0)
                m_nodes[parentNode].rows.append(static_cast<int>(m_fileNodes.size()));
            m_fileNodes.append(parentNode);
        }
    }

    QVector<Aggregate> totals(m_nodes.size());
    for (qsizetype row = 0; row < files.size(); ++row) {
        const int node = m_fileNodes.at(row);
        if (node < 0)
            continue;
        const auto& file = files.at(row);
        const double weight = file.checkProgress >= 0.0 ? qMax(1.0, file.size) : 0.0;
        const int priority = file.wanted ? file.priority : -2;
        totals[node].add({file.size, file.size * file.completion, weight * file.checkProgress,
                          weight, file.wanted ? 1 : 0, 1, priority, priority});
    }

    // Каждый родитель создан раньше своих детей. Обратный обход считает
    // агрегаты папок за один проход, без повторного разбора путей.
    for (qsizetype i = m_nodes.size(); i-- > 0;) {
        auto& node = m_nodes[i];
        const auto& total = totals.at(i);
        if (node.parent >= 0)
            totals[node.parent].add(total);
        auto* want = node.item;
        want->setCheckState(total.wanted == 0 ? Qt::Unchecked :
                            total.wanted == total.count ? Qt::Checked : Qt::PartiallyChecked);
        if (!rebuild && node.previous == total)
            continue;
        auto* parent = want->parent() ? want->parent() : invisibleRootItem();
        const int row = want->row();
        if (rebuild || (node.previous.wanted == 0) != (total.wanted == 0)) {
            const auto foreground = total.wanted == 0 ? QBrush(Qt::gray) : QBrush();
            for (int column = 0; column < ColumnCount; ++column)
                parent->child(row, column)->setForeground(foreground);
        }
        if (rebuild || node.previous.size != total.size)
            parent->child(row, Size)->setText(QString::fromStdString(tgui::formatSize(total.size)));
        if (rebuild || node.previous.done != total.done)
            parent->child(row, Done)->setText(QString::fromStdString(tgui::formatSize(total.done)));
        const double progress = total.checkSize > 0.0 ? total.checkDone * 100.0 / total.checkSize
            : total.size > 0.0 ? total.done * 100.0 / total.size : 100.0;
        parent->child(row, Progress)->setData(progress, Qt::DisplayRole);
        if (rebuild || node.previous.minPriority != total.minPriority ||
            node.previous.maxPriority != total.maxPriority) {
            parent->child(row, Priority)->setText(total.minPriority != total.maxPriority ? tr("Mixed")
                : total.minPriority == -2 ? tr("Skip") : priorityText(total.minPriority));
        }
        node.previous = total;
    }
}

void FileTreeModel::collectRows(const QStandardItem* item, QVector<int>& rows) const
{
    const auto& node = m_nodes.at(item->data(SourceRowsRole).toInt());
    rows.append(node.rows);
    for (int row = 0; row < item->rowCount(); ++row)
        collectRows(item->child(row, Want), rows);
}

QVector<int> FileTreeModel::sourceRows(const QModelIndex& index) const
{
    QVector<int> rows;
    const auto* item = itemFromIndex(index.siblingAtColumn(Want));
    if (item) {
        collectRows(item, rows);
        std::ranges::sort(rows);
    }
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
