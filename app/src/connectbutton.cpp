#include "connectbutton.h"
#include "theme.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>

#include <algorithm>

namespace {
constexpr int kPadding = 8;
constexpr int kGap = 7;
constexpr int kIconSize = 16;
constexpr int kArrowWidth = 8;
constexpr int kFrameWidth = 2 * kPadding + 2 * kGap + kIconSize + kArrowWidth;

/** Возвращает шрифт второй строки, сохраняя выбранный масштаб интерфейса.
 * @param base Шрифт кнопки.
 */
QFont subtitleFont(QFont base)
{
    if (base.pointSizeF() > 0)
        base.setPointSizeF(base.pointSizeF() * 0.875);
    else
        base.setPixelSize(std::max(1, qRound(base.pixelSize() * 0.875)));
    return base;
}
}

ConnectButton::ConnectButton(QWidget* parent)
    : QToolButton(parent)
{
    setPopupMode(QToolButton::InstantPopup);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMaximumWidth(230);
}

void ConnectButton::setConnection(const QString& title, const QString& subtitle, bool connected)
{
    setText(title);
    m_subtitle = subtitle;
    m_connected = connected;
    setIcon(tgui::theme::icon(connected ? tgui::theme::Icon::Connect : tgui::theme::Icon::Disconnect));
    setAccessibleName(title);
    setAccessibleDescription(subtitle);
    updateGeometry();
    update();
}

QSize ConnectButton::sizeHint() const
{
    QFont titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    const auto titleMetrics = QFontMetrics(titleFont);
    const auto subtitleMetrics = QFontMetrics(subtitleFont(font()));
    const int textWidth = std::max(titleMetrics.horizontalAdvance(text()),
                                   subtitleMetrics.horizontalAdvance(m_subtitle));
    const int height = std::max(34, titleMetrics.height() + subtitleMetrics.height() + 4);
    return QSize(std::clamp(textWidth + kFrameWidth, minimumSizeHint().width(), maximumWidth()), height);
}

QSize ConnectButton::minimumSizeHint() const
{
    QFont titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    const int height = QFontMetrics(titleFont).height() + QFontMetrics(subtitleFont(font())).height() + 4;
    return QSize(kFrameWidth + 36, std::max(34, height));
}

void ConnectButton::paintEvent(QPaintEvent*)
{
    const auto& colors = tgui::theme::palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    //const bool isInteractive = underMouse() || hasFocus() || isDown();
    const bool isInteractive = underMouse() || isDown();

    // Фон и бордюр рисуем ТОЛЬКО в интерактивном состоянии.
    // В обычном виде кнопка полностью прозрачная.
    if (isInteractive) {
        painter.setBrush(m_connected ? colors.panel : colors.connectOfflineBg);
        const QColor border = colors.inputFocus;
        painter.setPen(border);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);
    }

    icon().paint(&painter, QRect(kPadding, (height() - kIconSize) / 2, kIconSize, kIconSize));
    const int arrowX = width() - kPadding - kArrowWidth / 2;
    const int arrowY = height() / 2;
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.textMuted);
    const QPoint arrow[]{QPoint(arrowX - 4, arrowY - 2), QPoint(arrowX + 4, arrowY - 2),
                         QPoint(arrowX, arrowY + 2)};
    painter.drawPolygon(arrow, 3);

    QFont titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    const QFont subFont = subtitleFont(font());
    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics subMetrics(subFont);
    const int x = kPadding + kIconSize + kGap;
    const int w = std::max(0, width() - kFrameWidth);
    const int y = (height() - titleMetrics.height() - subMetrics.height()) / 2;
    painter.setFont(titleFont);
    painter.setPen(colors.text);
    painter.drawText(x, y + titleMetrics.ascent(), titleMetrics.elidedText(text(), Qt::ElideRight, w));
    painter.setFont(subFont);
    painter.setPen(colors.textMuted);
    painter.drawText(x, y + titleMetrics.height() + subMetrics.ascent(),
                     subMetrics.elidedText(m_subtitle, Qt::ElideRight, w));
}

void ConnectButton::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Down:
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Space:
            event->accept();
            if (!event->isAutoRepeat())
                showMenu();
            return;
        default:
            QToolButton::keyPressEvent(event);
    }
}
