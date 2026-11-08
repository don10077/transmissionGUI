#include "torrentmodel.h"

#include <algorithm>

#include "tgui/util.h"

TorrentModel::TorrentModel(QObject* parent) : QAbstractTableModel(parent) {}

void TorrentModel::setTorrents(const QVector<tgui::Torrent>& torrents)
{
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

QString TorrentModel::statusText(int status)
{
    switch (status) {
        case 0:
            return QObject::tr("Остановлен");
        case 1:
            return QObject::tr("Ожидание проверки");
        case 2:
            return QObject::tr("Проверка");
        case 3:
            return QObject::tr("Ожидание загрузки");
        case 4:
            return QObject::tr("Загрузка");
        case 5:
            return QObject::tr("Ожидание раздачи");
        case 6:
            return QObject::tr("Раздача");
    }
    return QString();
}

int TorrentModel::statusGroup(int status, double sizeWhenDone, double leftUntilDone)
{
    if (status == 0)
        return GroupStopped;
    if (status == 1 || status == 2 || status == 3 || status == 5)
        return GroupWaiting;
    if (status == 4)
        return GroupDownloading;
    if (status == 6)
        return tgui::statusIsFinished(status, sizeWhenDone, leftUntilDone)
                   ? GroupFinished
                   : GroupSeeding;
    return GroupWaiting;
}

QVariant TorrentModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_torrents.size())
        return {};
    const auto& t = m_torrents.at(index.row());
    const int col = index.column();

    if (role == StatusGroupRole)
        return statusGroup(t.status.value_or(-1), t.sizeWhenDone.value_or(0.0),
                           t.leftUntilDone.value_or(0.0));
    if (role == Qt::TextAlignmentRole && col >= ColProgress && col <= ColRatio)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};

    switch (col) {
        case ColName:
            return t.name.value_or(QString());
        case ColStatus:
            if (tgui::statusIsFinished(t.status.value_or(-1), t.sizeWhenDone.value_or(0.0),
                                       t.leftUntilDone.value_or(0.0)))
                return QObject::tr("Завершено");
            return statusText(t.status.value_or(-1));
        case ColProgress: {
            if (t.sizeWhenDone && t.sizeWhenDone.value() > 0.0)
                return QString::number(
                           tgui::percentComplete(t.sizeWhenDone.value(),
                                                 t.leftUntilDone.value_or(0.0)),
                           'f', 0) +
                       "%";
            if (t.metadataPercentComplete)
                return QObject::tr("метаданные %1%")
                    .arg(t.metadataPercentComplete.value() * 100.0, 0, 'f', 0);
            return QString();
        }
        case ColSize:
            if (t.sizeWhenDone)
                return QString::fromStdString(tgui::formatSize(t.sizeWhenDone.value()));
            if (t.totalSize)
                return QString::fromStdString(tgui::formatSize(t.totalSize.value()));
            return QString();
        case ColRateDown:
            if (t.rateDownload)
                return QString::fromStdString(tgui::formatRate(t.rateDownload.value()));
            return QString();
        case ColRateUp:
            if (t.rateUpload)
                return QString::fromStdString(tgui::formatRate(t.rateUpload.value()));
            return QString();
        case ColEta:
            if (t.eta)
                return QString::fromStdString(tgui::formatEta(t.eta.value()));
            return QString();
        case ColPeers: {
            const int a = t.peersSendingToUs.value_or(0);
            const int b = t.peersGettingFromUs.value_or(0);
            return a + b > 0 ? QString::number(a) + '+' + QString::number(b) : QString();
        }
        case ColRatio:
            if (t.uploadRatio)
                return QString::fromStdString(tgui::formatRatio(t.uploadRatio.value()));
            return QString();
        case ColLabels:
            return t.labels.value_or(QStringList()).join(QStringLiteral(", "));
    }
    return {};
}

QVariant TorrentModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
        case ColName:
            return QObject::tr("Имя");
        case ColStatus:
            return QObject::tr("Статус");
        case ColProgress:
            return QObject::tr("Прогресс");
        case ColSize:
            return QObject::tr("Размер");
        case ColRateDown:
            return QObject::tr("Загрузка");
        case ColRateUp:
            return QObject::tr("Отдача");
        case ColEta:
            return QObject::tr("Осталось");
        case ColPeers:
            return QObject::tr("Пиры");
        case ColRatio:
            return QObject::tr("Рейтинг");
        case ColLabels:
            return QObject::tr("Метки");
    }
    return {};
}

QVariant TorrentModel::sortKey(const tgui::Torrent& t, int column)
{
    switch (column) {
        case ColName:
            return t.name.value_or(QString());
        case ColStatus:
            return t.status.value_or(-1);
        case ColProgress:
            if (t.sizeWhenDone && t.sizeWhenDone.value() > 0.0)
                return tgui::percentComplete(t.sizeWhenDone.value(),
                                             t.leftUntilDone.value_or(0.0));
            return t.metadataPercentComplete.value_or(0.0) * 100.0;
        case ColSize:
            if (t.sizeWhenDone)
                return t.sizeWhenDone.value();
            return t.totalSize.value_or(0.0);
        case ColRateDown:
            return t.rateDownload.value_or(0);
        case ColRateUp:
            return t.rateUpload.value_or(0);
        case ColEta:
            if (t.eta && t.eta.value() >= 0)
                return t.eta.value();
            return 2147483647; // ∞ — в конец
        case ColPeers:
            return t.peersSendingToUs.value_or(0) + t.peersGettingFromUs.value_or(0);
        case ColRatio:
            return t.uploadRatio.value_or(-1.0);
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

void TorrentFilterProxy::setStatusGroup(int group)
{
    if (m_group == group)
        return;
    beginFilterChange();
    m_group = group;
    endFilterChange();
}

void TorrentFilterProxy::setSearchText(const QString& text)
{
    if (m_search == text)
        return;
    beginFilterChange();
    m_search = text;
    endFilterChange();
}

const tgui::Torrent& TorrentFilterProxy::torrentAt(int row) const
{
    auto* model = qobject_cast<const TorrentModel*>(sourceModel());
    return model->torrentAt(mapToSource(index(row, 0)).row());
}

bool TorrentFilterProxy::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    const QModelIndex src = sourceModel()->index(row, TorrentModel::ColStatus);
    if (m_group >= 0 && src.data(TorrentModel::StatusGroupRole).toInt() != m_group)
        return false;
    if (!m_search.isEmpty() &&
        !sourceModel()
             ->index(row, TorrentModel::ColName)
             .data(Qt::DisplayRole)
             .toString()
             .contains(m_search, Qt::CaseInsensitive))
        return false;
    return true;
}