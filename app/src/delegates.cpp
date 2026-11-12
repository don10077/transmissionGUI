#include "delegates.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionViewItem>

#include "detailmodels.h"
#include "theme.h"
#include "torrentlabels.h"
#include "torrentmodel.h"

#include <algorithm>

namespace {

/** Выбирает цвет полосы. Ошибка и проверка важнее процента загрузки. */
QColor progressColor(const QModelIndex& index)
{
    const auto& colors = tgui::theme::palette();
    if (index.data(TorrentModel::ErrorRole).toBool())
        return colors.progressRed;

    using tgui::TorrentStatus;
    switch (static_cast<TorrentStatus>(index.data(TorrentModel::StatusRole).toInt())) {
        case TorrentStatus::Download:
            return colors.blue;
        case TorrentStatus::Seed:
            return colors.progressGreen;
        case TorrentStatus::CheckWait:
        case TorrentStatus::Check:
        case TorrentStatus::DownloadWait:
        case TorrentStatus::SeedWait:
            return colors.progressAmber;
        default:
            return index.data(TorrentModel::FinishedRole).toBool()
                ? colors.progressGreen : colors.progressStopped;
    }
}

tgui::theme::Icon torrentStatusIcon(int status, bool finished, bool error)
{
    using tgui::theme::Icon;
    if (error)
        return Icon::StatusError;
    switch (status) {
        case 4: // Download
            return Icon::StatusDown;
        case 6: // Seed
            return Icon::StatusSeed;
        case 2: // Check (verifying)
            return Icon::StatusVerifying;
        case 1: // CheckWait
        case 3: // DownloadWait
        case 5: // SeedWait
            return Icon::StatusWaiting;
        default:
            break;
    }
    if (finished)
        return Icon::StatusDone;
    return Icon::StatusStopped;
}

} // namespace

/** Кэширует шрифты и размеры, зависящие от масштаба интерфейса. */
void TorrentDelegate::updateFonts(const QFont& font) const
{
    if (m_fontsReady && m_baseFont == font)
        return;

    m_fontsReady = true;
    m_baseFont = font;
    const auto scaledFont = [&font](qreal scale) {
        QFont result = font;
        if (font.pointSizeF() > 0)
            result.setPointSizeF(font.pointSizeF() * scale);
        else
            result.setPixelSize(std::max(1, qRound(font.pixelSize() * scale)));
        return result;
    };
    m_progressFont = scaledFont(11.0 / 12.0);
    m_labelFont = scaledFont(10.5 / 12.0);
    const auto digits = QStringLiteral("0123456789.%");
    m_progressHeight = std::max(15, QFontMetrics(m_progressFont).tightBoundingRect(digits).height() + 2);
    m_labelWidths.clear();
}

namespace {

/** Рисует общую шкалу прогресса с белым текстом на заливке. */
void drawProgressBar(QPainter& painter, const QRect& cell, double progress,
                     const QColor& fill, const QFont& font, int barHeight)
{
    const auto& colors = tgui::theme::palette();
    const QRect track(cell.left(), cell.top() + (cell.height() - barHeight) / 2,
                      cell.width(), barHeight);
    const QRect inner = track.adjusted(1, 1, -1, -1);
    if (!inner.isValid())
        return;

    const int fillWidth = qRound(inner.width() * std::min(progress, 100.0) / 100.0);
    const QRect chunk(inner.left(), inner.top(), fillWidth, inner.height());

    painter.fillRect(track, colors.progressBorder);
    painter.fillRect(inner, colors.progressBg);
    if (fillWidth > 0)
        painter.fillRect(chunk, fill);
    painter.setFont(font);

    const QString percent = QStringLiteral("%1%").arg(progress, 0, 'f', 1);
    painter.save();
    if (fillWidth < inner.width()) {
        painter.save();
        painter.setClipRect(QRect(inner.left() + fillWidth, inner.top(),
                                  inner.width() - fillWidth, inner.height()), Qt::IntersectClip);
        painter.setPen(colors.text);
        painter.drawText(inner, Qt::AlignCenter, percent);
        painter.restore();
    }
    if (fillWidth > 0) {
        painter.setClipRect(chunk, Qt::IntersectClip);
        painter.setPen(Qt::white);
        painter.drawText(inner, Qt::AlignCenter, percent);
    }
    painter.restore();
}

/** Рисует фон ячейки, в которой стандартный делегат заменён. */
void drawDetailCell(QPainter& painter, const QStyleOptionViewItem& option)
{
    const auto& colors = tgui::theme::palette();
    const QColor background = option.state.testFlag(QStyle::State_Selected) ? colors.selBg
        : option.state.testFlag(QStyle::State_MouseOver) ? colors.hoverBg : colors.panel;
    painter.fillRect(option.rect, background);
    painter.setPen(colors.listRowBorder);
    painter.drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
}

} // namespace

void TorrentDelegate::paintProgress(QPainter& painter, const QRect& cell, const QModelIndex& index) const
{
    const double progress = index.data(TorrentModel::ProgressRole).toDouble();
    if (!(progress >= 0.0)) {
        painter.setPen(tgui::theme::palette().textMuted);
        painter.drawText(cell, Qt::AlignCenter, QStringLiteral("—"));
        return;
    }
    drawProgressBar(painter, cell, progress, progressColor(index), m_progressFont, m_progressHeight);
}

/** Раскладывает метки в одну строку и показывает число не поместившихся. */
void TorrentDelegate::paintLabels(QPainter& painter, const QRect& cell, const QStringList& labels) const
{
    const auto& colors = tgui::theme::palette();
    constexpr int kIcon = 12;
    constexpr int kIconGap = 3;
    constexpr int kLabelGap = 8;
    const QFontMetrics metrics(m_labelFont);
    int x = cell.left();
    const auto overflowText = [](qsizetype count) {
        return QLatin1Char('+') + QString::number(count);
    };

    painter.setFont(m_labelFont);
    painter.setPen(colors.labelText);
    const auto drawLabel = [&](const QString& label, const QString& displayed, int textWidth) {
        torrentlabels::labelIcon(label).paint(&painter,
            QRect(x, cell.top() + (cell.height() - kIcon) / 2, kIcon, kIcon));
        painter.drawText(QRect(x + kIcon + kIconGap, cell.top(), textWidth, cell.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, displayed);
        x += kIcon + kIconGap + textWidth + kLabelGap;
    };

    for (qsizetype i = 0; i < labels.size(); ++i) {
        const auto& label = labels.at(i);
        auto cached = m_labelWidths.constFind(label);
        if (cached == m_labelWidths.cend()) {
            if (m_labelWidths.size() >= 256)
                m_labelWidths.clear();
            m_labelWidths.insert(label, metrics.horizontalAdvance(label));
            cached = m_labelWidths.constFind(label);
        }
        const int labelWidth = *cached + kIcon + kIconGap;
        const int available = cell.right() + 1 - x;
        const qsizetype remaining = labels.size() - i - 1;
        const int reserve = remaining > 0
            ? kLabelGap + metrics.horizontalAdvance(overflowText(remaining)) : 0;
        if (labelWidth + reserve > available) {
            const int shortenedWidth = available - reserve - kIcon - kIconGap;
            qsizetype hidden = remaining + 1;
            if (i == 0 && shortenedWidth >= metrics.horizontalAdvance(QStringLiteral("…"))) {
                drawLabel(label, metrics.elidedText(label, Qt::ElideRight, shortenedWidth), shortenedWidth);
                --hidden;
            }
            const int remainingWidth = cell.right() + 1 - x;
            if (hidden > 0 && remainingWidth > 0)
                painter.drawText(QRect(x, cell.top(), remainingWidth, cell.height()),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 metrics.elidedText(overflowText(hidden), Qt::ElideRight, remainingWidth));
            break;
        }

        drawLabel(label, label, *cached);
    }
}

void TorrentDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    using namespace tgui::theme;
    const QRect row = option.rect;
    if (!row.isValid())
        return;

    const auto& colors = palette();
    const auto* view = qobject_cast<const QAbstractItemView*>(option.widget);
    const bool selected = option.state & QStyle::State_Selected;
    const bool active = view ? view->isActiveWindow() : option.state.testFlag(QStyle::State_Active);
    const bool focused = view ? view->hasFocus() : option.state.testFlag(QStyle::State_HasFocus);
    const bool current = view && view->currentIndex().row() == index.row();

    painter->save();
    painter->setClipRect(row, Qt::IntersectClip);

    QColor background = index.row() % 2 == 0 ? colors.panel : colors.zebra;
    if (selected)
        background = active ? (focused ? colors.selBgActive : colors.selBg) : colors.selBgInactive;
    else if (option.state & QStyle::State_MouseOver)
        background = colors.hoverBg;
    painter->fillRect(row, background);
    if (current) {
        painter->setPen(colors.selectionBorder);
        painter->drawLine(row.topLeft(), row.topRight());
        painter->drawLine(row.bottomLeft(), row.bottomRight());
    } else if (!selected) {
        painter->setPen(colors.listRowBorder);
        painter->drawLine(row.bottomLeft(), row.bottomRight());
    }

    QRect content = row.adjusted(6, 0, -6, -1);
    if (!content.isValid()) {
        painter->restore();
        return;
    }

    painter->setFont(option.font);
    const int column = index.column();
    if (column == TorrentModel::ColDone) {
        updateFonts(option.font);
        paintProgress(*painter, content, index);
    } else if (column == TorrentModel::ColLabels) {
        updateFonts(option.font);
        const auto labels = index.data(TorrentModel::LabelListRole).toStringList();
        if (!labels.isEmpty())
            paintLabels(*painter, content, labels);
    } else {
        if (column == TorrentModel::ColName) {
            const bool error = index.data(TorrentModel::ErrorRole).toBool();
            const auto iconId = torrentStatusIcon(index.data(TorrentModel::StatusRole).toInt(),
                                                  !error && index.data(TorrentModel::FinishedRole).toBool(),
                                                  error);
            const QRect iconRect(content.left(), content.top() + (content.height() - 16) / 2, 16, 16);
            icon(iconId).paint(painter, iconRect);
            content.adjust(22, 0, 0, 0);
        }
        const auto foreground = index.data(Qt::ForegroundRole);
        painter->setPen(foreground.isValid() ? foreground.value<QBrush>().color() : colors.text);
        const int alignment = index.data(Qt::TextAlignmentRole).toInt() | Qt::AlignVCenter;
        const auto text = index.data(Qt::DisplayRole).toString();
        const auto elided = option.fontMetrics.elidedText(text, Qt::ElideRight,
                                                          std::max(0, content.width()));
        painter->drawText(content, alignment, elided);
    }
    painter->restore();
}

QSize TorrentDelegate::sizeHint(const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    updateFonts(option.font);
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(std::max(tgui::theme::rowHeightFor(option.font), m_progressHeight + 4));
    return size;
}

int NameCheckDelegate::checkSize()
{
    return 22; // поле чекбокса: галочка + отступ
}

// --- ProgressDelegate ---

void ProgressDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    bool ok = false;
    const QVariant numeric = index.data(PeerListModel::ProgressRole);
    const double progress = (numeric.isValid() ? numeric : index.data()).toDouble(&ok);
    painter->save();
    drawDetailCell(*painter, option);
    if (ok && progress >= 0.0) {
        const auto& colors = tgui::theme::palette();
        QFont font = option.font;
        if (font.pointSizeF() > 0)
            font.setPointSizeF(font.pointSizeF() * 11.0 / 12.0);
        else
            font.setPixelSize(std::max(1, qRound(font.pixelSize() * 11.0 / 12.0)));
        const int textHeight = QFontMetrics(font).tightBoundingRect(QStringLiteral("0123456789.%")).height();
        const int barHeight = std::min(option.rect.height() - 1, std::max(15, textHeight + 2));
        drawProgressBar(*painter, option.rect.adjusted(8, 0, -8, -1), progress,
                        progress >= 100.0 ? colors.progressGreen : colors.blue, font, barHeight);
    } else {
        painter->setPen(tgui::theme::palette().textMuted);
        painter->drawText(option.rect, Qt::AlignCenter, QStringLiteral("—"));
    }
    painter->restore();
}

// --- NameCheckDelegate ---

void NameCheckDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
    const bool selected = option.state & QStyle::State_Selected;

    // Галочка (выделение строки) слева.
    QStyleOptionButton check;
    check.rect = QRect(option.rect.left() + 4,
                       option.rect.top() + (option.rect.height() - 14) / 2, 14, 14);
    check.state = (selected ? QStyle::State_Selected : QStyle::State_None) |
                  QStyle::State_Enabled | QStyle::State_On;
    check.palette = option.palette;
    style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &check, painter);

    // Текст с отступом под чекбокс.
    opt.rect = opt.rect.adjusted(checkSize(), 0, 0, 0);
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
}

// --- WantCheckDelegate ---
// Клик по чекбоксу Want (файл/папка) → эмитит wantToggled без изменения
// локального состояния. Состояние обновится после ответа демона (getTorrentDetails).
void WantCheckDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    QRect r = option.rect;
    if (!r.isValid())
        return;

    painter->save();

    drawDetailCell(*painter, option);
    const int size = 14;
    QStyleOptionButton check;
    check.rect = QRect(r.left() + (r.width() - size) / 2,
                       r.top() + (r.height() - size) / 2, size, size);
    check.palette = option.palette;
    check.state = option.state & (QStyle::State_Enabled | QStyle::State_MouseOver);
    switch (static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt())) {
        case Qt::Checked: check.state |= QStyle::State_On; break;
        case Qt::PartiallyChecked: check.state |= QStyle::State_NoChange; break;
        default: check.state |= QStyle::State_Off; break;
    }
    const auto* style = option.widget ? option.widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &check, painter, option.widget);
    painter->restore();
}

bool WantCheckDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index)
{
    auto* tree = qobject_cast<FileTreeModel*>(model);
    if (!tree || index.column() != FileTreeModel::Want)
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    if (!(index.flags() & Qt::ItemIsEnabled))
        return false;

    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            return static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton;
        case QEvent::MouseButtonRelease:
            if (static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton)
                return false;
            break;
        case QEvent::KeyPress: {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() != Qt::Key_Space && key->key() != Qt::Key_Select)
                return false;
            break;
        }
        default:
            return false;
    }
    emit tree->wantToggled(tree->sourceRows(index), tree->wantState(index) != Qt::Checked);
    return true;
}
