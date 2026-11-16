#include "torrentmodel.h"

#include <QBrush>

#include <algorithm>

#include "theme.h"
#include "tgui/util.h"

TorrentModel::TorrentModel(QObject* parent) : QAbstractTableModel(parent) {}

/** Меняет только отображение скоростей, оставляя данные RPC доступными без сглаживания. */
void TorrentModel::setSmoothRates(bool enabled)
{
    if (m_smoothRates == enabled)
        return;
    m_smoothRates = enabled;
    m_displayedRates.clear();
    if (!m_torrents.isEmpty())
        emit dataChanged(index(0, ColRateDown), index(rowCount() - 1, ColRateUp), {Qt::DisplayRole});
}

/** Возвращает отображаемые скорости загрузки и отдачи в байтах в секунду. */
QPair<double, double> TorrentModel::displayedRates(const tgui::Torrent& torrent) const
{
    return m_displayedRates.value(torrent.id, {static_cast<double>(torrent.rateDownload.value_or(0)),
                                               static_cast<double>(torrent.rateUpload.value_or(0))});
}

void TorrentModel::setTorrents(const QVector<tgui::Torrent>& torrents)
{
    if (m_smoothRates) {
        QHash<int, QPair<double, double>> rates;
        const auto smooth = [](double previous, double sample) {
            return sample > 0.0 ? (previous + sample) / 2.0 : 0.0;
        };
        for (const auto& torrent : torrents) {
            const double down = static_cast<double>(torrent.rateDownload.value_or(0));
            const double up = static_cast<double>(torrent.rateUpload.value_or(0));
            const auto previous = m_displayedRates.value(torrent.id, {down, up});
            rates.insert(torrent.id, {smooth(previous.first, down), smooth(previous.second, up)});
        }
        m_displayedRates.swap(rates);
    }
    // Most RPC updates change values, not the set of rows. Keep the model's
    // indexes and the view's scroll position intact in that common case.
    bool sameRows = m_torrents.size() == torrents.size();
    for (int i = 0; sameRows && i < torrents.size(); ++i)
        sameRows = m_torrents.at(i).id == torrents.at(i).id;
    if (sameRows) {
        m_torrents = torrents;
        if (!m_torrents.isEmpty())
            emit dataChanged(index(0, 0), index(m_torrents.size() - 1, ColumnCount - 1));
        return;
    }
    beginResetModel();
    m_torrents = torrents;
    endResetModel();
}

int TorrentModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_torrents.size());
}

int TorrentModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QString TorrentModel::statusText(int status, bool finished)
{
    // Текст статуса — 1:1 с GetTorrentStatus референса (main.pas).
    if (finished && status == 0)
        return QObject::tr("Finished");
    switch (status) {
        case 0:
            return QObject::tr("Paused");
        case 1:
            return QObject::tr("Waiting");
        case 2:
            return QObject::tr("Verifying");
        case 3:
            return QObject::tr("Waiting");
        case 4:
            return QObject::tr("Downloading");
        case 5:
            return QObject::tr("Waiting");
        case 6:
            return QObject::tr("Seeding");
    }
    return QObject::tr("Unknown");
}

bool TorrentModel::matchesGroup(const tgui::Torrent& t, int group)
{
    // Семантика групп — 1:1 с lvFilter референса (flt*, main.pas).
    const int status = t.status.value_or(-1);
    const bool finished =
        tgui::statusIsFinished(status, t.sizeWhenDone.value_or(0.0),
                               t.leftUntilDone.value_or(0.0));
    const bool active = (t.rateDownload.value_or(0) != 0) ||
                        (t.rateUpload.value_or(0) != 0);
    const bool error = !tgui::torrentError(t).isEmpty();
    const bool stopped = status == 0 && !error;
    switch (group) {
        case GroupAll:
            return true;
        case GroupDownloading:
            return status == 4;
        case GroupCompleted:
            return (status == 6) || (finished && !error);
        case GroupActive:
            return active;
        case GroupInactive:
            return !active && !stopped;
        case GroupStopped:
            return stopped;
        case GroupError:
            return error;
        case GroupWaiting:
            return status == 1 || status == 2 || status == 3;
    }
    return true;
}

namespace {

// dd.MM.yyyy HH:mm; <= 0 — «—».
QString fmtTime(int unix)
{
    if (unix <= 0)
        return QStringLiteral("—");
    return QDateTime::fromSecsSinceEpoch(unix).toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

// 93782 → «25:53:02».
QString fmtDuration(int seconds)
{
    if (seconds < 0)
        return QStringLiteral("—");
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString priorityText(int priority)
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

// Статус трекера : lastAnnounceSucceeded → «Работает»,
// иначе lastAnnounceResult («Success» → «Работает»); fallback — announceResponse.
QString trackerStatusText(const tgui::Torrent& t)
{
    if (!t.trackerStats.isEmpty()) {
        const auto& ts = t.trackerStats.first();
        if (ts.hasAnnounced) {
            if (ts.lastAnnounceSucceeded ||
                ts.lastAnnounceResult.compare(QLatin1String("Success"), Qt::CaseInsensitive) == 0)
                return QObject::tr("Working");
            return ts.lastAnnounceResult;
        }
        return t.announceResponse.value_or(QString());
    }
    return QStringLiteral("—");
}

// «N» / «N/M» — текущие/всего у активного трекера (GetSeedsText из main.pas).
QString countWithTotal(int current, int total)
{
    if (current < 0)
        current = 0;
    if (total < 0)
        return QString::number(current);
    return QObject::tr("%1/%2").arg(current).arg(total);
}

// Идёт проверка торрента?
bool isVerifying(const tgui::Torrent& t)
{
    return t.status.value_or(-1) == static_cast<int>(tgui::TorrentStatus::Check) &&
           t.recheckProgress.has_value();
}

// Процент проверки: демон не сбрасывает have-биты при перепроверке уже
// скачанного торрента, поэтому sizeWhenDone-leftUntilDone всё время
// проверки дают 100%. Референс (FillTorrentsList, main.pas) в статусе
// «Проверка» показывает recheckProgress.
double verifyingPercent(const tgui::Torrent& t)
{
    return std::clamp(t.recheckProgress.value_or(0.0) * 100.0, 0.0, 100.0);
}

/** Возвращает выравнивание чисел и дат в таблице и в заголовке. */
QVariant columnAlignment(int column)
{
    using enum TorrentModel::Column;
    switch (column) {
        case ColSize:
        case ColSeeds:
        case ColPeers:
        case ColRateDown:
        case ColRateUp:
        case ColEta:
        case ColRatio:
        case ColDownloaded:
        case ColUploaded:
        case ColAddedOn:
        case ColCompletedOn:
        case ColLastActive:
        case ColSizeToDownload:
        case ColId:
        case ColQueuePosition:
        case ColSeedingTime:
        case ColSizeLeft:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return {};
    }
}

/** Возвращает цвет текста статуса, сохраняя приоритет ошибки. */
QColor statusForeground(int status, bool error)
{
    const auto& colors = tgui::theme::palette();
    if (error)
        return colors.red;
    using tgui::TorrentStatus;
    switch (static_cast<TorrentStatus>(status)) {
        case TorrentStatus::Download:
            return colors.blue;
        case TorrentStatus::Seed:
            return colors.green;
        case TorrentStatus::CheckWait:
        case TorrentStatus::Check:
        case TorrentStatus::DownloadWait:
        case TorrentStatus::SeedWait:
            return colors.amber;
        default:
            return colors.statusNeutral;
    }
}

} // namespace

QVariant TorrentModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_torrents.size())
        return {};
    const auto& t = m_torrents.at(index.row());
    const int col = index.column();
    const int status = t.status.value_or(-1);
    const auto isFinished = [&] {
        return tgui::statusIsFinished(status, t.sizeWhenDone.value_or(0.0), t.leftUntilDone.value_or(0.0));
    };
    const auto hasError = [&] { return !tgui::torrentError(t).isEmpty(); };

    if (role == Qt::ToolTipRole) {
        const QString error = tgui::torrentError(t);
        if (col == ColLabels && t.labels && !t.labels->isEmpty()) {
            const QString labels = t.labels->join(QStringLiteral(", "));
            return error.isEmpty() ? labels : error + QLatin1Char('\n') + labels;
        }
        return error.isEmpty() ? QVariant{} : QVariant(error);
    }

    // Числовые роли — для кастомного делегата (не зависят от текста ячейки).
    switch (role) {
        case ProgressRole:
            if (isVerifying(t))
                return verifyingPercent(t);
            if (t.sizeWhenDone && t.sizeWhenDone.value() > 0.0)
                return tgui::percentComplete(t.sizeWhenDone.value(),
                                             t.leftUntilDone.value_or(0.0));
            if (t.metadataPercentComplete)
                return t.metadataPercentComplete.value() * 100.0;
            return -1.0;
        case StatusRole:
            return status;
        case FinishedRole:
            return isFinished() && !hasError();
        case ErrorRole:
            return hasError();
        case LabelListRole:
            return t.labels ? QVariant::fromValue(*t.labels) : QVariant{};
        case Qt::ForegroundRole:
            return col == ColStatus ? QVariant::fromValue(QBrush(statusForeground(status, hasError())))
                                    : QVariant{};
        case RateDownRole:
            return static_cast<double>(t.rateDownload.value_or(0));
        case RateUpRole:
            return static_cast<double>(t.rateUpload.value_or(0));
        default:
            break;
    }

    if (role == Qt::TextAlignmentRole)
        return columnAlignment(col);
    if (role != Qt::DisplayRole)
        return {};

    switch (col) {
        case ColName:
            return t.name.value_or(QString());
        case ColSize:
            return t.totalSize
                       ? QString::fromStdString(tgui::formatSize(t.totalSize.value()))
                       : (t.sizeWhenDone
                              ? QString::fromStdString(tgui::formatSize(t.sizeWhenDone.value()))
                              : QStringLiteral("—"));
        case ColDone: {
            if (isVerifying(t))
                return QString::number(verifyingPercent(t), 'f', 1) + "%";
            if (t.sizeWhenDone && t.sizeWhenDone.value() > 0.0)
                return QString::number(
                           tgui::percentComplete(t.sizeWhenDone.value(),
                                                 t.leftUntilDone.value_or(0.0)),
                           'f', 1) +
                       "%";
            if (t.metadataPercentComplete)
                return QObject::tr("metadata %1%")
                    .arg(t.metadataPercentComplete.value() * 100.0, 0, 'f', 0);
            return QStringLiteral("—");
        }
        case ColStatus:
            return hasError() ? QObject::tr("Error") : statusText(status, isFinished());
        case ColSeeds:
            return countWithTotal(t.peersSendingToUs.value_or(0),
                                  t.trackerStats.isEmpty() ? -1
                                                           : t.trackerStats.first().seederCount);
        case ColPeers:
            return countWithTotal(t.peersGettingFromUs.value_or(0),
                                  t.trackerStats.isEmpty() ? -1
                                                           : t.trackerStats.first().leecherCount);
        case ColRateDown:
            return QString::fromStdString(tgui::formatRate(displayedRates(t).first));
        case ColRateUp:
            return QString::fromStdString(tgui::formatRate(displayedRates(t).second));
        case ColEta:
            if (!t.eta)
                return QStringLiteral("—");
            return t.eta.value() >= 0 ? QString::fromStdString(tgui::formatEta(t.eta.value()))
                                      : QStringLiteral("∞");
        case ColRatio: {
            const double r = t.uploadRatio.value_or(-1.0);
            if (r <= -1.0)
                return QStringLiteral("—");
            return QString::fromStdString(tgui::formatRatio(r));
        }
        case ColDownloaded:
            return t.downloadedEver
                       ? QString::fromStdString(tgui::formatSize(t.downloadedEver.value()))
                       : QStringLiteral("—");
        case ColUploaded:
            return t.uploadedEver
                       ? QString::fromStdString(tgui::formatSize(t.uploadedEver.value()))
                       : QStringLiteral("—");
        case ColTracker:
            return t.trackerStats.isEmpty() ? QStringLiteral("—")
                                            : t.trackerStats.first().announce;
        case ColTrackerStatus:
            return trackerStatusText(t);
        case ColAddedOn:
            return fmtTime(t.addedDate.value_or(0));
        case ColCompletedOn:
            return fmtTime(t.doneDate.value_or(0));
        case ColLastActive:
            return fmtTime(t.activityDate.value_or(0));
        case ColPath:
            return t.downloadDir.value_or(QStringLiteral("—"));
        case ColPriority:
            return priorityText(t.bandwidthPriority.value_or(0));
        case ColSizeToDownload:
            return t.sizeWhenDone
                       ? QString::fromStdString(tgui::formatSize(t.sizeWhenDone.value()))
                       : QStringLiteral("—");
        case ColId:
            return t.id;
        case ColQueuePosition:
            return t.queuePosition ? QVariant(t.queuePosition.value())
                                   : QStringLiteral("—");
        case ColSeedingTime:
            return fmtDuration(t.secondsSeeding.value_or(0));
        case ColSizeLeft:
            return QString::fromStdString(tgui::formatSize(t.leftUntilDone.value_or(0.0)));
        case ColPrivate:
            return t.isPrivate ? QObject::tr("yes") : QObject::tr("no");
        case ColLabels:
            return t.labels.value_or(QStringList()).join(QStringLiteral(", "));
    }
    return {};
}

QVariant TorrentModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::TextAlignmentRole)
        return columnAlignment(section);
    if (role != Qt::DisplayRole)
        return {};
    // Заголовки 1:1 с референсом (Title.Caption в main.lfm).
    switch (section) {
        case ColName:
            return QObject::tr("Name");
        case ColSize:
            return QObject::tr("Size");
        case ColDone:
            return QObject::tr("Done");
        case ColStatus:
            return QObject::tr("Status");
        case ColSeeds:
            return QObject::tr("Seeds");
        case ColPeers:
            return QObject::tr("Peers");
        case ColRateDown:
            return QObject::tr("Down speed");
        case ColRateUp:
            return QObject::tr("Up speed");
        case ColEta:
            return QStringLiteral("ETA");
        case ColRatio:
            return QObject::tr("Ratio");
        case ColDownloaded:
            return QObject::tr("Downloaded");
        case ColUploaded:
            return QObject::tr("Uploaded");
        case ColTracker:
            return QObject::tr("Tracker");
        case ColTrackerStatus:
            return QObject::tr("Tracker status");
        case ColAddedOn:
            return QObject::tr("Added on");
        case ColCompletedOn:
            return QObject::tr("Completed on");
        case ColLastActive:
            return QObject::tr("Last active");
        case ColPath:
            return QObject::tr("Path");
        case ColPriority:
            return QObject::tr("Priority");
        case ColSizeToDownload:
            return QObject::tr("Size to download");
        case ColId:
            return QStringLiteral("ID");
        case ColQueuePosition:
            return QObject::tr("Queue position");
        case ColSeedingTime:
            return QObject::tr("Seeding time");
        case ColSizeLeft:
            return QObject::tr("Size left");
        case ColPrivate:
            return QObject::tr("Private");
        case ColLabels:
            return QObject::tr("Labels");
    }
    return {};
}

QVariant TorrentModel::sortKey(const tgui::Torrent& t, int column)
{
    switch (column) {
        case ColName:
            return t.name.value_or(QString());
        case ColSize:
            return t.totalSize.value_or(t.sizeWhenDone.value_or(0.0));
        case ColDone:
            if (isVerifying(t))
                return verifyingPercent(t);
            if (t.sizeWhenDone && t.sizeWhenDone.value() > 0.0)
                return tgui::percentComplete(t.sizeWhenDone.value(),
                                             t.leftUntilDone.value_or(0.0));
            return t.metadataPercentComplete.value_or(0.0) * 100.0;
        case ColStatus:
            return t.status.value_or(-1);
        case ColSeeds:
            return t.peersSendingToUs.value_or(0);
        case ColPeers:
            return t.peersGettingFromUs.value_or(0);
        case ColRateDown:
            return t.rateDownload.value_or(0);
        case ColRateUp:
            return t.rateUpload.value_or(0);
        case ColEta:
            if (t.eta && t.eta.value() >= 0)
                return t.eta.value();
            return 2147483647; // ∞ — в конец
        case ColRatio:
            return t.uploadRatio.value_or(-1.0);
        case ColDownloaded:
            return t.downloadedEver.value_or(0.0);
        case ColUploaded:
            return t.uploadedEver.value_or(0.0);
        case ColTracker:
            return t.trackerStats.isEmpty() ? QString() : t.trackerStats.first().announce;
        case ColTrackerStatus:
            return trackerStatusText(t);
        case ColAddedOn:
            return t.addedDate.value_or(0);
        case ColCompletedOn:
            return t.doneDate.value_or(0);
        case ColLastActive:
            return t.activityDate.value_or(0);
        case ColPath:
            return t.downloadDir.value_or(QString());
        case ColPriority:
            return t.bandwidthPriority.value_or(0);
        case ColSizeToDownload:
            return t.sizeWhenDone.value_or(0.0);
        case ColId:
            return t.id;
        case ColQueuePosition:
            return t.queuePosition.value_or(0);
        case ColSeedingTime:
            return t.secondsSeeding.value_or(0);
        case ColSizeLeft:
            return t.leftUntilDone.value_or(0.0);
        case ColPrivate:
            return t.isPrivate.value_or(false) ? 1 : 0;
        case ColLabels:
            return t.labels.value_or(QStringList()).join(QStringLiteral(", "));
    }
    return {};
}

void TorrentModel::sort(int column, Qt::SortOrder order)
{
    emit layoutAboutToBeChanged();
    auto cmp = [column, order](const tgui::Torrent& a, const tgui::Torrent& b) {
        const QVariant va = TorrentModel::sortKey(a, column);
        const QVariant vb = TorrentModel::sortKey(b, column);
        int c = 0;
        if (va.canConvert<double>() && vb.canConvert<double>()) {
            const double da = va.toDouble();
            const double db = vb.toDouble();
            c = (da < db ? -1 : da > db ? 1 : 0);
        } else {
            c = QString::localeAwareCompare(va.toString(), vb.toString());
        }
        if (c == 0)
            c = (a.id < b.id ? -1 : a.id > b.id ? 1 : 0); // стабильный порядок
        return order == Qt::AscendingOrder ? c < 0 : c > 0;
    };
    std::stable_sort(m_torrents.begin(), m_torrents.end(), cmp);
    emit layoutChanged();
}

// --- TorrentFilterProxy ---

TorrentFilterProxy::TorrentFilterProxy(QObject* parent) : QSortFilterProxyModel(parent)
{
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
}

/** Обновляет фильтр строк через API установленной версии Qt. */
template<typename T>
void TorrentFilterProxy::setFilterValue(T& current, const T& value)
{
    if (current == value)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    current = value;
    endFilterChange(Direction::Rows);
#else
    current = value;
    invalidateRowsFilter();
#endif
}

void TorrentFilterProxy::setStatusGroup(int group)
{
    setFilterValue(m_group, group);
}

void TorrentFilterProxy::setFolderFilter(const QString& folder)
{
    setFilterValue(m_folder, folder);
}

void TorrentFilterProxy::setTrackerFilter(const QString& announce)
{
    setFilterValue(m_tracker, announce);
}

void TorrentFilterProxy::setLabelFilter(const QString& label, bool withoutLabels)
{
    std::optional<QString> filter;
    if (withoutLabels)
        filter.emplace();
    else if (!label.isEmpty())
        filter = label;
    setFilterValue(m_label, filter);
}

void TorrentFilterProxy::setSearchText(const QString& text)
{
    setFilterValue(m_search, text);
}

const tgui::Torrent& TorrentFilterProxy::torrentAt(int row) const
{
    auto* model = qobject_cast<const TorrentModel*>(sourceModel());
    return model->torrentAt(mapToSource(index(row, 0)).row());
}

bool TorrentFilterProxy::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    auto* model = qobject_cast<const TorrentModel*>(sourceModel());
    if (!model || row < 0 || row >= model->rowCount())
        return false;
    const tgui::Torrent& t = model->torrentAt(row);

    if (m_group != TorrentModel::GroupAll && !TorrentModel::matchesGroup(t, m_group))
        return false;
    if (!m_folder.isEmpty() && t.downloadDir.value_or(QString()) != m_folder)
        return false;
    if (m_label) {
        const bool matchesLabel = m_label->isEmpty()
            ? !t.labels || t.labels->isEmpty()
            : t.labels && t.labels->contains(*m_label);
        if (!matchesLabel)
            return false;
    }
    if (!m_tracker.isEmpty() &&
        (t.trackerStats.isEmpty() || t.trackerStats.first().announce != m_tracker))
        return false;
    if (!m_search.isEmpty() &&
        !t.name.value_or(QString()).contains(m_search, Qt::CaseInsensitive))
        return false;
    return true;
}
