#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

class QApplication;

// Единая тема приложения: палитра, QSS и векторные иконки.
// Ориентир — оформление transmission-клиента (см. WORKFLOW.md, design/need.png),
// но с более мягкими границами, аккуратными отступами и hover-состояниями.
namespace tgui::theme {

enum class Icon {
    Connect,
    Disconnect,
    Add,
    Start,
    Stop,
    StartAll,
    StopAll,
    Remove,
    Properties,
    Move,
    Refresh,
    AutoRefresh,
    AltSpeed,
    About,
    Search,
    Folder,
    Tracker,
    FilterAll,
    FilterDownloading,
    FilterCompleted,
    FilterActive,
    FilterInactive,
    FilterStopped,
    FilterError,
    FilterWaiting,
    TabGeneral,
    TabTrackers,
    TabPeers,
    TabFiles,
    TabStatistics,
};

namespace color {

inline const QColor window{0xF1, 0xF4, 0xF7};
inline const QColor panel{0xFF, 0xFF, 0xFF};
inline const QColor panelAlt{0xF7, 0xF9, 0xFB};
inline const QColor border{0xDC, 0xE1, 0xE7};
inline const QColor borderSoft{0xE7, 0xEB, 0xF0};
inline const QColor text{0x23, 0x2A, 0x33};
inline const QColor textMuted{0x68, 0x71, 0x7E};
inline const QColor textFaint{0x8A, 0x93, 0xA0};
inline const QColor accent{0x2F, 0x7F, 0xD1};
inline const QColor accentDark{0x24, 0x6B, 0xB5};
inline const QColor accentLight{0xE8, 0xF1, 0xFB};
inline const QColor hover{0xED, 0xF4, 0xFC};
inline const QColor green{0x2A, 0xA1, 0x5A};
inline const QColor greenLight{0xE7, 0xF6, 0xED};
inline const QColor amber{0xE0, 0xA2, 0x3B};
inline const QColor amberLight{0xFB, 0xF3, 0xE3};
inline const QColor red{0xD6, 0x45, 0x45};
inline const QColor redLight{0xFB, 0xEA, 0xEA};
inline const QColor purple{0x7B, 0x61, 0xC9};

} // namespace color

// Устанавливает Fusion + палитру + QSS. Вызывается один раз в main().
void apply(QApplication& app);

// Иконка в стиле темы. tint — необязательный цвет (по умолчанию — свой у иконки).
QIcon icon(Icon id, const QColor& tint = QColor());

// Иконка приложения (окно, трей).
QPixmap appIconPixmap(int size);

// Небольшая цветная точка для статус-бара (возвращает pixmap size×size).
QPixmap statusDotPixmap(const QColor& color, int size = 10);

} // namespace tgui::theme
