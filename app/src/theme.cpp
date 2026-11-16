#include "theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLayoutItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QVBoxLayout>

#include <algorithm>

#include "tgui/models.h"

namespace {

using tgui::theme::Icon;

/**
 * Сопоставляет роль иконки с SVG из макета или дополнительного набора.
 * @param id Роль иконки.
 */
QString iconPath(Icon id)
{
    switch (id) {
        case Icon::App:
            return QStringLiteral(":/icons/transmission-upstream.png");
        case Icon::Connect:
            return QStringLiteral(":/icons/i-conn.svg");
        case Icon::Disconnect:
            return QStringLiteral(":/icons/i-conn-off.svg");
        case Icon::Add:
            return QStringLiteral(":/icons/i-add.svg");
        case Icon::AddLink:
            return QStringLiteral(":/icons/i-magnet.svg");
        case Icon::Start:
            return QStringLiteral(":/icons/i-start.svg");
        case Icon::ForceStart:
            return QStringLiteral(":/icons/i-force.svg");
        case Icon::Pause:
            return QStringLiteral(":/icons/i-pause.svg");
        case Icon::Remove:
            return QStringLiteral(":/icons/i-remove.svg");
        case Icon::Properties:
            return QStringLiteral(":/icons/i-props.svg");
        case Icon::Move:
            return QStringLiteral(":/icons/i-move.svg");
        case Icon::Refresh:
            return QStringLiteral(":/icons/i-refresh.svg");
        case Icon::AltSpeed:
            return QStringLiteral(":/icons/i-alt.svg");
        case Icon::Queue:
            return QStringLiteral(":/icons/i-queue.svg");
        case Icon::QueueUp:
            return QStringLiteral(":/icons/i-up.svg");
        case Icon::QueueDown:
            return QStringLiteral(":/icons/i-down.svg");
        case Icon::DaemonOptions:
            return QStringLiteral(":/icons/i-daemon.svg");
        case Icon::AppOptions:
            return QStringLiteral(":/icons/i-opt.svg");
        case Icon::About:
        case Icon::TabGeneral:
            return QStringLiteral(":/icons/i-info.svg");
        case Icon::Search:
            return QStringLiteral(":/icons/i-search.svg");
        case Icon::Folder:
            return QStringLiteral(":/icons/i-folder.svg");
        case Icon::Film:
            return QStringLiteral(":/icons/i-film.svg");
        case Icon::Label:
            return QStringLiteral(":/icons/i-label.svg");
        case Icon::LabelVideo:
            return QStringLiteral(":/icons/lb-video.svg");
        case Icon::LabelSerials:
            return QStringLiteral(":/icons/lb-serials.svg");
        case Icon::LabelMusic:
            return QStringLiteral(":/icons/lb-music.svg");
        case Icon::LabelBooks:
            return QStringLiteral(":/icons/lb-books.svg");
        case Icon::LabelGames:
            return QStringLiteral(":/icons/lb-games.svg");
        case Icon::LabelSoft:
            return QStringLiteral(":/icons/lb-soft.svg");
        case Icon::LabelPictures:
            return QStringLiteral(":/icons/lb-pictures.svg");
        case Icon::Copy:
            return QStringLiteral(":/icons/i-copy.svg");
        case Icon::Warning:
            return QStringLiteral(":/icons/i-warn.svg");
        case Icon::Question:
            return QStringLiteral(":/icons/i-q.svg");
        case Icon::Information:
            return QStringLiteral(":/icons/i-inf32.svg");
        case Icon::Tracker:
        case Icon::TabTrackers:
            return QStringLiteral(":/icons/i-tracker.svg");
        case Icon::FilterAll:
            return QStringLiteral(":/icons/i-all.svg");
        case Icon::FilterDownloading:
        case Icon::StatusDown:
            return QStringLiteral(":/icons/st-dl.svg");
        case Icon::FilterCompleted:
        case Icon::StatusDone:
            return QStringLiteral(":/icons/st-fin.svg");
        case Icon::FilterActive:
            return QStringLiteral(":/icons/i-act.svg");
        case Icon::FilterInactive:
            return QStringLiteral(":/icons/i-inact.svg");
        case Icon::FilterStopped:
        case Icon::StatusStopped:
            return QStringLiteral(":/icons/st-stop.svg");
        case Icon::FilterError:
        case Icon::StatusError:
            return QStringLiteral(":/icons/st-err.svg");
        case Icon::FilterWaiting:
        case Icon::StatusWaiting:
            return QStringLiteral(":/icons/st-wait.svg");
        case Icon::StatusSeed:
            return QStringLiteral(":/icons/st-seed.svg");
        case Icon::StatusVerifying:
            return QStringLiteral(":/icons/st-check.svg");
        case Icon::TabPeers:
            return QStringLiteral(":/icons/i-peer.svg");
        case Icon::TabFiles:
            return QStringLiteral(":/icons/i-file.svg");
        case Icon::TabStatistics:
            return QStringLiteral(":/icons/i-stat.svg");
    }
    return {};
}

/**
 * Рендерит SVG в физическом разрешении экрана.
 * @param renderer Загруженный SVG.
 * @param size Размер в логических пикселях.
 * @param dpr Масштаб экрана.
 */
QPixmap renderIcon(QSvgRenderer& renderer, int size, qreal dpr)
{
    const int pixels = qRound(size * dpr);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
    }
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

/**
 * Уменьшает непрозрачность готового значка, как у отключённой кнопки в макете.
 * @param normal Обычное изображение значка.
 */
QPixmap disabledIcon(const QPixmap& normal)
{
    QPixmap disabled(normal.size());
    disabled.setDevicePixelRatio(normal.devicePixelRatio());
    disabled.fill(Qt::transparent);
    {
        QPainter painter(&disabled);
        painter.setOpacity(0.38);
        painter.drawPixmap(QPoint(0, 0), normal);
    }
    return disabled;
}

} // namespace

namespace tgui::theme {

namespace {

constexpr auto progressColorsKey = "ui/progressColors";
constexpr std::array progressColorNames{"downloading", "seeding", "finished", "waiting",
                                        "verifying", "paused", "error"};
static_assert(progressColorNames.size() == static_cast<size_t>(ProgressState::Count));

/** Хранит применённые цвета без чтения QSettings во время отрисовки. */
ProgressColors& activeProgressColors()
{
    static auto colors = loadProgressColors(QSettings());
    return colors;
}

} // namespace

const ProgressColors& defaultProgressColors()
{
    static const ProgressColors colors{
        palette().blue,          // Downloading
        QColor(0x3F, 0xAE, 0x5A), // Seeding
        QColor(0x39, 0x9D, 0x50), // Finished
        QColor(0xE7, 0xB4, 0x5A), // Waiting
        QColor(0xE7, 0xB4, 0x5A), // Verifying
        QColor(0xA3, 0xA3, 0xA3), // Paused
        QColor(0xE0, 0x67, 0x6D), // Error
    };
    return colors;
}

ProgressColors loadProgressColors(const QSettings& settings)
{
    auto colors = defaultProgressColors();
    const auto values = settings.value(progressColorsKey).toMap();
    for (size_t i = 0; i < colors.size(); ++i) {
        const QColor color(values.value(QLatin1String(progressColorNames[i])).toString());
        if (color.isValid())
            colors[i] = QColor(color.red(), color.green(), color.blue());
    }
    return colors;
}

ProgressState torrentProgressState(int status, bool finished, bool error)
{
    if (error)
        return ProgressState::Error;
    switch (static_cast<TorrentStatus>(status)) {
        case TorrentStatus::Download: return ProgressState::Downloading;
        case TorrentStatus::Seed: return ProgressState::Seeding;
        case TorrentStatus::Check: return ProgressState::Verifying;
        case TorrentStatus::CheckWait:
        case TorrentStatus::DownloadWait:
        case TorrentStatus::SeedWait: return ProgressState::Waiting;
        default: return finished ? ProgressState::Finished : ProgressState::Paused;
    }
}

QColor progressColor(ProgressState state)
{
    return activeProgressColors()[static_cast<size_t>(state)];
}

const Palette& palette()
{
    // Светлая палитра нового дизайна — значения из design/mockup.html
    // (таблицы DESIGN_PLAN 0.1/0.2; расхождения с макетом зафиксированы там же).
    static const Palette p{
        // Поверхности
        QColor(0xF0, 0xF0, 0xF0), // window
        QColor(0xFF, 0xFF, 0xFF), // panel
        QColor(0xFA, 0xFB, 0xFC), // zebra
        QColor(0xE1, 0xE1, 0xE1), // section
        QColor(0xFF, 0xFF, 0xFF), // headerBg
        QColor(0xEE, 0xF6, 0xFD), // listHeaderHover
        QColor(0xE9, 0xF2, 0xFB), // detailTabHover
        // Границы
        QColor(0xD5, 0xD5, 0xD5), // border
        QColor(0xE9, 0xE9, 0xE9), // borderSoft
        QColor(0xD5, 0xD5, 0xD5), // layoutBorder
        QColor(0xF3, 0xF3, 0xF3), // listRowBorder
        QColor(0xC8, 0xC8, 0xC8), // menuBorder
        QColor(0xC3, 0xDC, 0xF5), // menuSelectionBorder
        QColor(0xD7, 0xD7, 0xD7), // menuSeparator
        QColor(0xA0, 0xA0, 0xA0), // menuDisabledText
        QColor(0xE1, 0xEC, 0xF7), // statusRateHover
        QColor(0xAA, 0xAA, 0xAA), // statusOfflineDot
        // Текст
        QColor(0x1B, 0x1B, 0x1B), // text
        QColor(0x6D, 0x6D, 0x6D), // textMuted
        QColor(0x7A, 0x7A, 0x7A), // textFaint
        QColor(0x33, 0x33, 0x33), // iconSlate
        QColor(0x44, 0x44, 0x44), // detailFieldLabel
        QColor(0x22, 0x22, 0x22), // formLabel
        // Акцент
        QColor(0x00, 0x67, 0xC0), // accent
        QColor(0x00, 0x53, 0x9E), // accentDark
        QColor(0x1A, 0x88, 0xE0), // accentHover
        // Статусы
        QColor(0x2F, 0x9E, 0x4A), // green
        QColor(0xE7, 0xF6, 0xED), // greenLight
        QColor(0xD0, 0x8A, 0x14), // amber
        QColor(0xFD, 0xF3, 0xDC), // amberLight
        QColor(0xD9, 0x36, 0x3E), // red
        QColor(0xFB, 0xEA, 0xEA), // redLight
        QColor(0x7A, 0x65, 0xC0), // purple
        QColor(0x55, 0x55, 0x55), // statusNeutral
        // Текст статуса загрузки
        QColor(0x2B, 0x7B, 0xD6), // blue
        // Состояния
        QColor(0xE5, 0xF3, 0xFF), // hoverBg
        QColor(0xCC, 0xE8, 0xFF), // selBg
        QColor(0xB9, 0xDC, 0xFB), // selBgActive
        QColor(0xDC, 0xEA, 0xF7), // selBgInactive
        QColor(0xCC, 0xE8, 0xFF), // itemSelBg
        QColor(0x1B, 0x1B, 0x1B), // itemSelText
        QColor(0x99, 0x99, 0x99), // disabledText
        QColor(0xCC, 0xE8, 0xFF), // highlight
        QColor(0x1B, 0x1B, 0x1B), // highlightText
        // Новые элементы
        QColor(0x2B, 0x2B, 0x2B), // toastBackground
        QColor(0xF1, 0xF4, 0xF7), // groupRow
        QColor(0xF9, 0xF9, 0xF9), // toolbarBg
        QColor(0xD3, 0xD3, 0xD3), // toolbarSep
        QColor(0x99, 0xD1, 0xFF), // selectionBorder
        QColor(0xFF, 0xF6, 0xF5), // connectOfflineBg
        QColor(0xE5, 0xA4, 0xA0), // connectOfflineBorder
        QColor(0xE9, 0xEC, 0xEF), // sectionHeader
        QColor(0x2D, 0x4A, 0x6B), // labelText
        // PieceBar
        QColor(0xE3, 0xE8, 0xEE), // pieceEmpty
        QColor(0x9F, 0xB6, 0xCF), // pieceBorder
        // Input (как в mockup .srch/input: НЕ accent)
        QColor(0xAB, 0xAD, 0xB3), // inputBorder
        QColor(0x00, 0x78, 0xD7), // inputFocus
        QColor(0xFF, 0xFF, 0xFF), // inputSelectionText
        QColor(0xFF, 0xFF, 0xB6), // tooltipBg
        // Кнопки диалогов (.btn)
        QColor(0xE1, 0xE1, 0xE1), // btnBg
        QColor(0xAD, 0xAD, 0xAD), // btnBorder
        QColor(0xE5, 0xF1, 0xFB), // btnHoverBg
        QColor(0xFB, 0xE9, 0xE9), // btnDangerHover
        QColor(0xEE, 0xEE, 0xEE), // btnDisabledBg
        QColor(0xCC, 0xCC, 0xCC), // btnDisabledBorder
        QColor(0x99, 0x99, 0x99), // btnDisabledText
        // GroupBox (.gb legend)
        QColor(0x1A, 0x4F, 0x8A), // groupTitle
        QColor(0xD0, 0xD0, 0xD0), // groupBorder
        // Нативный прогресс-бар
        QColor(0xE6, 0xE6, 0xE6), // progressBg
        QColor(0xBC, 0xBC, 0xBC), // progressBorder
        // Скроллбары
        QColor(0xC0, 0xC0, 0xC0), // scrollbarHandle
        QColor(0xA0, 0xA0, 0xA0), // scrollHover
        QColor(0x7F, 0x8A, 0x95), // dialogBorder
        QColor(0xC9, 0xC9, 0xC9)  // dialogTabBorder
    };
    return p;
}

int rowHeightFor(const QFont& font)
{
    // A.2: не фиксировать жёстко — при крупном шрифте строки растут.
    return std::max(dp(22), QFontMetrics(font).height() + dp(6));
}

QGridLayout* formGrid(QWidget* parent)
{
    // Фаза 6.2 (mockup .fr): подписи 200 px + гибкое поле, gap 8 px.
    auto* grid = new QGridLayout(parent);
    grid->setColumnMinimumWidth(0, dp(200));
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(dp(8));
    grid->setVerticalSpacing(dp(8));
    return grid;
}

namespace {

// Поле занимает всю колонку значений, как `.fv>input/select{flex:1}` в макете.
// У полей с ограниченной шириной (спинбоксы, короткие списки) остаётся
// прижатие к левому краю колонки, а не к правому краю диалога.
bool stretchesInForm(QWidget* field)
{
    if (field->maximumWidth() < QWIDGETSIZE_MAX)
        return false;
    if (qobject_cast<QComboBox*>(field)) {
        field->setSizePolicy(QSizePolicy::Expanding, field->sizePolicy().verticalPolicy());
        return true;
    }
    return (field->sizePolicy().horizontalPolicy() & QSizePolicy::ExpandFlag) != 0;
}

} // namespace

void addFormRow(QGridLayout* grid, const QString& label, QWidget* field, int row)
{
    const int at = row < 0 ? grid->rowCount() : row;
    if (label.isEmpty()) {
        // Пустая подпись (как QFormLayout::addRow(QString(), …)): поле в колонке полей.
        grid->addWidget(field, at, 1);
        return;
    }
    auto* host = grid->parentWidget();
    auto* lbl = new QLabel(label, host);
    lbl->setObjectName(QStringLiteral("formLabel"));
    lbl->setBuddy(field); // Alt-навигация к полю (Фаза 6.2).
    grid->addWidget(lbl, at, 0, Qt::AlignLeft | Qt::AlignVCenter);
    if (stretchesInForm(field))
        grid->addWidget(field, at, 1);
    else
        grid->addWidget(field, at, 1, Qt::AlignLeft | Qt::AlignVCenter);
}

void addFormRow(QGridLayout* grid, const QString& label, QLayout* field, int row)
{
    const int at = row < 0 ? grid->rowCount() : row;
    if (label.isEmpty()) {
        grid->addLayout(field, at, 1);
        return;
    }
    auto* host = grid->parentWidget();
    auto* lbl = new QLabel(label, host);
    lbl->setObjectName(QStringLiteral("formLabel"));
    // Buddy — первое поле внутри компоновки (ввод/выбор), если оно есть.
    const auto* first = field->itemAt(0);
    if (first && first->widget())
        lbl->setBuddy(first->widget());
    grid->addWidget(lbl, at, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addLayout(field, at, 1);
}

void addFullRow(QGridLayout* grid, QWidget* field, int row)
{
    const int at = row < 0 ? grid->rowCount() : row;
    grid->addWidget(field, at, 0, 1, 2);
}

void addFullRow(QGridLayout* grid, QLayout* field, int row)
{
    const int at = row < 0 ? grid->rowCount() : row;
    grid->addLayout(field, at, 0, 1, 2);
}

QIcon icon(Icon id)
{
    if (id == Icon::App) {
        static const QIcon appIcon(iconPath(Icon::App));
        return appIcon;
    }

    // Роли с одинаковым рисунком используют один набор пиксмапов.
    static QHash<QString, QIcon> cache;
    const QString path = iconPath(id);
    const auto found = cache.constFind(path);
    if (found != cache.cend())
        return *found;

    QSvgRenderer renderer(path);
    if (!renderer.isValid())
        return {};

    QIcon result;
    const auto addSize = [&](int size) {
        for (qreal dpr : {1.0, 1.25, 1.5, 2.0}) {
            const QPixmap normal = renderIcon(renderer, size, dpr);
            result.addPixmap(normal, QIcon::Normal);
            result.addPixmap(disabledIcon(normal), QIcon::Disabled);
        }
    };
    for (int size : {14, 16, 18, 20, 22, 24, 32})
        addSize(size);
    cache.insert(path, result);
    return result;
}

QIcon fileIcon(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    const bool isVideo = suffix == QStringLiteral("mkv") ||
                         suffix == QStringLiteral("mp4") ||
                         suffix == QStringLiteral("avi");
    return icon(isVideo ? Icon::Film : Icon::TabFiles);
}

QPixmap statusDotPixmap(const QColor& color, int size, qreal dpr)
{
    const int pixels = qRound(size * dpr);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal inset = 0.5 * dpr;
        painter.drawEllipse(QRectF(inset, inset, pixels - 2 * inset, pixels - 2 * inset));
    }
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

// DESIGN_PLAN 0.2: QSS генерируется из токенов палитры — без hex-литералов.
// Порядок правок не важен: имя каждого токена уникально (скобки %…% не пересекаются).
QString buildQss(const Palette& p)
{
    QString q = QStringLiteral(R"(
* { outline: none; }
QWidget { color: %text%; }
QMainWindow, QDialog { background: %window%; }
/* DLG (mockup .dlg/.dtt): тонкая рамка и своя белая шапка без системного хрома. */
QDialog#tguiDialog { background: %window%; border: 1px solid %dialogBorder%; }
QWidget#dialogTitleBar { background: %panel%; border-bottom: 1px solid %borderSoft%; }
QLabel#dialogTitle { background: transparent; color: %text%; }
QPushButton#dialogClose {
    background: transparent; border: none; border-radius: 0;
    min-width: 44px; max-width: 44px; min-height: 30px; max-height: 30px; padding: 0;
}
QPushButton#dialogClose:hover { background: %red%; color: %panel%; }
QWidget#dialogBody { background: %window%; }
QMenuBar { background: %panel%; border-bottom: 1px solid %layoutBorder%; padding: 1px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 3px; color: %text%; }
QMenuBar::item:selected { background: %selBg%; color: %text%; }
QMenuBar::item:pressed { background: %selBgActive%; }
QMenu {
    background: %toolbarBg%; color: %text%; border: 1px solid %menuBorder%;
    border-radius: 0; padding: 2px; min-width: 194px;
}
QMenu::item {
    min-height: 17px; padding: 2px 18px 2px 26px; border: 1px solid transparent;
}
QMenu::item:selected {
    background: %statusRateHover%; border-color: %menuSelectionBorder%; color: %text%;
}
QMenu::item:disabled {
    background: transparent; border-color: transparent; color: %menuDisabledText%;
}
QMenu::icon, QMenu::indicator {
    width: 16px; height: 16px; subcontrol-origin: border; subcontrol-position: left center;
    left: 6px;
}
QMenu::right-arrow {
    width: 8px; height: 8px; subcontrol-origin: border; subcontrol-position: right center;
    right: 6px;
}
QMenu::separator { height: 1px; background: %menuSeparator%; margin: 3px 2px 3px 28px; }
QToolBar { background: %toolbarBg%; border: none; border-bottom: 1px solid %layoutBorder%; spacing: 1px; padding: 3px 6px; }
QToolBar::separator { background: %toolbarSep%; width: 1px; margin: 5px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 3px; }
QToolButton:hover { background: %hoverBg%; border-color: %border%; }
QToolButton:pressed { background: %selBg%; }
QToolButton:checked { background: %selBg%; border-color: %inputFocus%; }
QToolBar#mainToolBar > QToolButton[toolbarAction="true"] { min-width: 22px; padding: 2px 6px; }
QToolBar#mainToolBar > QToolButton[toolbarAction="true"]:hover { border-color: %selBg%; }
QToolBar#mainToolBar > QToolButton[toolbarAction="true"]:pressed { background: %selBgActive%; }
QToolBar#mainToolBar > QToolButton[toolbarAction="true"]:checked { border-color: %selectionBorder%; }
QToolBar#mainToolBar > QToolButton[toolbarAction="true"]:focus { border-color: %inputFocus%; }
QToolBar#mainToolBar > QToolButton[toolbarAction="true"]:disabled { color: %disabledText%; }
QToolBar#mainToolBar > QToolButton#queueButton { padding-right: 16px; }
QToolButton#queueButton::menu-indicator {
    width: 8px;
    height: 8px;
    subcontrol-origin: padding;
    subcontrol-position: right center;
    right: 3px;
}
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTimeEdit, QPlainTextEdit, QTextEdit {
    background: %panel%;
    border: 1px solid %inputBorder%;
    border-radius: 3px;
    padding: 4px 7px;
    min-height: 18px;
    selection-background-color: %inputFocus%;
    selection-color: %inputSelectionText%;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QTimeEdit:focus,
QPlainTextEdit:focus, QTextEdit:focus { border-color: %inputFocus%; }
QLineEdit#searchEdit { min-height: 24px; padding: 0 5px; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,
QTimeEdit:disabled {
    background: %window%;
    color: %disabledText%;
    border-color: %borderSoft%;
}
QComboBox::drop-down { border: none; width: 20px; }
QComboBox#replacementTorrent::down-arrow {
    image: url(:/icons/spin-down.svg); width: 7px; height: 7px;
}
QComboBox#replacementTorrent::down-arrow:disabled {
    image: url(:/icons/spin-down-disabled.svg);
}
QComboBox QAbstractItemView {
    background: %panel%;
    border: 1px solid %inputBorder%;
    selection-background-color: %selBg%;
    selection-color: %text%;
    padding: 2px;
}
/* Фаза 0.4/6.2: поля диалогов — без скругления (mockup .fr input). */
QDialog QLineEdit, QDialog QComboBox, QDialog QSpinBox, QDialog QDoubleSpinBox,
QDialog QTimeEdit, QDialog QPlainTextEdit, QDialog QTextEdit { border-radius: 0; }
/* Спинбоксы: при включённом QSS кнопки «вверх/вниз» рисуются только если
   заданы их геометрия и стрелки — иначе остаются пустые рамки. */
QSpinBox, QDoubleSpinBox, QTimeEdit { padding-right: 17px; }
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button,
QTimeEdit::up-button, QTimeEdit::down-button {
    subcontrol-origin: border;
    width: 16px;
    margin: 1px;
    background: %panel%;
    border-left: 1px solid %inputBorder%;
}
QSpinBox::up-button, QDoubleSpinBox::up-button, QTimeEdit::up-button {
    subcontrol-position: top right;
    border-bottom: 1px solid %inputBorder%;
}
QSpinBox::down-button, QDoubleSpinBox::down-button, QTimeEdit::down-button {
    subcontrol-position: bottom right;
}
QSpinBox::up-button:hover, QSpinBox::down-button:hover,
QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover,
QTimeEdit::up-button:hover, QTimeEdit::down-button:hover { background: %hoverBg%; }
QSpinBox::up-button:pressed, QSpinBox::down-button:pressed,
QDoubleSpinBox::up-button:pressed, QDoubleSpinBox::down-button:pressed,
QTimeEdit::up-button:pressed, QTimeEdit::down-button:pressed { background: %selBg%; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QTimeEdit::up-arrow {
    image: url(:/icons/spin-up.svg); width: 7px; height: 7px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow, QTimeEdit::down-arrow {
    image: url(:/icons/spin-down.svg); width: 7px; height: 7px;
}
QSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off,
QDoubleSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:off,
QTimeEdit::up-arrow:disabled, QTimeEdit::up-arrow:off {
    image: url(:/icons/spin-up-disabled.svg);
}
QSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off,
QDoubleSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:off,
QTimeEdit::down-arrow:disabled, QTimeEdit::down-arrow:off {
    image: url(:/icons/spin-down-disabled.svg);
}
/* Фаза 6.1 (.btn): классические кнопки диалогов — без заливки, default и
   danger выделяются рамкой. */
QPushButton { background: %btnBg%; border: 1px solid %btnBorder%; border-radius: 0; min-width: 78px; min-height: 24px; padding: 0 10px; }
QPushButton:hover { background: %btnHoverBg%; border-color: %inputFocus%; }
QPushButton:focus { border-color: %inputFocus%; }
QPushButton:pressed { background: %btnHoverBg%; }
QPushButton:default { background: %btnBg%; border: 2px solid %inputFocus%; }
QPushButton:default:hover { background: %btnHoverBg%; }
QPushButton[danger="true"] { border: 2px solid %red%; }
QPushButton[danger="true"]:hover { background: %btnDangerHover%; }
QPushButton[danger="true"]:disabled { background: %btnDisabledBg%; border-color: %btnDisabledBorder%; color: %btnDisabledText%; }
QPushButton:disabled { background: %btnDisabledBg%; border-color: %btnDisabledBorder%; color: %btnDisabledText%; }
QTreeView, QTableView, QListView {
    background: %panel%;
    alternate-background-color: %zebra%;
    border: 1px solid %border%;
    border-radius: 2px;
    selection-background-color: %itemSelBg%;
    selection-color: %itemSelText%;
    gridline-color: %section%;
}
QTreeView::item, QTableView::item, QListView::item { padding: 2px 5px; }
QTreeView::item:selected, QTableView::item:selected, QListView::item:selected {
    background: %itemSelBg%;
    color: %itemSelText%;
}
QHeaderView { background: %headerBg%; }
QHeaderView::section {
    background: %headerBg%;
    color: %text%;
    border: none;
    border-right: 1px solid %borderSoft%;
    border-bottom: 1px solid %border%;
    padding: 4px 7px;
    font-weight: 600;
}
QHeaderView::section:hover { background: %hoverBg%; }
QHeaderView#torrentHeader::section { min-height: 23px; padding: 0 6px; font-weight: normal; color: %iconSlate%; }
QHeaderView#torrentHeader::section:hover { background: %listHeaderHover%; }
QTableCornerButton::section { background: %headerBg%; border: none; }
QTabWidget::pane { border: 1px solid %border%; border-radius: 0; background: %panel%; top: -1px; }
QTabBar { background: %window%; }
QTabBar::tab {
    background: %window%;
    color: %text%;
    border: 1px solid transparent;
    border-bottom: none;
    padding: 5px 12px;
    margin-right: 2px;
    margin-top: 2px;
}
QTabBar::tab:hover { background: %hoverBg%; }
QTabBar::tab:selected {
    background: %panel%;
    border-color: %border%;
    border-bottom-color: %panel%;
    margin-top: 0;
    padding-top: 7px;
}
QTabBar::tab:disabled { color: %disabledText%; }
QDialog#tguiDialog QTabWidget::pane {
    background: %panel%; border: 1px solid %dialogTabBorder%; border-radius: 0; top: -1px;
}
QDialog#tguiDialog QTabBar::tab {
    background: transparent; border: 1px solid transparent;
    padding: 3px 10px 4px; margin: 0;
}
QDialog#tguiDialog QTabBar::tab:hover { background: %detailTabHover%; }
QDialog#tguiDialog QTabBar::tab:selected {
    background: %panel%; border-color: %dialogTabBorder%; border-bottom-color: %panel%;
}
QTabWidget#infoPanel::pane { border: none; border-top: 1px solid %border%; top: -1px; }
QTabWidget#infoPanel QTabBar::tab {
    background: transparent;
    padding: 3px 10px 4px;
    margin: 3px 0 0;
    border: 1px solid transparent;
    border-bottom: none;
    border-radius: 2px;
}
QTabWidget#infoPanel QTabBar::tab:hover { background: %detailTabHover%; }
QTabWidget#infoPanel QTabBar::tab:selected {
    background: %panel%;
    border-color: %border%;
    border-bottom: 1px solid %panel%;
    padding-bottom: 3px;
}
QLabel#infoTorrentTitle { color: %textMuted%; padding: 0 8px 3px; }
QLabel#peerEmpty { color: %textMuted%; }
QTabWidget#infoPanel QHeaderView::section, QTableWidget#statisticsTable QHeaderView::section {
    padding: 3px 8px; font-weight: normal;
}
QTabWidget#infoPanel QTreeView, QTabWidget#infoPanel QTableView, QTableWidget#statisticsTable {
    border: none; border-radius: 0;
}
QTabWidget#infoPanel QTreeView::item, QTabWidget#infoPanel QTableView::item, QTableWidget#statisticsTable::item {
    background: %panel%; min-height: 16px; padding: 2px 8px; border-bottom: 1px solid %listRowBorder%;
}
QTabWidget#infoPanel QTreeView::item:hover, QTabWidget#infoPanel QTableView::item:hover,
QTableWidget#statisticsTable::item:hover { background: %hoverBg%; }
QTabWidget#infoPanel QTreeView::item:selected, QTabWidget#infoPanel QTableView::item:selected,
QTableWidget#statisticsTable::item:selected { background: %selBg%; color: %text%; }
)");
    q += QStringLiteral(R"(
QStatusBar { background: %window%; border-top: 1px solid %layoutBorder%; color: %text%; min-height: 22px; }
QStatusBar::item { border: none; }
QWidget#statusStrip { background: %window%; }
QWidget#statusSegmentGroup, QLabel#statusSegment { border-right: 1px solid %border%; }
QLabel#statusSegment { padding: 0 10px; }
QToolButton#statusRate { border: none; border-right: 1px solid %border%; border-radius: 0; padding: 0 10px; }
QToolButton#statusRate:hover, QToolButton#statusRate:pressed { background: %statusRateHover%; }
QToolButton#statusRate:focus { border-bottom: 1px solid %inputFocus%; }
QToolButton#statusRate::menu-indicator { image: none; width: 0; }
QSplitter::handle { background: %layoutBorder%; }
QSplitter::handle:hover { background: %scrollHover%; }
QSplitter::handle:vertical { height: 4px; }
QSplitter::handle:horizontal { width: 4px; }
QScrollBar { background: %window%; border: none; margin: 0; }
QScrollBar:vertical { width: 14px; border-left: 1px solid %border%; }
QScrollBar:horizontal { height: 14px; border-top: 1px solid %border%; }
QScrollBar::handle {
    background: %scrollbarHandle%; border: 1px solid %border%; margin: 1px;
}
QScrollBar::handle:vertical { min-height: 24px; }
QScrollBar::handle:horizontal { min-width: 24px; }
QScrollBar::handle:hover, QScrollBar::handle:pressed { background: %scrollHover%; }
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0; height: 0; border: none; background: transparent;
}
QScrollBar::up-arrow, QScrollBar::down-arrow, QScrollBar::left-arrow, QScrollBar::right-arrow {
    image: none; width: 0; height: 0;
}
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QAbstractScrollArea::corner { background: %window%; }
QProgressBar {
    background: %progressBg%;
    border: 1px solid %progressBorder%;
    border-radius: 0;
    min-height: 12px;
    max-height: 12px;
    text-align: center;
    color: %text%;
    font-weight: 600;
}
QProgressBar::chunk { border-radius: 0; background: %progressBlue%; }
/* Фаза 6.3 (mockup .gb): рамка + синий legend, не наезжающий на рамку. */
QGroupBox {
    background: %panel%;
    border: 1px solid %groupBorder%;
    border-radius: 0;
    margin-top: 2px;
    padding: 6px 10px 10px;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %groupTitle%; }
QGroupBox#addTorrentGroup { background: transparent; }
/* Вкладка «General»: секции «Transfer»/«Torrent» — плоские блоки
   (вкладку Statistics тоже оформили так же). */
QFrame#sectionPanel { background: transparent; border: none; }
QLabel#sectionTitle { background: %sectionHeader%; color: %text%; font-weight: 600; padding: 3px 8px; }
QLabel#fieldLabel { color: %detailFieldLabel%; }
QLabel#fieldValue { color: %text%; }
QLabel#formLabel { color: %formLabel%; }
QLabel#hintLabel, QLabel#aboutVersion { color: %textMuted%; }
QDialog[aboutDialog="true"] QWidget#dialogTitleBar,
QGroupBox#donateGroup, QPlainTextEdit#licenseText { background: %window%; }
QLabel#donateQr { background: transparent; }
QToolButton#licenseToggle { border: none; background: transparent; color: %accent%; padding: 2px 0; }
QToolButton#licenseToggle:hover { text-decoration: underline; }
QToolButton#licenseToggle:checked { background: transparent; }
QLabel#toast { background: %toastBackground%; color: %panel%; border-radius: 3px; padding: 7px 14px; }
QPushButton#propertyLink, QPushButton#propertyLink:hover,
QPushButton[linkStyle="true"], QPushButton[linkStyle="true"]:hover {
    color: %accent%; background: transparent; border: none; min-width: 0; min-height: 0; padding: 0;
}
QPushButton#propertyLink:hover, QPushButton[linkStyle="true"]:hover { text-decoration: underline; }
QPushButton#propertyLink:focus, QPushButton[linkStyle="true"]:focus { border-bottom: 1px solid %accent%; }
QPushButton#propertyLink:disabled, QPushButton[linkStyle="true"]:disabled { color: %disabledText%; }
QLabel#statsHeader { color: %text%; font-weight: 700; padding: 4px 0; border-bottom: 1px solid %border%; }
QWidget#filterPanel { background: %panel%; border-right: 1px solid %border%; }
QTreeWidget#filterTree { background: %panel%; border: none; border-radius: 0; }
QTreeWidget#filterTree::item:selected { background: transparent; color: %text%; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QToolTip { background: %tooltipBg%; color: %text%; border: 1px solid %border%; padding: 3px 6px; border-radius: 0; }
QCheckBox, QRadioButton { spacing: 6px; }
)");
    const struct {
        const char* token;
        const QColor& color;
    } subs[] = {
        {"%window%", p.window},
        {"%headerBg%", p.headerBg},
        {"%listHeaderHover%", p.listHeaderHover},
        {"%detailTabHover%", p.detailTabHover},
        {"%detailFieldLabel%", p.detailFieldLabel},
        {"%formLabel%", p.formLabel},
        {"%listRowBorder%", p.listRowBorder},
        {"%menuBorder%", p.menuBorder},
        {"%menuSelectionBorder%", p.menuSelectionBorder},
        {"%menuSeparator%", p.menuSeparator},
        {"%menuDisabledText%", p.menuDisabledText},
        {"%statusRateHover%", p.statusRateHover},
        {"%sectionHeader%", p.sectionHeader},
        {"%panel%", p.panel},
        {"%zebra%", p.zebra},
        {"%section%", p.section},
        {"%border%", p.border},
        {"%borderSoft%", p.borderSoft},
        {"%layoutBorder%", p.layoutBorder},
        {"%text%", p.text},
        {"%textMuted%", p.textMuted},
        {"%iconSlate%", p.iconSlate},
        {"%accent%", p.accent},
        {"%disabledText%", p.disabledText},
        {"%accentDark%", p.accentDark},
        {"%accentHover%", p.accentHover},
        {"%hoverBg%", p.hoverBg},
        {"%selBg%", p.selBg},
        {"%selBgActive%", p.selBgActive},
        {"%itemSelBg%", p.itemSelBg},
        {"%itemSelText%", p.itemSelText},
        {"%toolbarBg%", p.toolbarBg},
        {"%toolbarSep%", p.toolbarSep},
        {"%selectionBorder%", p.selectionBorder},
        {"%inputBorder%", p.inputBorder},
        {"%inputFocus%", p.inputFocus},
        {"%inputSelectionText%", p.inputSelectionText},
        {"%tooltipBg%", p.tooltipBg},
        {"%btnBg%", p.btnBg},
        {"%btnBorder%", p.btnBorder},
        {"%btnHoverBg%", p.btnHoverBg},
        {"%btnDangerHover%", p.btnDangerHover},
        {"%btnDisabledBg%", p.btnDisabledBg},
        {"%btnDisabledBorder%", p.btnDisabledBorder},
        {"%btnDisabledText%", p.btnDisabledText},
        {"%red%", p.red},
        {"%groupTitle%", p.groupTitle},
        {"%groupBorder%", p.groupBorder},
        {"%progressBg%", p.progressBg},
        {"%progressBorder%", p.progressBorder},
        {"%progressBlue%", progressColor(ProgressState::Downloading)},
        {"%scrollbarHandle%", p.scrollbarHandle},
        {"%scrollHover%", p.scrollHover},
        {"%dialogBorder%", p.dialogBorder},
        {"%toastBackground%", p.toastBackground},
        {"%dialogTabBorder%", p.dialogTabBorder},
    };
    for (const auto& s : subs)
        q.replace(QLatin1String(s.token), s.color.name());
    return q;
}

/** Записывает только выбранные отличия от стандартных цветов и обновляет виджеты. */
void saveProgressColors(QSettings& settings, const ProgressColors& colors)
{
    QVariantMap values;
    const auto& defaults = defaultProgressColors();
    for (size_t i = 0; i < colors.size(); ++i) {
        if (colors[i] != defaults[i])
            values.insert(QLatin1String(progressColorNames[i]), colors[i].name());
    }
    if (values.isEmpty())
        settings.remove(progressColorsKey);
    else
        settings.setValue(progressColorsKey, values);

    auto& active = activeProgressColors();
    if (active == colors)
        return;
    active = colors;
    if (qApp && !qApp->styleSheet().isEmpty())
        qApp->setStyleSheet(buildQss(palette()));
}

void apply(QApplication& app)
{
    activeProgressColors() = loadProgressColors(QSettings());
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const Palette& pal = palette();
    QPalette p;
    p.setColor(QPalette::Window, pal.window);
    p.setColor(QPalette::WindowText, pal.text);
    p.setColor(QPalette::Base, pal.panel);
    p.setColor(QPalette::AlternateBase, pal.zebra);
    p.setColor(QPalette::Text, pal.text);
    p.setColor(QPalette::Button, pal.panel);
    p.setColor(QPalette::ButtonText, pal.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, pal.highlight);
    p.setColor(QPalette::HighlightedText, pal.highlightText);
    p.setColor(QPalette::ToolTipBase, pal.text);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::PlaceholderText, pal.textFaint);
    p.setColor(QPalette::Link, pal.accent);
    p.setColor(QPalette::LinkVisited, pal.accentDark);
    p.setColor(QPalette::Disabled, QPalette::Text, pal.textFaint);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, pal.textFaint);
    p.setColor(QPalette::Disabled, QPalette::WindowText, pal.textFaint);
    app.setPalette(p);

    QFont font;
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("SF Pro Text"),
                      QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")});
    font.setPointSize(9);
    app.setFont(font);

    app.setStyleSheet(buildQss(pal));
}

namespace {

/** Перетаскивание диалога за собственную шапку (системного заголовка нет). */
class DialogDragFilter : public QObject {
public:
    explicit DialogDragFilter(QDialog* dialog) : QObject(dialog), m_dialog(dialog) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* press = static_cast<QMouseEvent*>(event);
            if (press->button() != Qt::LeftButton)
                break;
            m_dragging = true;
            m_offset = m_dialog->mapFromGlobal(press->globalPosition().toPoint());
            return true;
        }
        case QEvent::MouseMove: {
            auto* move = static_cast<QMouseEvent*>(event);
            if (!m_dragging || !(move->buttons() & Qt::LeftButton))
                break;
            m_dialog->move(move->globalPosition().toPoint() - m_offset);
            return true;
        }
        case QEvent::MouseButtonRelease:
            m_dragging = false;
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QDialog* m_dialog = nullptr;
    QPoint m_offset;
    bool m_dragging = false;
};

} // namespace

void decorateDialog(QDialog* dialog)
{
    // Mockup .dlg: тонкая рамка и своя шапка вместо системного заголовка.
    if (!dialog || dialog->property("tguiFramed").toBool())
        return;
    auto* contentLayout = dialog->layout();
    if (!contentLayout)
        return;

    dialog->setProperty("tguiFramed", true);
    dialog->setObjectName(QStringLiteral("tguiDialog"));
    dialog->setWindowFlag(Qt::FramelessWindowHint, true);
    dialog->setAttribute(Qt::WA_StyledBackground, true);

    // setLayout переносит саму раскладку и перепривязывает её виджеты
    // к body. Перенос отдельных QLayoutItem оставлял поля под фоном body.
    const auto sizeConstraint = contentLayout->sizeConstraint();
    contentLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    auto* body = new QWidget(dialog);
    body->setObjectName(QStringLiteral("dialogBody"));
    body->setAttribute(Qt::WA_StyledBackground, true);
    body->setLayout(contentLayout);

    auto* header = new QWidget(dialog);
    header->setObjectName(QStringLiteral("dialogTitleBar"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    header->setFixedHeight(dp(30));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(dp(10), 0, 0, 0);
    headerLayout->setSpacing(0);
    auto* title = new QLabel(dialog->windowTitle(), header);
    title->setObjectName(QStringLiteral("dialogTitle"));
    title->setTextFormat(Qt::PlainText);
    auto* close = new QPushButton(QStringLiteral("✕"), header);
    close->setObjectName(QStringLiteral("dialogClose"));
    close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(QObject::tr("Close"));
    close->setCursor(Qt::ArrowCursor);
    headerLayout->addWidget(title, 1);
    headerLayout->addWidget(close);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::reject);

    auto* rootLayout = new QVBoxLayout(dialog);
    rootLayout->setSizeConstraint(sizeConstraint);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(header);
    rootLayout->addWidget(body, 1);

    auto* drag = new DialogDragFilter(dialog);
    header->installEventFilter(drag);
    title->installEventFilter(drag);
}

} // namespace tgui::theme
