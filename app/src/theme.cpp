#include "theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QSvgRenderer>

#include <algorithm>

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
            return QStringLiteral(":/icons/i-app.svg");
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
        QColor(0xE1, 0xEC, 0xF7), // statusRateHover
        QColor(0xAA, 0xAA, 0xAA), // statusOfflineDot
        // Текст
        QColor(0x1B, 0x1B, 0x1B), // text
        QColor(0x6D, 0x6D, 0x6D), // textMuted
        QColor(0x7A, 0x7A, 0x7A), // textFaint
        QColor(0x33, 0x33, 0x33), // iconSlate
        QColor(0x44, 0x44, 0x44), // detailFieldLabel
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
        // Прогресс/куски
        QColor(0x2B, 0x7B, 0xD6), // blue
        QColor(0x3F, 0xAE, 0x5A), // progressGreen
        QColor(0xE7, 0xB4, 0x5A), // progressAmber
        QColor(0xE0, 0x67, 0x6D), // progressRed
        QColor(0xA3, 0xA3, 0xA3), // progressStopped
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
        QColor(0xF0, 0x7C, 0x12), // newBadge
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
        // Нативный прогресс-бар
        QColor(0xE6, 0xE6, 0xE6), // progressBg
        QColor(0xBC, 0xBC, 0xBC), // progressBorder
        // Скроллбары
        QColor(0xC0, 0xC0, 0xC0), // scrollbarHandle
        QColor(0xA0, 0xA0, 0xA0)  // scrollHover
    };
    return p;
}

int rowHeightFor(const QFont& font)
{
    // A.2: не фиксировать жёстко — при крупном шрифте строки растут.
    return std::max(dp(22), QFontMetrics(font).height() + dp(6));
}

QIcon icon(Icon id)
{
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
    if (id == Icon::App) {
        addSize(48);
        addSize(64);
    }
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
QMenuBar { background: %panel%; border-bottom: 1px solid %layoutBorder%; padding: 1px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 3px; color: %text%; }
QMenuBar::item:selected { background: %selBg%; color: %text%; }
QMenuBar::item:pressed { background: %selBgActive%; }
QMenu { background: %panel%; border: 1px solid %border%; border-radius: 4px; padding: 3px; }
QMenu::item { padding: 5px 28px 5px 10px; border-radius: 3px; }
QMenu::item:selected { background: %selBg%; color: %text%; }
QMenu::item:disabled { color: %disabledText%; }
QMenu::separator { height: 1px; background: %borderSoft%; margin: 4px 8px; }
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
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: %panel%;
    border: 1px solid %inputBorder%;
    border-radius: 3px;
    padding: 4px 7px;
    min-height: 18px;
    selection-background-color: %inputFocus%;
    selection-color: %inputSelectionText%;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: %inputFocus%; }
QLineEdit#searchEdit { min-height: 24px; padding: 0 5px; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    background: %window%;
    color: %disabledText%;
    border-color: %borderSoft%;
}
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView {
    background: %panel%;
    border: 1px solid %inputBorder%;
    selection-background-color: %selBg%;
    selection-color: %text%;
    padding: 2px;
}
QPushButton { background: %btnBg%; border: 1px solid %btnBorder%; border-radius: 3px; padding: 5px 14px; min-height: 18px; }
QPushButton:hover { background: %btnHoverBg%; border-color: %inputFocus%; }
QPushButton:pressed { background: %selBg%; }
QPushButton:default { background: %inputFocus%; border-color: %accentDark%; color: %inputSelectionText%; }
QPushButton:default:hover { background: %accentHover%; }
QPushButton:disabled { background: %btnDisabledBg%; color: %btnDisabledText%; border-color: %btnDisabledBorder%; }
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
QScrollBar:vertical { background: %window%; width: 14px; margin: 0; border-left: 1px solid %borderSoft%; }
QScrollBar::handle:vertical { background: %scrollbarHandle%; min-height: 24px; border: 1px solid %border%; border-radius: 2px; margin: 1px; }
QScrollBar::handle:vertical:hover { background: %scrollHover%; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: %window%; height: 14px; margin: 0; border-top: 1px solid %borderSoft%; }
QScrollBar::handle:horizontal { background: %scrollbarHandle%; min-width: 24px; border: 1px solid %border%; border-radius: 2px; margin: 1px; }
QScrollBar::handle:horizontal:hover { background: %scrollHover%; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
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
QProgressBar::chunk { border-radius: 0; background: %blue%; }
QGroupBox {
    background: %panel%;
    border: 1px solid %border%;
    border-radius: 2px;
    margin-top: 14px;
    padding: 10px;
    font-weight: 600;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %text%; }
/* Вкладка «General»: секции «Transfer»/«Torrent» — плоские блоки
   (вкладку Statistics тоже оформили так же). */
QFrame#sectionPanel { background: transparent; border: none; }
QLabel#sectionTitle { background: %sectionHeader%; color: %text%; font-weight: 600; padding: 3px 8px; }
QLabel#fieldLabel { color: %detailFieldLabel%; }
QLabel#fieldValue { color: %text%; }
QPushButton#propertyLink, QPushButton#propertyLink:hover {
    color: %accent%; background: transparent; border: none; min-height: 0; padding: 0;
}
QPushButton#propertyLink:hover { text-decoration: underline; }
QPushButton#propertyLink:focus { border-bottom: 1px solid %accent%; }
QPushButton#propertyLink:disabled { color: %disabledText%; }
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
        {"%listRowBorder%", p.listRowBorder},
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
        {"%btnDisabledBg%", p.btnDisabledBg},
        {"%btnDisabledBorder%", p.btnDisabledBorder},
        {"%btnDisabledText%", p.btnDisabledText},
        {"%progressBg%", p.progressBg},
        {"%progressBorder%", p.progressBorder},
        {"%blue%", p.blue},
        {"%scrollbarHandle%", p.scrollbarHandle},
        {"%scrollHover%", p.scrollHover},
    };
    for (const auto& s : subs)
        q.replace(QLatin1String(s.token), s.color.name());
    return q;
}

void apply(QApplication& app)
{
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

} // namespace tgui::theme
