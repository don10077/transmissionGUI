#pragma once

#include <QColor>
#include <QFont>
#include <QGridLayout>
#include <QIcon>
#include <QPixmap>

#include <array>

class QApplication;
class QDialog;
class QLayout;
class QMessageBox;
class QSettings;
class QWidget;

// Единая тема приложения: палитра, QSS и векторные иконки.
// Ориентир — макет интерфейса (design/mockup.html).
//
// Правила (раздел A макета):
// - Все цвета — токены Palette; hex-литералы в QSS запрещены (CI-grep),
//   значения живут только в определениях палитр.
namespace tgui::theme {

/** Варианты оформления приложения. */
enum class Scheme { Light, Dark };

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
    ThemeDark,
    ThemeLight,
};

/** Цветовые токены оформления по design/mockup.html. */
struct Palette {
    // Поверхности
    QColor window;   // «хром» окна/тулбара
    QColor panel;    // контент
    QColor zebra;    // чётные строки
    QColor section;  // сетки/разделения (gridline)
    QColor headerBg; // заголовки колонок
    QColor listHeaderHover;
    QColor detailTabHover;
    // Границы
    QColor border;     // лейаутные границы
    QColor borderSoft; // мягкие границы
    QColor layoutBorder; // тулбар/статус-бар/spлиттер (мягкая лейаутная)
    QColor listRowBorder;
    QColor menuBorder;
    QColor menuSelectionBorder;
    QColor menuSeparator;
    QColor menuDisabledText;
    QColor statusRateHover;
    QColor statusOfflineDot;
    // Текст
    QColor text;
    QColor textMuted;
    QColor textFaint;
    QColor iconSlate;
    QColor detailFieldLabel;
    QColor formLabel; // подпись в форме диалога (.fr>label)
    // Акцент
    QColor accent;
    QColor accentDark;
    // Статусы
    QColor green;
    QColor greenLight;
    QColor amber;
    QColor amberLight;
    QColor red;
    QColor redLight;
    QColor purple;
    QColor statusNeutral;
    // Прогресс/куски
    QColor blue;            // текст статуса «загрузка»
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
    QColor toastBackground;
    QColor toastText;    // текст всплывающего уведомления
    QColor groupRow;     // .gr
    QColor toolbarBg;
    QColor toolbarSep;  // разделитель тулбара
    QColor selectionBorder;
    QColor connectOfflineBg;
    QColor connectOfflineBorder;
    QColor sectionHeader; // .fh/.sec
    QColor labelText;   // метки списка торрентов
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
    // GroupBox (.gb legend)
    QColor groupTitle;
    QColor groupBorder;
    // Нативный прогресс-бар (диалоги)
    QColor progressBg;
    QColor progressBorder;
    // Скроллбары
    QColor scrollbarHandle;
    QColor scrollHover;
    // Рамка диалогового окна (mockup .dlg: #7f8a95)
    QColor dialogBorder;
    QColor dialogTabBorder;
};

/** Возвращает активную палитру приложения (светлую или тёмную). */
const Palette& palette();

/** Возвращает текущую схему оформления. */
Scheme scheme();

/**
 * Переключает оформление на лету: палитра, QPalette, QSS и кэш иконок.
 * @param scheme Новая схема.
 */
void setScheme(Scheme scheme);

/** Состояния, по которым выбирается цвет заполнения шкалы. */
enum class ProgressState { Downloading, Seeding, Finished, Waiting, Verifying, Paused, Error, Count };
using ProgressColors = std::array<QColor, static_cast<std::size_t>(ProgressState::Count)>;

const ProgressColors& defaultProgressColors();
ProgressColors loadProgressColors(const QSettings& settings);
void saveProgressColors(QSettings& settings, const ProgressColors& colors);
/** Применяет цвета шкалы для предпросмотра без записи настроек. */
void previewProgressColors(const ProgressColors& colors);
ProgressState torrentProgressState(int status, bool finished, bool error);
QColor progressColor(ProgressState state);
/** Общий цвет текста для светлой и тёмной схем соответственно. */
using TextColors = std::array<QColor, 2>;
TextColors defaultTextColors();
TextColors loadTextColors(const QSettings& settings);
/** Сохраняет цвета и немедленно обновляет оформление приложения. */
void saveTextColors(QSettings& settings, const TextColors& colors);
/** Показывает цвет текущей темы без записи настроек. */
void previewTextColors(const TextColors& colors);

// A.2: логические пиксели при 100%; Qt 6 масштабирует сам (HiDPI).
inline int dp(int v) { return v; }
// Минимальная высота строки с учётом шрифта (не фиксировать жёстко).
int rowHeightFor(const QFont& font);

// Фаза 6.2 (mockup .fr): двухколоночная форма — подпись 200 px + гибкое поле, gap 8.
QGridLayout* formGrid(QWidget* parent);
// Строка формы: подпись (buddy поля, Alt-навигация) + поле; пустая подпись — поле в правой колонке.
void addFormRow(QGridLayout* grid, const QString& label, QWidget* field, int row = -1);
// Та же строка, когда поле — компоновка (кнопка рядом с полем ввода).
void addFormRow(QGridLayout* grid, const QString& label, QLayout* field, int row = -1);

// Строка на обе колонки: подпись не создаётся (groupbox, подсказка, футер с кнопками).
void addFullRow(QGridLayout* grid, QWidget* field, int row = -1);
// То же для компоновки.
void addFullRow(QGridLayout* grid, QLayout* field, int row = -1);

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
 * @param scheme Схема оформления; по умолчанию светлая.
 */
void apply(QApplication& app, Scheme scheme = Scheme::Light);

/**
 * Оформляет диалог как в макете (DLG): своя белая шапка с названием и «✕»,
 * тонкая рамка вместо системного заголовка. Вызывается в конце конструктора,
 * когда layout диалога уже собран.
 * @param dialog Диалог приложения.
 */
void decorateDialog(QDialog* dialog);

/**
 * Фиксирует минимальную ширину кнопок QMessageBox по их sizeHint. Без этого
 * общий QSS (min-width у QPushButton) не растягивает окно под длинный текст
 * кнопки («Открыть выпуск»), и кнопка срезает его по краям.
 * @param box Бокс сообщений, которому уже добавлены все кнопки.
 */
void fitMessageBoxButtons(QMessageBox& box);

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

/**
 * Ставит цветное состояние метки (QSS: QLabel[statusState="ok" | "error"]).
 * Палитрой не пользоваться: QSS `QWidget { color: ... }` её перебивает.
 * @param widget Метка.
 * @param state "ok", "error" или пустая строка — без цвета.
 */
void setStatusState(QWidget* widget, const QString& state);

} // namespace tgui::theme
