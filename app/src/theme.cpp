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
using tgui::theme::color::accentDark;
using tgui::theme::color::amber;
using tgui::theme::color::green;
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
        case Icon::FilterCompleted:
        case Icon::FilterActive:
        case Icon::FilterWaiting:
            return green;
        case Icon::Stop:
        case Icon::StopAll:
            return QColor(0xD9, 0x55, 0x55);
        case Icon::AltSpeed:
        case Icon::FilterInactive:
        case Icon::FilterStopped:
            return amber;
        default:
            return accent;
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

void drawCheck(QPainter& p, const QRectF& r, const QColor& c)
{
    stroke(p, c, 2.0);
    QPainterPath path;
    path.moveTo(r.left() + r.width() * 0.24, r.top() + r.height() * 0.52);
    path.lineTo(r.left() + r.width() * 0.44, r.top() + r.height() * 0.72);
    path.lineTo(r.left() + r.width() * 0.78, r.top() + r.height() * 0.30);
    p.drawPath(path);
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
QWidget { color: #232A33; }
QMainWindow, QDialog { background: #F1F4F7; }

QMenuBar {
    background: #FFFFFF;
    border-bottom: 1px solid #DCE1E7;
    padding: 2px 6px;
}
QMenuBar::item {
    background: transparent;
    padding: 6px 11px;
    border-radius: 5px;
    color: #3B4450;
}
QMenuBar::item:selected { background: #E8F1FB; color: #1F5F9E; }
QMenuBar::item:pressed { background: #D8E7F8; }

QMenu {
    background: #FFFFFF;
    border: 1px solid #DCE1E7;
    border-radius: 8px;
    padding: 5px;
}
QMenu::item {
    padding: 6px 30px 6px 12px;
    border-radius: 5px;
}
QMenu::item:selected { background: #E8F1FB; color: #1F5F9E; }
QMenu::item:disabled { color: #AAB2BD; }
QMenu::separator { height: 1px; background: #E7EBF0; margin: 4px 8px; }

QToolBar {
    background: #FBFCFD;
    border: none;
    border-bottom: 1px solid #DCE1E7;
    spacing: 2px;
    padding: 5px 8px;
}
QToolBar::separator {
    background: #E1E6EC;
    width: 1px;
    margin: 6px 5px;
}
QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px;
}
QToolButton:hover { background: #EAF2FB; border-color: #D6E6F6; }
QToolButton:pressed { background: #D8E7F8; }
QToolButton:checked { background: #DCECFC; border-color: #BFDDF8; }
QToolButton::menu-indicator { image: none; }

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: #FFFFFF;
    border: 1px solid #D3DAE3;
    border-radius: 6px;
    padding: 5px 8px;
    min-height: 18px;
    selection-background-color: #2F7FD1;
    selection-color: #FFFFFF;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #2F7FD1;
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    background: #F2F4F7;
    color: #A5ADB7;
    border-color: #E3E7EC;
}
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: #FFFFFF;
    border: 1px solid #DCE1E7;
    selection-background-color: #E8F1FB;
    selection-color: #1F5F9E;
    padding: 4px;
}

QPushButton {
    background: #FFFFFF;
    border: 1px solid #D3DAE3;
    border-radius: 6px;
    padding: 6px 14px;
    min-height: 18px;
}
QPushButton:hover { background: #F2F8FE; border-color: #B9D5F0; }
QPushButton:pressed { background: #E3EFFB; }
QPushButton:default { background: #2F7FD1; border-color: #2F7FD1; color: #FFFFFF; }
QPushButton:default:hover { background: #3A8BDE; }
QPushButton:disabled {
    background: #F2F4F7;
    color: #A5ADB7;
    border-color: #E3E7EC;
}

QTreeView, QTableView, QListView {
    background: #FFFFFF;
    alternate-background-color: #F7F9FB;
    border: 1px solid #DCE1E7;
    border-radius: 6px;
    selection-background-color: #2F7FD1;
    selection-color: #FFFFFF;
    gridline-color: #EDF0F3;
}
QTreeView::item, QTableView::item, QListView::item { padding: 3px 6px; }
QTreeView::item:selected, QTableView::item:selected, QListView::item:selected {
    background: #2F7FD1;
    color: #FFFFFF;
}
QHeaderView { background: transparent; }
QHeaderView::section {
    background: #F4F6F9;
    color: #55606D;
    border: none;
    border-right: 1px solid #E4E8ED;
    border-bottom: 1px solid #DCE1E7;
    padding: 6px 8px;
    font-weight: 600;
}
QHeaderView::section:hover { background: #EAF2FB; }
QTableCornerButton::section { background: #F4F6F9; border: none; }

QTabWidget::pane {
    border: 1px solid #DCE1E7;
    border-radius: 6px;
    background: #FFFFFF;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    color: #68717E;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 7px 14px;
    margin-right: 2px;
}
QTabBar::tab:hover {
    color: #2F7FD1;
    background: #EDF4FC;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
QTabBar::tab:selected { color: #1F5F9E; border-bottom-color: #2F7FD1; font-weight: 600; }
QTabBar::tab:disabled { color: #B6BDC6; }

QStatusBar {
    background: #F4F6F9;
    border-top: 1px solid #DCE1E7;
    color: #55606D;
}
QStatusBar::item { border: none; }
QLabel#statusText { color: #55606D; padding: 0 4px; }
QLabel#statusSegment {
    color: #55606D;
    border-left: 1px solid #DCE1E7;
    padding: 0 12px;
}

QSplitter::handle { background: #E7EBF0; }
QSplitter::handle:hover { background: #BFD8F2; }
QSplitter::handle:vertical { height: 4px; }
QSplitter::handle:horizontal { width: 4px; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
QScrollBar::handle:vertical {
    background: #C5CDD6;
    min-height: 28px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover { background: #A8B3C0; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
QScrollBar::handle:horizontal {
    background: #C5CDD6;
    min-width: 28px;
    border-radius: 4px;
}
QScrollBar::handle:horizontal:hover { background: #A8B3C0; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

QProgressBar {
    background: #E8EDF2;
    border: none;
    border-radius: 6px;
    min-height: 18px;
    max-height: 18px;
    text-align: center;
    color: #FFFFFF;
    font-weight: 600;
}
QProgressBar::chunk {
    border-radius: 6px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #4C95DE, stop:0.5 #2F7FD1, stop:1 #3A8BDE);
}

QGroupBox {
    background: #FFFFFF;
    border: 1px solid #E3E8EE;
    border-radius: 8px;
    margin-top: 14px;
    padding: 10px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 4px;
    color: #55606D;
}

QFrame#infoCard, QFrame#statsCard {
    background: #FFFFFF;
    border: 1px solid #E3E8EE;
    border-radius: 8px;
}
QLabel#infoCardTitle { color: #1F5F9E; font-weight: 700; }
QLabel#fieldLabel { color: #8A93A0; }
QLabel#fieldValue { color: #232A33; }
QLabel#statsHeader {
    color: #55606D;
    font-weight: 600;
    border-bottom: 1px solid #E4E8ED;
    padding-bottom: 4px;
}

QWidget#filterPanel { background: #F7F9FB; border-right: 1px solid #DCE1E7; }
QTreeWidget#filterTree {
    background: transparent;
    border: none;
    border-radius: 0;
}
QTreeWidget#filterTree::item:selected { background: transparent; color: #232A33; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

QToolTip {
    background: #232A33;
    color: #FFFFFF;
    border: none;
    padding: 5px 8px;
    border-radius: 4px;
}

QCheckBox, QRadioButton { spacing: 6px; }
)"));
}

} // namespace tgui::theme
