#include "filterpanel.h"

#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "torrentmodel.h"

namespace {
constexpr int kRoleGroup = Qt::UserRole + 1; // TorrentModel::StatusGroup
constexpr int kRoleFolder = Qt::UserRole + 2;
constexpr int kRoleTracker = Qt::UserRole + 3;
constexpr int kRoleBase = Qt::UserRole + 4; // базовое имя строки (без счётчика)
} // namespace

FilterPanel::FilterPanel(QWidget* parent) : QWidget(parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setMinimumWidth(170);
    m_tree->setFocusPolicy(Qt::NoFocus);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_search);

    // Статусные строки (порядок — как в референсе, lvFilter.Rows[0..7]).
    const struct {
        int group;
        const char* name;
    } status[] = {
        {TorrentModel::GroupAll, "All torrents"},
        {TorrentModel::GroupDownloading, "Downloading"},
        {TorrentModel::GroupCompleted, "Completed"},
        {TorrentModel::GroupActive, "Active"},
        {TorrentModel::GroupInactive, "Inactive"},
        {TorrentModel::GroupStopped, "Stopped"},
        {TorrentModel::GroupError, "Error"},
        {TorrentModel::GroupWaiting, "Waiting"},
    };
    for (const auto& s : status) {
        auto* it = new QTreeWidgetItem(m_tree);
        it->setText(0, tr(s.name));
        it->setData(0, kRoleGroup, s.group);
        it->setData(0, kRoleBase, tr(s.name));
    }

    auto* foldersSection = new QTreeWidgetItem(m_tree);
    foldersSection->setText(0, tr("Folders:"));
    auto* trackersSection = new QTreeWidgetItem(m_tree);
    trackersSection->setText(0, tr("Trackers:"));

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
                if (!cur)
                    return;
                m_selStatus = cur->data(0, kRoleGroup).toInt();
                m_selFolder = cur->data(0, kRoleFolder).toString();
                m_selTracker = cur->data(0, kRoleTracker).toString();
                const bool plainStatus =
                    m_selFolder.isEmpty() && m_selTracker.isEmpty();
                emit filterChanged(plainStatus ? m_selStatus : TorrentModel::GroupAll,
                                   m_selFolder, m_selTracker);
            });
}

QTreeWidgetItem* FilterPanel::itemAt(int row) const
{
    return m_tree->topLevelItem(row);
}

int FilterPanel::selectedGroup() const
{
    if (!m_selFolder.isEmpty() || !m_selTracker.isEmpty())
        return TorrentModel::GroupAll;
    return m_selStatus;
}

QString FilterPanel::selectedFolder() const { return m_selFolder; }
QString FilterPanel::selectedTracker() const { return m_selTracker; }

void FilterPanel::setFolderGroupingEnabled(bool enabled)
{
    if (QTreeWidgetItem* it = itemAt(8))
        it->setHidden(!enabled);
}

void FilterPanel::setTrackerGroupingEnabled(bool enabled)
{
    if (QTreeWidgetItem* it = itemAt(9))
        it->setHidden(!enabled);
}

void FilterPanel::setEnabledUi(bool enabled)
{
    m_tree->setEnabled(enabled);
    m_search->setEnabled(enabled);
}

void FilterPanel::setCounts(int all, int downloading, int completed, int active,
                            int inactive, int stopped, int error, int waiting,
                            const QVector<QPair<QString, int>>& folders,
                            const QVector<QPair<QString, int>>& trackers)
{
    const int counts[] = {all, downloading, completed, active, inactive,
                          stopped, error, waiting};
    for (int i = 0; i < 8; ++i) {
        if (QTreeWidgetItem* it = itemAt(i)) {
            it->setText(0, QStringLiteral("%1 (%2)").arg(it->data(0, kRoleBase).toString()).arg(counts[i]));
        }
    }

    auto fillSection = [this](int row, const QVector<QPair<QString, int>>& items,
                              int role) {
        QTreeWidgetItem* section = itemAt(row);
        if (!section)
            return;
        while (section->childCount() > 0)
            delete section->takeChild(0);
        for (const auto& [name, count] : items) {
            auto* it = new QTreeWidgetItem(section);
            it->setText(0, QStringLiteral("%1 (%2)").arg(name).arg(count));
            it->setData(0, role, name);
        }
    };
    fillSection(8, folders, kRoleFolder);
    fillSection(9, trackers, kRoleTracker);
}