#include "delegates.h"

#include <QApplication>
#include <QPainter>

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