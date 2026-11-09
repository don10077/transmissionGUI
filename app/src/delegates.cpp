#include "delegates.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionViewItem>

#include "theme.h"
#include "torrentmodel.h"

namespace {

QColor statusColor(int status, bool finished, bool error)
{
    using namespace tgui::theme::color;
    if (error)
        return red;
    if (finished)
        return green;
    switch (status) {
        case 4: // Download
            return accent;
        case 6: // Seed
            return green;
        case 1: // CheckWait
        case 3: // DownloadWait
        case 5: // SeedWait
            return amber;
        case 2: // Check
            return purple;
        default: // Stopped
            return textFaint;
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
    // Остановленный, но полностью скачанный — зелёная галочка «готово».
    if (finished)
        return Icon::StatusDone;
    return Icon::StatusStopped;
}

} // namespace

void TorrentDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    using namespace tgui::theme;
    using namespace tgui::theme::color;

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const QRect r = option.rect;
    if (!r.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Фон строки: выделение, hover, чередование.
    QColor bg;
    if (selected)
        bg = accent;
    else if (hovered)
        bg = hover;
    else
        bg = (index.row() % 2 == 0) ? panel : panelAlt;
    painter->fillRect(r, bg);
    if (!selected) {
        painter->setPen(borderSoft);
        painter->drawLine(r.bottomLeft(), r.bottomRight());
    }

    const int col = index.column();
    const QColor normalText = selected ? QColor(Qt::white) : text;
    const QColor mutedText = selected ? QColor(255, 255, 255, 205) : textMuted;

    if (col == TorrentModel::ColName) {
        const int status = index.data(TorrentModel::StatusRole).toInt();
        const bool finished = index.data(TorrentModel::FinishedRole).toBool();
        const bool error = index.data(TorrentModel::ErrorRole).toBool();
        const QIcon stateIcon = tgui::theme::icon(torrentStatusIcon(status, finished, error));
        const QRect iconRect(r.left() + 7, r.top() + (r.height() - 16) / 2, 16, 16);
        stateIcon.paint(painter, iconRect, Qt::AlignCenter,
                        selected ? QIcon::Selected : QIcon::Normal);

        QFont f = option.font;
        painter->setFont(f);
        painter->setPen(normalText);
        const QRect textRect(r.left() + 28, r.top(), r.width() - 36, r.height());
        const QString elided = QFontMetrics(f).elidedText(
            index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
        painter->restore();
        return;
    }

    if (col == TorrentModel::ColDone) {
        const double progress = index.data(TorrentModel::ProgressRole).toDouble();
        if (progress < 0.0) {
            painter->setPen(mutedText);
            painter->drawText(r.adjusted(2, 0, -8, 0),
                              Qt::AlignVCenter | Qt::AlignHCenter, QStringLiteral("—"));
            painter->restore();
            return;
        }

        const QRectF track(r.left() + 6, r.top() + (r.height() - 14) / 2.0,
                           r.width() - 12, 14);
        painter->setPen(QPen(border, 1.0));
        painter->setBrush(QColor(Qt::white));
        painter->drawRoundedRect(track, 2, 2);

        const qreal fillW = track.width() * qBound(0.0, progress, 100.0) / 100.0;
        if (fillW > 0.5) {
            QRectF fill = track;
            fill.setWidth(fillW);
            QPainterPath clip;
            clip.addRoundedRect(track, 2, 2);
            painter->save();
            painter->setClipPath(clip);
            // Родной градиент эталона: GetLikeColor(clHighlight, 70) → clHighlight.
            QLinearGradient grad(fill.topLeft(), fill.bottomLeft());
            grad.setColorAt(0.0, QColor(0x46, 0xBE, 0xFF));
            grad.setColorAt(0.5, QColor(0x00, 0x78, 0xD7));
            grad.setColorAt(1.0, QColor(0x46, 0xBE, 0xFF));
            painter->fillRect(fill, grad);
            painter->restore();
        }

        const QString textValue = QStringLiteral("%1%").arg(progress, 0, 'f', 1);
        QFont f = option.font;
        f.setPointSizeF(qMax(7.5, f.pointSizeF() - 0.5));
        painter->setFont(f);

        // Как в DrawProgressCell эталона: текст белый поверх заливки, тёмный —
        // на незаполненной части. Иначе на выделенной строке проценты пропадают.
        const QRect textRect = track.toRect();
        painter->save();
        painter->setClipRect(QRectF(textRect.left(), textRect.top(), fillW,
                                    textRect.height()));
        painter->setPen(QColor(Qt::white));
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignHCenter, textValue);
        painter->restore();

        if (fillW < textRect.width()) {
            painter->save();
            painter->setClipRect(QRectF(textRect.left() + fillW, textRect.top(),
                                        textRect.width() - fillW, textRect.height()));
            painter->setPen(tgui::theme::color::text);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignHCenter, textValue);
            painter->restore();
        }
        painter->restore();
        return;
    }

    if (col == TorrentModel::ColStatus) {
        const int status = index.data(TorrentModel::StatusRole).toInt();
        const bool finished = index.data(TorrentModel::FinishedRole).toBool();
        const bool error = index.data(TorrentModel::ErrorRole).toBool();
        const QColor dot = selected ? QColor(Qt::white) : statusColor(status, finished, error);

        painter->setPen(Qt::NoPen);
        painter->setBrush(dot);
        painter->drawEllipse(QPointF(r.left() + 12, r.center().y() + 0.5), 3.6, 3.6);

        QFont f = option.font;
        painter->setFont(f);
        painter->setPen(selected ? normalText : (error ? red : text));
        const QRect textRect(r.left() + 23, r.top(), r.width() - 29, r.height());
        const QString elided = QFontMetrics(f).elidedText(
            index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
        painter->restore();
        return;
    }

    // Обычная текстовая ячейка.
    int align = index.data(Qt::TextAlignmentRole).toInt();
    if ((align & Qt::AlignHorizontal_Mask) == 0)
        align |= Qt::AlignLeft;
    align |= Qt::AlignVCenter;

    QColor textColor = normalText;
    if (!selected && col == TorrentModel::ColRateDown &&
        index.data(TorrentModel::RateDownRole).toDouble() > 0.0) {
        textColor = accent;
    } else if (!selected && col == TorrentModel::ColRateUp &&
               index.data(TorrentModel::RateUpRole).toDouble() > 0.0) {
        textColor = green;
    }

    QFont f = option.font;
    painter->setFont(f);
    painter->setPen(textColor);
    const int leftPad = (align & Qt::AlignRight) ? 2 : 8;
    const QRect textRect(r.left() + leftPad, r.top(), r.width() - leftPad - 8, r.height());
    const QString elided = QFontMetrics(f).elidedText(
        index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
    painter->drawText(textRect, align, elided);
    painter->restore();
}

QSize TorrentDelegate::sizeHint(const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(qMax(size.height(), 28));
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
    const double progress = index.data(Qt::DisplayRole).toDouble(); // 0..100
    QRect r = option.rect;
    if (r.width() < 4 || r.height() < 4)
        return;

    painter->save();
    const bool selected = option.state & QStyle::State_Selected;

    // Фон клетки.
    if (selected)
        painter->fillRect(r, option.palette.highlight());
    else
        painter->fillRect(r, option.palette.base());

    const QString text = QStringLiteral("%1%").arg(progress, 0, 'f', 1);

    // Бар: вертикальный градиент (светлее в центре), как GetLikeColor(clHighlight, 70).
    const int fillW = qRound(r.width() * (progress / 100.0));
    if (fillW > 0) {
        QRect bar(r.left(), r.top(), qMin(fillW, r.width()), r.height());
        QLinearGradient grad(bar.topLeft(), bar.bottomLeft());
        const QColor base = option.palette.highlight().color();
        grad.setColorAt(0.0, base.lighter(170));
        grad.setColorAt(0.5, base);
        grad.setColorAt(1.0, base.lighter(170));
        painter->fillRect(bar, grad);
    }

    // Текст по центру клетки; цвет — белый, если центр закрыт баром.
    const bool centerFilled = (r.width() / 2.0) < fillW;
    painter->setPen(centerFilled ? option.palette.highlightedText().color()
                                 : option.palette.text().color());
    painter->drawText(r.adjusted(2, 0, -2, 0), Qt::AlignVCenter | Qt::AlignHCenter,
                      text);
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