#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleFactory>

namespace {

using tgui::theme::Icon;
using tgui::theme::color::accent;
using tgui::theme::color::amber;
using tgui::theme::color::green;
using tgui::theme::color::iconSlate;
using tgui::theme::color::purple;
using tgui::theme::color::red;

// Все иконки рисуются в условной сетке 24×24 и масштабируются под размер.
constexpr qreal kGrid = 24.0;

QColor defaultColor(Icon id)
{
    switch (id) {
        case Icon::Disconnect:
        case Icon::Remove:
        case Icon::FilterError:
            return red;
        case Icon::Start:
        case Icon::StartAll:
        case Icon::Add:
        case Icon::FilterCompleted:
        case Icon::FilterActive:
        case Icon::FilterWaiting:
            return green;
        case Icon::Stop:
        case Icon::StopAll:
            return QColor(0xC7, 0x4C, 0x4C);
        case Icon::AltSpeed:
        case Icon::Move:
        case Icon::Folder:
        case Icon::FilterInactive:
        case Icon::FilterStopped:
        case Icon::StatusWaiting:
            return amber;
        case Icon::StatusDone:
        case Icon::StatusSeed:
            return green;
        case Icon::StatusDown:
        case Icon::Connect:
            return accent;
        case Icon::StatusError:
            return red;
        case Icon::StatusVerifying:
            return purple;
        case Icon::StatusStopped:
            return QColor(0x78, 0x7C, 0x82);
        default:
            return iconSlate;
    }
}

void stroke(QPainter& p, const QColor& c, qreal width = 2.0)
{
    QPen pen(c, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
}

void fill(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
}

// Квадратная «плашка» статуса торрента (как imgDone/imgDown в эталоне).
void badge(QPainter& p, const QColor& c)
{
    fill(p, c);
    p.drawRoundedRect(QRectF(2.5, 2.5, 19.0, 19.0), 5.5, 5.5);
}

void drawFolder(QPainter& p, const QRectF& r, const QColor& c, bool openRight = false)
{
    QPainterPath path;
    path.moveTo(r.left(), r.top() + r.height() * 0.22);
    path.lineTo(r.left() + r.width() * 0.38, r.top() + r.height() * 0.22);
    path.lineTo(r.left() + r.width() * 0.48, r.top() + r.height() * 0.36);
    path.lineTo(r.right() - 1.5, r.top() + r.height() * 0.36);
    path.quadTo(r.right(), r.top() + r.height() * 0.36, r.right(), r.top() + r.height() * 0.48);
    path.lineTo(r.right(), r.bottom() - 2.0);
    path.quadTo(r.right(), r.bottom(), r.right() - 2.0, r.bottom());
    path.lineTo(r.left() + 2.0, r.bottom());
    path.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - 2.0);
    path.closeSubpath();
    fill(p, c.lighter(112));
    p.drawPath(path);
    stroke(p, c.darker(115), 1.4);
    p.drawPath(path);

    if (openRight) {
        stroke(p, accent, 2.0);
        p.drawLine(QPointF(r.right() - 7.0, r.center().y() + 1.0),
                   QPointF(r.right() - 1.5, r.center().y() + 1.0));
        p.drawLine(QPointF(r.right() - 4.0, r.center().y() - 1.5),
                   QPointF(r.right() - 1.5, r.center().y() + 1.0));
        p.drawLine(QPointF(r.right() - 4.0, r.center().y() + 3.5),
                   QPointF(r.right() - 1.5, r.center().y() + 1.0));
    }
}

void drawLink(QPainter& p, const QColor& c)
{
    stroke(p, c, 2.0);
    // Два наклонных звена цепочки.
    p.save();
    p.translate(12, 12);
    p.rotate(-45);
    p.drawRoundedRect(QRectF(-8, -4.5, 8.5, 9), 4.0, 4.0);
    p.drawRoundedRect(QRectF(-0.5, -4.5, 8.5, 9), 4.0, 4.0);
    p.restore();
}

void drawDocument(QPainter& p, const QColor& c)
{
    QPainterPath path;
    path.moveTo(6, 3);
    path.lineTo(14, 3);
    path.lineTo(19, 8);
    path.lineTo(19, 21);
    path.lineTo(6, 21);
    path.closeSubpath();
    fill(p, c.lighter(190));
    stroke(p, c, 1.7);
    p.drawPath(path);
    p.drawLine(QPointF(14, 3), QPointF(14, 8));
    p.drawLine(QPointF(14, 8), QPointF(19, 8));
    stroke(p, c.lighter(130), 1.2);
    p.drawLine(QPointF(9, 12), QPointF(16, 12));
    p.drawLine(QPointF(9, 15), QPointF(16, 15));
    p.drawLine(QPointF(9, 18), QPointF(13, 18));
}

void drawPerson(QPainter& p, const QColor& c, const QPointF& center, qreal scale)
{
    fill(p, c);
    p.drawEllipse(center + QPointF(-3.2 * scale, -4.6 * scale), 3.0 * scale, 3.0 * scale);
    QPainterPath body;
    body.moveTo(center.x() - 5.6 * scale, center.y() + 6.4 * scale);
    body.quadTo(center.x() - 5.4 * scale, center.y() + 0.6 * scale,
                center.x(), center.y() + 0.6 * scale);
    body.quadTo(center.x() + 5.4 * scale, center.y() + 0.6 * scale,
                center.x() + 5.6 * scale, center.y() + 6.4 * scale);
    body.closeSubpath();
    p.drawPath(body);
}

void drawIcon(QPainter& p, Icon id, const QColor& c)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    switch (id) {
        case Icon::Connect: {
            stroke(p, c, 1.9);
            p.drawLine(QPointF(9, 3.5), QPointF(9, 8));
            p.drawLine(QPointF(15, 3.5), QPointF(15, 8));
            p.drawRoundedRect(QRectF(6.2, 8, 11.6, 7.2), 2.4, 2.4);
            p.drawLine(QPointF(12, 15.2), QPointF(12, 21));
            p.drawLine(QPointF(9.5, 18.2), QPointF(12, 21));
            p.drawLine(QPointF(14.5, 18.2), QPointF(12, 21));
            break;
        }
        case Icon::Disconnect: {
            stroke(p, c.lighter(125), 1.8);
            p.drawLine(QPointF(9, 4), QPointF(9, 8));
            p.drawLine(QPointF(15, 4), QPointF(15, 8));
            p.drawRoundedRect(QRectF(6.2, 8, 11.6, 7.2), 2.4, 2.4);
            p.drawLine(QPointF(12, 15.2), QPointF(12, 20));
            stroke(p, c, 2.2);
            p.drawLine(QPointF(4, 20), QPointF(20, 4));
            break;
        }
        case Icon::Add: {
            stroke(p, c, 2.0);
            p.drawEllipse(QPointF(12, 12), 8.6, 8.6);
            p.drawLine(QPointF(12, 7.8), QPointF(12, 16.2));
            p.drawLine(QPointF(7.8, 12), QPointF(16.2, 12));
            break;
        }
        case Icon::Start: {
            fill(p, c);
            QPainterPath path;
            path.moveTo(8.4, 4.8);
            path.lineTo(19.2, 12);
            path.lineTo(8.4, 19.2);
            path.closeSubpath();
            p.drawPath(path);
            break;
        }
        case Icon::StartAll: {
            fill(p, c);
            for (qreal dx : {0.0, 6.0}) {
                QPainterPath path;
                path.moveTo(5.5 + dx, 6);
                path.lineTo(12.0 + dx, 12);
                path.lineTo(5.5 + dx, 18);
                path.closeSubpath();
                p.drawPath(path);
            }
            break;
        }
        case Icon::Stop: {
            fill(p, c);
            p.drawRoundedRect(QRectF(6.5, 6.5, 11, 11), 2.4, 2.4);
            break;
        }
        case Icon::StopAll: {
            fill(p, c);
            p.drawRoundedRect(QRectF(4.5, 6.5, 7, 11), 2.0, 2.0);
            p.drawRoundedRect(QRectF(13.0, 6.5, 7, 11), 2.0, 2.0);
            break;
        }
        case Icon::Remove: {
            stroke(p, c, 1.9);
            p.drawLine(QPointF(4.5, 7), QPointF(19.5, 7));
            p.drawLine(QPointF(9, 4), QPointF(15, 4));
            QPainterPath body;
            body.moveTo(6.5, 7);
            body.lineTo(7.4, 20);
            body.quadTo(7.5, 21, 8.5, 21);
            body.lineTo(15.5, 21);
            body.quadTo(16.5, 21, 16.6, 20);
            body.lineTo(17.5, 7);
            p.drawPath(body);
            stroke(p, c.lighter(145), 1.5);
            p.drawLine(QPointF(10.2, 10.5), QPointF(10.7, 17.5));
            p.drawLine(QPointF(13.8, 10.5), QPointF(13.3, 17.5));
            break;
        }
        case Icon::Properties: {
            stroke(p, c, 1.8);
            for (qreal y : {6.5, 12.0, 17.5}) {
                p.drawLine(QPointF(4.5, y), QPointF(19.5, y));
            }
            fill(p, c);
            p.drawEllipse(QPointF(9, 6.5), 2.4, 2.4);
            p.drawEllipse(QPointF(15, 12.0), 2.4, 2.4);
            p.drawEllipse(QPointF(10.5, 17.5), 2.4, 2.4);
            break;
        }
        case Icon::Move: {
            drawFolder(p, QRectF(3.5, 5.5, 14.5, 13.5), amber, true);
            break;
        }
        case Icon::Refresh: {
            stroke(p, c, 2.1);
            QRectF r(4.5, 4.5, 15, 15);
            p.drawArc(r, 35 * 16, 285 * 16);
            p.drawLine(QPointF(18.2, 4.2), QPointF(18.2, 8.6));
            p.drawLine(QPointF(18.2, 8.6), QPointF(13.8, 8.6));
            break;
        }
        case Icon::AutoRefresh: {
            stroke(p, c, 1.7);
            p.drawEllipse(QPointF(12, 12), 8.4, 8.4);
            p.drawLine(QPointF(12, 7.2), QPointF(12, 12));
            p.drawLine(QPointF(12, 12), QPointF(15.6, 14.2));
            break;
        }
        case Icon::AltSpeed: {
            stroke(p, c, 1.9);
            QRectF r(3.5, 6.0, 17, 14);
            p.drawArc(r, 0, 180 * 16);
            stroke(p, c, 2.2);
            p.drawLine(QPointF(12, 14.5), QPointF(16.8, 9.2));
            fill(p, c);
            p.drawEllipse(QPointF(12, 14.5), 1.8, 1.8);
            break;
        }
        case Icon::About: {
            stroke(p, c, 1.9);
            p.drawEllipse(QPointF(12, 12), 8.6, 8.6);
            fill(p, c);
            p.drawEllipse(QPointF(12, 7.8), 1.15, 1.15);
            stroke(p, c, 2.0);
            p.drawLine(QPointF(12, 11), QPointF(12, 16.8));
            break;
        }
        case Icon::Search: {
            stroke(p, c, 2.0);
            p.drawEllipse(QPointF(10.5, 10.5), 5.8, 5.8);
            p.drawLine(QPointF(15.0, 15.0), QPointF(20.0, 20.0));
            break;
        }
        case Icon::Folder:
            drawFolder(p, QRectF(2.8, 5.0, 18.4, 14.5), amber);
            break;
        case Icon::Tracker:
            drawLink(p, c);
            break;
        case Icon::FilterAll: {
            fill(p, c);
            p.drawEllipse(QPointF(12, 12), 6.4, 6.4);
            fill(p, QColor(255, 255, 255, 190));
            p.drawEllipse(QPointF(12, 12), 2.3, 2.3);
            break;
        }
        case Icon::FilterDownloading:
        case Icon::FilterCompleted:
        case Icon::FilterActive:
        case Icon::FilterInactive:
        case Icon::FilterStopped:
        case Icon::FilterError:
        case Icon::FilterWaiting: {
            const QColor dot = defaultColor(id);
            fill(p, dot);
            p.drawEllipse(QPointF(12, 12), 6.2, 6.2);
            fill(p, QColor(255, 255, 255, 150));
            p.drawEllipse(QPointF(9.6, 9.4), 1.7, 1.7);
            break;
        }
        case Icon::StatusDone: {
            badge(p, c);
            stroke(p, Qt::white, 2.6);
            QPainterPath check;
            check.moveTo(6.6, 12.2);
            check.lineTo(10.3, 15.9);
            check.lineTo(17.6, 8.4);
            p.drawPath(check);
            break;
        }
        case Icon::StatusStopped: {
            badge(p, c);
            fill(p, Qt::white);
            p.drawRoundedRect(QRectF(8.2, 7.2, 2.8, 9.6), 1.0, 1.0);
            p.drawRoundedRect(QRectF(13.0, 7.2, 2.8, 9.6), 1.0, 1.0);
            break;
        }
        case Icon::StatusDown: {
            badge(p, c);
            stroke(p, Qt::white, 2.4);
            p.drawLine(QPointF(12, 6.4), QPointF(12, 16.4));
            p.drawLine(QPointF(12, 16.4), QPointF(7.8, 12.2));
            p.drawLine(QPointF(12, 16.4), QPointF(16.2, 12.2));
            break;
        }
        case Icon::StatusSeed: {
            badge(p, c);
            stroke(p, Qt::white, 2.4);
            p.drawLine(QPointF(12, 17.6), QPointF(12, 7.6));
            p.drawLine(QPointF(12, 7.6), QPointF(7.8, 11.8));
            p.drawLine(QPointF(12, 7.6), QPointF(16.2, 11.8));
            break;
        }
        case Icon::StatusError: {
            badge(p, c);
            fill(p, Qt::white);
            p.drawRoundedRect(QRectF(10.6, 6.2, 2.8, 8.8), 1.4, 1.4);
            p.drawEllipse(QPointF(12, 17.8), 1.7, 1.7);
            break;
        }
        case Icon::StatusWaiting: {
            badge(p, c);
            stroke(p, Qt::white, 1.9);
            p.drawEllipse(QPointF(12, 12), 6.2, 6.2);
            p.drawLine(QPointF(12, 8.2), QPointF(12, 12.2));
            p.drawLine(QPointF(12, 12.2), QPointF(15.3, 14.2));
            break;
        }
        case Icon::StatusVerifying: {
            badge(p, c);
            fill(p, Qt::white);
            QPainterPath top;
            top.moveTo(7.4, 5.8);
            top.lineTo(16.6, 5.8);
            top.lineTo(12.0, 11.8);
            top.closeSubpath();
            p.drawPath(top);
            QPainterPath bottom;
            bottom.moveTo(7.4, 18.2);
            bottom.lineTo(16.6, 18.2);
            bottom.lineTo(12.0, 12.2);
            bottom.closeSubpath();
            p.drawPath(bottom);
            break;
        }
        case Icon::TabGeneral:
        case Icon::TabFiles:
            drawDocument(p, c);
            break;
        case Icon::TabTrackers:
            drawLink(p, c);
            break;
        case Icon::TabPeers:
            drawPerson(p, c, QPointF(9.2, 10.0), 0.92);
            drawPerson(p, c.lighter(135), QPointF(15.4, 10.8), 0.72);
            break;
        case Icon::TabStatistics: {
            fill(p, c);
            p.drawRoundedRect(QRectF(4.5, 13.0, 3.6, 7.0), 1.0, 1.0);
            p.drawRoundedRect(QRectF(10.2, 9.0, 3.6, 11.0), 1.0, 1.0);
            p.drawRoundedRect(QRectF(15.9, 5.0, 3.6, 15.0), 1.0, 1.0);
            break;
        }
    }

    p.restore();
}

QPixmap renderIcon(Icon id, int size, const QColor& tint, qreal dpr)
{
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const qreal scale = (size * dpr) / kGrid;
    p.scale(scale, scale);
    drawIcon(p, id, tint.isValid() ? tint : defaultColor(id));
    p.end();
    return pm;
}

} // namespace

namespace tgui::theme {

QIcon icon(Icon id, const QColor& tint)
{
    QIcon result;
    for (int size : {16, 20, 24, 32, 48}) {
        result.addPixmap(renderIcon(id, size, tint, 1.0), QIcon::Normal, QIcon::Off);
        result.addPixmap(renderIcon(id, size, tint, 2.0), QIcon::Normal, QIcon::Off);
    }
    return result;
}

QPixmap appIconPixmap(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, 0, size);
    bg.setColorAt(0.0, QColor(0x4B, 0x9A, 0xE3));
    bg.setColorAt(1.0, QColor(0x24, 0x6B, 0xB5));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(1, 1, size - 2, size - 2), size * 0.23, size * 0.23);

    QPen pen(Qt::white, size * 0.105);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal cx = size / 2.0;
    // Стрелка вниз (слева) и вверх (справа) — мотив «обмена».
    p.drawLine(QPointF(cx - size * 0.14, size * 0.27), QPointF(cx - size * 0.14, size * 0.63));
    p.drawLine(QPointF(cx - size * 0.14, size * 0.63), QPointF(cx - size * 0.24, size * 0.49));
    p.drawLine(QPointF(cx - size * 0.14, size * 0.63), QPointF(cx - size * 0.04, size * 0.49));
    p.drawLine(QPointF(cx + size * 0.14, size * 0.73), QPointF(cx + size * 0.14, size * 0.37));
    p.drawLine(QPointF(cx + size * 0.14, size * 0.37), QPointF(cx + size * 0.04, size * 0.51));
    p.drawLine(QPointF(cx + size * 0.14, size * 0.37), QPointF(cx + size * 0.24, size * 0.51));
    return pm;
}

QPixmap statusDotPixmap(const QColor& color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(0.5, 0.5, size - 1.0, size - 1.0));
    return pm;
}

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, color::window);
    p.setColor(QPalette::WindowText, color::text);
    p.setColor(QPalette::Base, color::panel);
    p.setColor(QPalette::AlternateBase, color::panelAlt);
    p.setColor(QPalette::Text, color::text);
    p.setColor(QPalette::Button, color::panel);
    p.setColor(QPalette::ButtonText, color::text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, color::accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, color::text);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::PlaceholderText, color::textFaint);
    p.setColor(QPalette::Disabled, QPalette::Text, color::textFaint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, color::textFaint);
    p.setColor(QPalette::Disabled, QPalette::WindowText, color::textFaint);
    app.setPalette(p);

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSize(9);
    app.setFont(font);

    app.setStyleSheet(QStringLiteral(R"(
* { outline: none; }
QWidget { color: #000000; }
QMainWindow, QDialog { background: #F0F0F0; }

QMenuBar {
    background: #FFFFFF;
    border-bottom: 1px solid #D9D9D9;
    padding: 1px 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 5px 10px;
    border-radius: 3px;
    color: #000000;
}
QMenuBar::item:selected { background: #CCE8FF; color: #000000; }
QMenuBar::item:pressed { background: #B8DCFA; }

QMenu {
    background: #FFFFFF;
    border: 1px solid #ABADB3;
    border-radius: 4px;
    padding: 3px;
}
QMenu::item {
    padding: 5px 28px 5px 10px;
    border-radius: 3px;
}
QMenu::item:selected { background: #CCE8FF; color: #000000; }
QMenu::item:disabled { color: #8C8C8C; }
QMenu::separator { height: 1px; background: #D9D9D9; margin: 4px 8px; }

QToolBar {
    background: #F0F0F0;
    border: none;
    border-bottom: 1px solid #D9D9D9;
    spacing: 1px;
    padding: 3px 6px;
}
QToolBar::separator {
    background: #ABADB3;
    width: 1px;
    margin: 4px 5px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px;
}
QToolButton:hover { background: #E5F3FF; border-color: #ABADB3; }
QToolButton:pressed { background: #CCE8FF; }
QToolButton:checked { background: #CCE8FF; border-color: #0078D7; }
QToolButton::menu-indicator { image: none; }

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: #FFFFFF;
    border: 1px solid #ABADB3;
    border-radius: 3px;
    padding: 4px 7px;
    min-height: 18px;
    selection-background-color: #0078D7;
    selection-color: #FFFFFF;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #0078D7;
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    background: #F0F0F0;
    color: #8C8C8C;
    border-color: #D9D9D9;
}
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background: #FFFFFF;
    border: 1px solid #ABADB3;
    selection-background-color: #CCE8FF;
    selection-color: #000000;
    padding: 2px;
}

QPushButton {
    background: #F0F0F0;
    border: 1px solid #ABADB3;
    border-radius: 3px;
    padding: 5px 14px;
    min-height: 18px;
}
QPushButton:hover { background: #E5F3FF; border-color: #0078D7; }
QPushButton:pressed { background: #CCE8FF; }
QPushButton:default { background: #0078D7; border-color: #005A9E; color: #FFFFFF; }
QPushButton:default:hover { background: #1A88E0; }
QPushButton:disabled {
    background: #F0F0F0;
    color: #8C8C8C;
    border-color: #D9D9D9;
}

QTreeView, QTableView, QListView {
    background: #FFFFFF;
    alternate-background-color: #F7F7F7;
    border: 1px solid #ABADB3;
    border-radius: 2px;
    selection-background-color: #0078D7;
    selection-color: #FFFFFF;
    gridline-color: #E1E1E1;
}
QTreeView::item, QTableView::item, QListView::item { padding: 2px 5px; }
QTreeView::item:selected, QTableView::item:selected, QListView::item:selected {
    background: #0078D7;
    color: #FFFFFF;
}
QHeaderView { background: #F0F0F0; }
QHeaderView::section {
    background: #F0F0F0;
    color: #000000;
    border: none;
    border-right: 1px solid #D9D9D9;
    border-bottom: 1px solid #ABADB3;
    padding: 4px 7px;
    font-weight: 600;
}
QHeaderView::section:hover { background: #E5F3FF; }
QTableCornerButton::section { background: #F0F0F0; border: none; }

QTabWidget::pane {
    border: 1px solid #ABADB3;
    border-radius: 0;
    background: #FFFFFF;
    top: -1px;
}
QTabBar { background: #F0F0F0; }
QTabBar::tab {
    background: #F0F0F0;
    color: #000000;
    border: 1px solid transparent;
    border-bottom: none;
    padding: 5px 12px;
    margin-right: 2px;
    margin-top: 2px;
}
QTabBar::tab:hover { background: #E5F3FF; }
QTabBar::tab:selected {
    background: #FFFFFF;
    border-color: #ABADB3;
    border-bottom-color: #FFFFFF;
    margin-top: 0;
    padding-top: 7px;
}
QTabBar::tab:disabled { color: #8C8C8C; }

QStatusBar {
    background: #F0F0F0;
    border-top: 1px solid #D9D9D9;
    color: #000000;
}
QStatusBar::item { border: none; }
QLabel#statusText { color: #000000; padding: 0 4px; }
QLabel#statusSegment {
    color: #000000;
    border-left: 1px solid #ABADB3;
    padding: 0 10px;
}

QSplitter::handle { background: #D9D9D9; }
QSplitter::handle:hover { background: #A0A0A0; }
QSplitter::handle:vertical { height: 4px; }
QSplitter::handle:horizontal { width: 4px; }

QScrollBar:vertical {
    background: #F0F0F0;
    width: 14px;
    margin: 0;
    border-left: 1px solid #D9D9D9;
}
QScrollBar::handle:vertical {
    background: #CDCDCD;
    min-height: 24px;
    border: 1px solid #ABADB3;
    border-radius: 2px;
    margin: 1px;
}
QScrollBar::handle:vertical:hover { background: #A0A0A0; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal {
    background: #F0F0F0;
    height: 14px;
    margin: 0;
    border-top: 1px solid #D9D9D9;
}
QScrollBar::handle:horizontal {
    background: #CDCDCD;
    min-width: 24px;
    border: 1px solid #ABADB3;
    border-radius: 2px;
    margin: 1px;
}
QScrollBar::handle:horizontal:hover { background: #A0A0A0; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

QProgressBar {
    background: #FFFFFF;
    border: 1px solid #A0A0A0;
    border-radius: 0;
    min-height: 12px;
    max-height: 12px;
    text-align: center;
    color: #000000;
    font-weight: 600;
}
QProgressBar::chunk {
    border-radius: 0;
    /* Как в эталоне: сплошная синяя заливка #0078D7. */
    background: #0078D7;
}
/* Верхняя тонкая полоса шкалы General (нижняя — карта кусков, рисуется кодом). */
QProgressBar#progressTop {
    min-height: 4px;
    max-height: 4px;
}

QGroupBox {
    background: #FFFFFF;
    border: 1px solid #ABADB3;
    border-radius: 2px;
    margin-top: 14px;
    padding: 10px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #000000;
}

QFrame#infoCard, QFrame#statsCard {
    background: #FFFFFF;
    border: 1px solid #C0C0C0;
    border-radius: 2px;
}
QLabel#infoCardTitle {
    background: #E1E1E1;
    color: #000000;
    font-weight: 700;
    padding: 4px 8px;
}
QLabel#fieldLabel { color: #000000; }
QLabel#fieldValue { color: #000000; }
QLabel#statsHeader {
    color: #000000;
    font-weight: 700;
    padding: 4px 0;
    border-bottom: 1px solid #ABADB3;
}

QWidget#filterPanel { background: #FFFFFF; border-right: 1px solid #D9D9D9; }
QTreeWidget#filterTree {
    background: #FFFFFF;
    border: none;
    border-radius: 0;
}
QTreeWidget#filterTree::item:selected { background: transparent; color: #000000; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

QToolTip {
    background: #FFFFB6;
    color: #000000;
    border: 1px solid #ABADB3;
    padding: 3px 6px;
    border-radius: 0;
}

QCheckBox, QRadioButton { spacing: 6px; }
)"));
}

} // namespace tgui::theme
