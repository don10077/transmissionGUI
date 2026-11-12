#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPixmap>

class QApplication;
class QString;

// Единая тема приложения: палитра, QSS и векторные иконки.
// Ориентир — DESIGN_PLAN.md (design/) + design/mockup.html.
//
// Правила (DESIGN_PLAN, Раздел A / Фаза 0):
// - Все цвета — токены Palette; hex-литералы в QSS запрещены (CI-grep),
//   значения живут только в определениях палитр.
namespace tgui::theme {

enum class Icon {
    App,
    Connect,
    Disconnect,
    Add,
    AddLink,
    Start,
    ForceStart,
    Pause,
    Remove,
    Properties,
    Move,
    Refresh,
    AltSpeed,
    Queue,
    QueueUp,
    QueueDown,
    DaemonOptions,
    AppOptions,
    About,
    Search,
    Folder,
    Film,
    Label,
    LabelVideo,
    LabelSerials,
    LabelMusic,
    LabelBooks,
    LabelGames,
    LabelSoft,
    LabelPictures,
    Copy,
    Warning,
    Question,
    Information,
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

/** Цветовые токены оформления по design/mockup.html. */
struct Palette {
    // Поверхности
    QColor window;   // «хром» окна/тулбара
    QColor panel;    // контент
    QColor zebra;    // чётные строки
    QColor section;  // сетки/разделения (gridline)
    QColor headerBg; // заголовки колонок
    // Границы
    QColor border;     // лейаутные границы
    QColor borderSoft; // мягкие границы
    QColor layoutBorder; // тулбар/статус-бар/spлиттер (мягкая лейаутная)
    // Текст
    QColor text;
    QColor textMuted;
    QColor textFaint;
    QColor iconSlate;
    // Акцент
    QColor accent;
    QColor accentDark;
    QColor accentHover;
    // Статусы
    QColor green;
    QColor greenLight;
    QColor amber;
    QColor amberLight;
    QColor red;
    QColor redLight;
    QColor purple;
    // Прогресс/куски
    QColor blue;            // «загрузка» (chunk)
    QColor progressGreen;   // «завершено»
    QColor progressAmber;   // «ожидание»
    QColor progressRed;     // «ошибка»
    QColor progressStopped; // «остановлен»
    // Состояния строк/кнопок
    QColor hoverBg;
    QColor selBg;
    QColor selBgActive;
    QColor selBgInactive; // выделение в неактивном окне (Qt-специфика)
    QColor itemSelBg;   // выделение строк в списках/таблицах
    QColor itemSelText; // текст выделенных строк
    QColor disabledText;
    QColor highlight;     // QPalette::Highlight
    QColor highlightText; // QPalette::HighlightedText
    // Новые элементы (Фазы 8–9)
    QColor newBadge;     // .shownew
    QColor groupRow;     // .gr
    QColor toolbarBg;
    QColor toolbarSep;  // разделитель тулбара
    QColor sectionHeader; // .fh/.sec
    // Чипы меток (.chip-s)
    QColor chipBg;
    QColor chipText;
    QColor chipBorder;
    // PieceBar (.gbar)
    QColor pieceEmpty;
    QColor pieceBorder;
    // Input-контролы (.srch, input, .ftree, .lb)
    QColor inputBorder;
    QColor inputFocus;
    QColor inputSelectionText; // цвет текста выделения в полях
    QColor tooltipBg;          // фон подсказки (жёлтый, как в transgui)
    // Кнопки диалогов (.btn)
    QColor btnBg;
    QColor btnBorder;
    QColor btnHoverBg;
    QColor btnDangerHover;
    QColor btnDisabledBg;
    QColor btnDisabledBorder;
    QColor btnDisabledText;
    // Нативный прогресс-бар (диалоги)
    QColor progressBg;
    QColor progressBorder;
    // Скроллбары
    QColor scrollbarHandle;
    QColor scrollHover;
};

/** Возвращает палитру приложения по design/mockup.html. */
const Palette& palette();

// A.2: логические пиксели при 100%; Qt 6 масштабирует сам (HiDPI).
inline int dp(int v) { return v; }
// Минимальная высота строки с учётом шрифта (не фиксировать жёстко).
int rowHeightFor(const QFont& font);

// Имена токенов, используемые в существующих виджетах.
namespace color {

inline const QColor& window = palette().window;
inline const QColor& panel = palette().panel;
inline const QColor& panelAlt = palette().zebra;
inline const QColor& border = palette().border;
inline const QColor& borderSoft = palette().borderSoft;
inline const QColor& section = palette().section;
inline const QColor& text = palette().text;
inline const QColor& textMuted = palette().textMuted;
inline const QColor& textFaint = palette().textFaint;
inline const QColor& iconSlate = palette().iconSlate;
inline const QColor& accent = palette().accent;
inline const QColor& accentDark = palette().accentDark;
inline const QColor& accentLight = palette().selBg;
inline const QColor& hover = palette().hoverBg;
inline const QColor& green = palette().green;
inline const QColor& greenLight = palette().greenLight;
inline const QColor& amber = palette().amber;
inline const QColor& amberLight = palette().amberLight;
inline const QColor& red = palette().red;
inline const QColor& redLight = palette().redLight;
inline const QColor& purple = palette().purple;

} // namespace color

/**
 * Устанавливает Fusion, шрифт, палитру и QSS.
 * @param app Приложение Qt.
 */
void apply(QApplication& app);

/**
 * Возвращает иконку темы с вариантами для разных размеров и масштаба.
 * @param id Иконка из набора приложения.
 */
QIcon icon(Icon id);

/**
 * Возвращает значок файла, выделяя видеоформаты из макета.
 * @param fileName Имя или путь файла.
 */
QIcon fileIcon(const QString& fileName);

/**
 * Рисует точку состояния с учётом масштаба экрана.
 * @param color Цвет точки.
 * @param size Размер в логических пикселях.
 * @param dpr Масштаб экрана.
 */
QPixmap statusDotPixmap(const QColor& color, int size, qreal dpr);

} // namespace tgui::theme
