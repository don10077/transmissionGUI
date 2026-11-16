#include "filterpanel.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "theme.h"
#include "torrentlabels.h"
#include "torrentmodel.h"

namespace {
constexpr int kRoleGroup = Qt::UserRole + 1; // TorrentModel::StatusGroup
constexpr int kRoleFolder = Qt::UserRole + 2;
constexpr int kRoleTracker = Qt::UserRole + 3;
constexpr int kRoleBase = Qt::UserRole + 4; // базовое имя строки (без счётчика)
constexpr int kRoleCount = Qt::UserRole + 5;
constexpr int kRoleSection = Qt::UserRole + 6;
constexpr int kRoleLabel = Qt::UserRole + 7;
constexpr int kRoleCollapsed = Qt::UserRole + 8;

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

/** Подбирает шрифт заголовка относительно текущего масштаба интерфейса. */
QFont sectionFont(QFont font)
{
    font.setWeight(QFont::DemiBold);
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * 11.0 / 12.0);
    else
        font.setPixelSize(std::max(1, qRound(font.pixelSize() * 11.0 / 12.0)));
    return font;
}

/** Рисует фильтры и заголовки секций по макету. */
class FilterTreeDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /** Рисует строку с названием и отдельным счётчиком или заголовок секции. */
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        if (!option.rect.isValid())
            return;

        painter->save();
        painter->setClipRect(option.rect, Qt::IntersectClip);
        if (index.data(kRoleSection).toBool())
            paintSection(*painter, option, index);
        else
            paintFilter(*painter, option, index);
        painter->restore();
    }

    /** Возвращает высоту, достаточную для шрифта и иконки. */
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override
    {
        const int height = index.data(kRoleSection).toBool()
            ? std::max(15, QFontMetrics(sectionFont(option.font)).height()) + 20
            : tgui::theme::rowHeightFor(option.font);
        return QSize(0, height);
    }

private:
    /** Рисует верхний разделитель, стрелку и заголовок секции. */
    static void paintSection(QPainter& painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index)
    {
        const auto& palette = tgui::theme::palette();
        const QRect rect = option.rect.adjusted(4, 8, -4, -4);
        painter.setPen(palette.borderSoft);
        painter.drawLine(rect.topLeft(), rect.topRight());

        const QRect content = rect.adjusted(6, 8, -6, 0);
        const int x = content.left() + 5;
        const int y = content.center().y();
        const bool collapsed = index.data(kRoleCollapsed).toBool();
        const QPoint right[]{QPoint(x - 2, y - 3), QPoint(x - 2, y + 3), QPoint(x + 2, y)};
        const QPoint down[]{QPoint(x - 3, y - 2), QPoint(x + 3, y - 2), QPoint(x, y + 2)};
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.textMuted);
        painter.drawPolygon(collapsed ? right : down, 3);

        const QFont font = sectionFont(option.font);
        painter.setFont(font);
        painter.setPen(palette.textMuted);
        const QRect titleRect = content.adjusted(14, 0, 0, 0);
        const QString title = QLocale().toUpper(index.data(Qt::DisplayRole).toString());
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(font).elidedText(title, Qt::ElideRight, titleRect.width()));
    }

    /** Рисует иконку, сокращаемое имя и выровненный справа счётчик. */
    static void paintFilter(QPainter& painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index)
    {
        using namespace tgui::theme;
        const auto& colors = palette();
        const QRect row = option.rect.adjusted(2, 0, -2, 0);
        const bool selected = option.state & QStyle::State_Selected;
        if (selected) {
            painter.setPen(colors.selectionBorder);
            painter.setBrush(colors.selBg);
            painter.drawRect(QRectF(row).adjusted(0.5, 0.5, -0.5, -0.5));
        } else if (option.state & QStyle::State_MouseOver) {
            painter.fillRect(row, colors.hoverBg);
        }

        QIcon glyph;
        if (index.data(kRoleFolder).isValid())
            glyph = icon(Icon::Folder);
        else if (index.data(kRoleTracker).isValid())
            glyph = icon(Icon::Tracker);
        else if (const auto label = index.data(kRoleLabel); label.isValid())
            glyph = torrentlabels::labelIcon(label.toString());
        else
            glyph = icon(statusIcon(index.data(kRoleGroup).toInt()));

        const QRect iconRect(row.left() + 11, row.top() + (row.height() - 16) / 2, 16, 16);
        glyph.paint(&painter, iconRect);

        const QFontMetrics metrics(option.font);
        const QString count = QString::number(index.data(kRoleCount).toInt());
        const int countWidth = metrics.horizontalAdvance(count);
        const QRect countRect(row.right() - 8 - countWidth, row.top(), countWidth, row.height());
        painter.setFont(option.font);
        painter.setPen(colors.textMuted);
        painter.drawText(countRect, Qt::AlignRight | Qt::AlignVCenter, count);

        const int textLeft = iconRect.right() + 7;
        const int textWidth = std::max(0, countRect.left() - 12 - textLeft);
        const QRect textRect(textLeft, row.top(), textWidth, row.height());
        painter.setPen(colors.text);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         metrics.elidedText(index.data(kRoleBase).toString(), Qt::ElideRight, textWidth));
    }
};

} // namespace

FilterPanel::FilterPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("filterPanel"));
    setAttribute(Qt::WA_StyledBackground);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("filterTree"));
    m_tree->setHeaderHidden(true);
    m_tree->header()->setSectionResizeMode(QHeaderView::Stretch);
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(0);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setUniformRowHeights(false);
    setMinimumWidth(120);
    setMaximumWidth(480);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setMouseTracking(true);
    m_tree->viewport()->setAttribute(Qt::WA_Hover);
    m_tree->viewport()->installEventFilter(this);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setItemDelegate(new FilterTreeDelegate(m_tree));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 3, 1, 3);
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
                const QVariant label = cur->data(0, kRoleLabel);
                m_selLabel = label.toString();
                m_withoutLabels = label.isValid() && m_selLabel.isEmpty();
                m_selTracker = cur->data(0, kRoleTracker).toString();
                const bool plainStatus = m_selFolder.isEmpty() && m_selLabel.isEmpty() &&
                                         m_selTracker.isEmpty() && !m_withoutLabels;
                emit filterChanged(plainStatus ? m_selStatus : TorrentModel::GroupAll,
                                   m_selFolder, m_selTracker, m_selLabel, m_withoutLabels);
            });

    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        emit contextMenuRequested(m_tree->viewport()->mapToGlobal(point));
    });

    // По умолчанию выбран «All torrents» — как в эталоне.
    if (QTreeWidgetItem* first = itemAt(0))
        m_tree->setCurrentItem(first);
}

bool FilterPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_tree->viewport() && (event->type() == QEvent::MouseButtonPress ||
                                         event->type() == QEvent::MouseButtonDblClick)) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        auto* item = m_tree->itemAt(mouse->position().toPoint());
        if (mouse->button() == Qt::LeftButton && item && item->data(0, kRoleSection).toBool()) {
            if (event->type() == QEvent::MouseButtonPress) {
                const bool collapsed = !item->data(0, kRoleCollapsed).toBool();
                item->setData(0, kRoleCollapsed, collapsed);
                item->setExpanded(!collapsed && item->childCount() > 0);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FilterPanel::expandSections()
{
    for (auto* section : {m_foldersSection, m_labelsSection, m_trackersSection}) {
        section->setData(0, kRoleCollapsed, false);
        section->setExpanded(section->childCount() > 0);
    }
}

QTreeWidgetItem* FilterPanel::itemAt(int row) const
{
    return m_tree->topLevelItem(row);
}

int FilterPanel::selectedGroup() const
{
    if (!m_selFolder.isEmpty() || !m_selLabel.isEmpty() || !m_selTracker.isEmpty() || m_withoutLabels)
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
    if (!enabled && (!m_selLabel.isEmpty() || m_withoutLabels))
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
                        const bool wasCurrent = m_tree->currentItem() == it;
                        const QSignalBlocker blocker(m_tree);
                        section->takeChild(i);
                        section->insertChild(rowIndex, it);
                        if (wasCurrent)
                            m_tree->setCurrentItem(it);
                    }
                    break;
                }
            }
            if (!it) {
                it = new QTreeWidgetItem;
                section->insertChild(rowIndex, it);
            }
            ++rowIndex;
            const QString title = role == kRoleLabel && name.isEmpty() ? tr("No label") : name;
            it->setText(0, QStringLiteral("%1 (%2)").arg(title).arg(count));
            it->setData(0, kRoleBase, title);
            it->setData(0, kRoleCount, count);
            it->setData(0, role, name);
            it->setToolTip(0, title);
        }
        while (section->childCount() > rowIndex)
            delete section->takeChild(rowIndex);
        section->setExpanded(!items.isEmpty() && !section->data(0, kRoleCollapsed).toBool());
    };
    fillSection(m_foldersSection, folders, kRoleFolder);
    if ((!m_selLabel.isEmpty() || m_withoutLabels) &&
        std::none_of(labels.cbegin(), labels.cend(),
                     [this](const auto& label) { return label.first == m_selLabel; }))
        selectAllTorrents();
    fillSection(m_labelsSection, labels, kRoleLabel);
    fillSection(m_trackersSection, trackers, kRoleTracker);
    m_tree->doItemsLayout();
    m_tree->verticalScrollBar()->setValue(scrollPosition);
}
