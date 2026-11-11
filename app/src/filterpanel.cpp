#include "filterpanel.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "theme.h"
#include "torrentmodel.h"

namespace {
constexpr int kRoleGroup = Qt::UserRole + 1; // TorrentModel::StatusGroup
constexpr int kRoleFolder = Qt::UserRole + 2;
constexpr int kRoleTracker = Qt::UserRole + 3;
constexpr int kRoleBase = Qt::UserRole + 4; // базовое имя строки (без счётчика)
constexpr int kRoleCount = Qt::UserRole + 5;
constexpr int kRoleSection = Qt::UserRole + 6;
constexpr int kRoleLabel = Qt::UserRole + 7;

tgui::theme::Icon statusIcon(int group)
{
    switch (group) {
        case TorrentModel::GroupDownloading:
            return tgui::theme::Icon::FilterDownloading;
        case TorrentModel::GroupCompleted:
            return tgui::theme::Icon::FilterCompleted;
        case TorrentModel::GroupActive:
            return tgui::theme::Icon::FilterActive;
        case TorrentModel::GroupInactive:
            return tgui::theme::Icon::FilterInactive;
        case TorrentModel::GroupStopped:
            return tgui::theme::Icon::FilterStopped;
        case TorrentModel::GroupError:
            return tgui::theme::Icon::FilterError;
        case TorrentModel::GroupWaiting:
            return tgui::theme::Icon::FilterWaiting;
        default:
            return tgui::theme::Icon::FilterAll;
    }
}

// Строка панели фильтров: иконка, название и счётчик-бейдж справа.
// Секции «Folders»/«Trackers» рисуются как заголовки с разделителем.
class FilterTreeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        using namespace tgui::theme;
        const QRect r = option.rect;
        if (!r.isValid())
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        if (index.data(kRoleSection).toBool()) {
            QFont f = option.font;
            f.setBold(true);
            f.setPointSizeF(qMax(7.5, f.pointSizeF() - 1.0));
            painter->setFont(f);
            painter->setPen(color::text);
            const QRect textRect = r.adjusted(10, 6, -10, -4);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              index.data(Qt::DisplayRole).toString().toUpper());
            painter->setPen(color::borderSoft);
            painter->drawLine(r.left() + 10, r.bottom() - 1, r.right() - 10,
                              r.bottom() - 1);
            painter->restore();
            return;
        }

        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRect row = r.adjusted(3, 1, -3, -1);

        painter->setPen(Qt::NoPen);
        if (selected) {
            painter->setBrush(color::accent);
            painter->drawRoundedRect(row, 2, 2);
        } else if (hovered) {
            painter->setBrush(color::accentLight);
            painter->drawRoundedRect(row, 2, 2);
        }

        QIcon ic;
        const QVariant folder = index.data(kRoleFolder);
        const QVariant tracker = index.data(kRoleTracker);
        const QVariant labelValue = index.data(kRoleLabel);
        if (folder.isValid())
            ic = tgui::theme::icon(Icon::Folder);
        else if (tracker.isValid())
            ic = tgui::theme::icon(Icon::Tracker);
        else if (labelValue.isValid())
            ic = tgui::theme::icon(Icon::Properties);
        else
            ic = tgui::theme::icon(statusIcon(index.data(kRoleGroup).toInt()));

        const QRect iconRect(row.left() + 7, row.top() + (row.height() - 16) / 2, 16, 16);
        ic.paint(painter, iconRect, Qt::AlignCenter,
                 selected ? QIcon::Selected : QIcon::Normal);

        const int count = index.data(kRoleCount).toInt();
        const QString countText = QString::number(count);
        QFont countFont = option.font;
        countFont.setBold(true);
        countFont.setPointSizeF(qMax(7.5, option.font.pointSizeF() - 0.5));
        const int badgeW = QFontMetrics(countFont).horizontalAdvance(countText) + 16;
        const QRect badge(row.right() - badgeW - 3, row.top() + (row.height() - 18) / 2,
                          badgeW, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? QColor(255, 255, 255, 55) : QColor(0xEC, 0xEF, 0xF3));
        painter->drawRoundedRect(badge, 9, 9);

        QString label = index.data(kRoleBase).toString();
        if (label.isEmpty())
            label = index.data(Qt::DisplayRole).toString();

        QFont f = option.font;
        if (index.data(kRoleGroup).toInt() == TorrentModel::GroupAll)
            f.setBold(true);
        painter->setFont(f);
        painter->setPen(selected ? QColor(Qt::white) : color::text);
        QRect textRect(iconRect.right() + 7, row.top(), badge.left() - iconRect.right() - 13,
                       row.height());
        const QString elided =
            QFontMetrics(f).elidedText(label, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

        painter->setFont(countFont);
        painter->setPen(selected ? QColor(Qt::white) : color::textMuted);
        painter->drawText(badge, Qt::AlignCenter, countText);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(index.data(kRoleSection).toBool() ? 30 : 25);
        return size;
    }
};

} // namespace

FilterPanel::FilterPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("filterPanel"));

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("filterTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(false);
    m_tree->setMinimumWidth(190);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setMouseTracking(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setItemDelegate(new FilterTreeDelegate(m_tree));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(m_tree, 1);

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
        it->setData(0, kRoleCount, 0);
        it->setToolTip(0, tr(s.name));
    }

    m_foldersSection = new QTreeWidgetItem(m_tree);
    m_foldersSection->setText(0, tr("Folders"));
    m_foldersSection->setData(0, kRoleSection, true);
    m_foldersSection->setFlags(Qt::ItemIsEnabled);
    m_labelsSection = new QTreeWidgetItem(m_tree);
    m_labelsSection->setText(0, tr("Labels"));
    m_labelsSection->setData(0, kRoleSection, true);
    m_labelsSection->setFlags(Qt::ItemIsEnabled);
    m_trackersSection = new QTreeWidgetItem(m_tree);
    m_trackersSection->setText(0, tr("Trackers"));
    m_trackersSection->setData(0, kRoleSection, true);
    m_trackersSection->setFlags(Qt::ItemIsEnabled);

    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
                if (!cur || cur->data(0, kRoleSection).toBool())
                    return;
                m_selStatus = cur->data(0, kRoleGroup).toInt();
                m_selFolder = cur->data(0, kRoleFolder).toString();
                m_selLabel = cur->data(0, kRoleLabel).toString();
                m_selTracker = cur->data(0, kRoleTracker).toString();
                const bool plainStatus = m_selFolder.isEmpty() && m_selLabel.isEmpty() &&
                                         m_selTracker.isEmpty();
                emit filterChanged(plainStatus ? m_selStatus : TorrentModel::GroupAll,
                                   m_selFolder, m_selTracker, m_selLabel);
            });

    // По умолчанию выбран «All torrents» — как в эталоне.
    if (QTreeWidgetItem* first = itemAt(0))
        m_tree->setCurrentItem(first);
}

QTreeWidgetItem* FilterPanel::itemAt(int row) const
{
    return m_tree->topLevelItem(row);
}

int FilterPanel::selectedGroup() const
{
    if (!m_selFolder.isEmpty() || !m_selLabel.isEmpty() || !m_selTracker.isEmpty())
        return TorrentModel::GroupAll;
    return m_selStatus;
}

QString FilterPanel::selectedFolder() const { return m_selFolder; }
QString FilterPanel::selectedLabel() const { return m_selLabel; }
QString FilterPanel::selectedTracker() const { return m_selTracker; }

void FilterPanel::selectAllTorrents()
{
    if (QTreeWidgetItem* first = itemAt(0))
        m_tree->setCurrentItem(first);
}

void FilterPanel::setFolderGroupingEnabled(bool enabled)
{
    if (m_foldersSection)
        m_foldersSection->setHidden(!enabled);
}

void FilterPanel::setLabelGroupingEnabled(bool enabled)
{
    if (!enabled && !m_selLabel.isEmpty())
        selectAllTorrents();
    if (m_labelsSection)
        m_labelsSection->setHidden(!enabled);
}

void FilterPanel::setTrackerGroupingEnabled(bool enabled)
{
    if (m_trackersSection)
        m_trackersSection->setHidden(!enabled);
}

void FilterPanel::setEnabledUi(bool enabled)
{
    m_tree->setEnabled(enabled);
}

void FilterPanel::setCounts(int all, int downloading, int completed, int active,
                            int inactive, int stopped, int error, int waiting,
                            const QVector<QPair<QString, int>>& folders,
                            const QVector<QPair<QString, int>>& labels,
                            const QVector<QPair<QString, int>>& trackers)
{
    const int scrollPosition = m_tree->verticalScrollBar()->value();
    const int counts[] = {all, downloading, completed, active, inactive,
                          stopped, error, waiting};
    for (int i = 0; i < 8; ++i) {
        if (QTreeWidgetItem* it = itemAt(i)) {
            it->setData(0, kRoleCount, counts[i]);
            it->setText(0, QStringLiteral("%1 (%2)")
                               .arg(it->data(0, kRoleBase).toString())
                               .arg(counts[i]));
        }
    }

    auto fillSection = [this](QTreeWidgetItem* section,
                              const QVector<QPair<QString, int>>& items,
                              int role) {
        if (!section)
            return;
        // Reuse rows by their key so an RPC refresh retains the selection.
        int rowIndex = 0;
        for (const auto& [name, count] : items) {
            QTreeWidgetItem* it = nullptr;
            for (int i = rowIndex; i < section->childCount(); ++i) {
                if (section->child(i)->data(0, role).toString() == name) {
                    it = section->child(i);
                    if (i != rowIndex) {
                        section->takeChild(i);
                        section->insertChild(rowIndex, it);
                    }
                    break;
                }
            }
            if (!it) {
                it = new QTreeWidgetItem;
                section->insertChild(rowIndex, it);
            }
            ++rowIndex;
            it->setText(0, QStringLiteral("%1 (%2)").arg(name).arg(count));
            it->setData(0, kRoleBase, name);
            it->setData(0, kRoleCount, count);
            it->setData(0, role, name);
            it->setToolTip(0, name);
        }
        while (section->childCount() > rowIndex)
            delete section->takeChild(rowIndex);
        // Секции всегда раскрыты: стрелки у заголовков скрыты (rootIsDecorated
        // = false), поэтому свёрнутое состояние сделало бы строки недоступными.
        section->setExpanded(!items.isEmpty());
    };
    fillSection(m_foldersSection, folders, kRoleFolder);
    if (!m_selLabel.isEmpty() &&
        std::none_of(labels.cbegin(), labels.cend(),
                     [this](const auto& label) { return label.first == m_selLabel; }))
        selectAllTorrents();
    fillSection(m_labelsSection, labels, kRoleLabel);
    fillSection(m_trackersSection, trackers, kRoleTracker);
    m_tree->doItemsLayout();
    m_tree->verticalScrollBar()->setValue(scrollPosition);
}
