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
    // Иконки состояния торрента (первая ячейка колонки Name).
    StatusDone,
    StatusStopped,
    StatusDown,
    StatusSeed,
    StatusError,
    StatusWaiting,
    StatusVerifying,
    TabGeneral,
    TabTrackers,
    TabPeers,
    TabFiles,
    TabStatistics,
};

namespace color {

// Палитра — как в эталоне (transgui): серая «хром-панель», белый контент,
// системное синее выделение, серые заголовки секций.
inline const QColor window{0xF0, 0xF0, 0xF0};
inline const QColor panel{0xFF, 0xFF, 0xFF};
inline const QColor panelAlt{0xF7, 0xF7, 0xF7};
inline const QColor border{0xAB, 0xAD, 0xB3};
inline const QColor borderSoft{0xD9, 0xD9, 0xD9};
inline const QColor section{0xE1, 0xE1, 0xE1};
inline const QColor text{0x00, 0x00, 0x00};
inline const QColor textMuted{0x3C, 0x3C, 0x3C};
inline const QColor textFaint{0x7A, 0x7A, 0x7A};
inline const QColor iconSlate{0x33, 0x33, 0x33};
inline const QColor accent{0x00, 0x78, 0xD7};
inline const QColor accentDark{0x00, 0x5A, 0x9E};
inline const QColor accentLight{0xCC, 0xE8, 0xFF};
inline const QColor hover{0xE5, 0xF3, 0xFF};
inline const QColor green{0x1E, 0x9E, 0x4A};
inline const QColor greenLight{0xE7, 0xF6, 0xED};
inline const QColor amber{0xE8, 0xA2, 0x00};
inline const QColor amberLight{0xFD, 0xF3, 0xDC};
inline const QColor red{0xD1, 0x34, 0x38};
inline const QColor redLight{0xFB, 0xEA, 0xEA};
inline const QColor purple{0x7A, 0x65, 0xC0};

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
