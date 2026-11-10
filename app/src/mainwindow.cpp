#include "mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QIcon>
#include <QFileDialog>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QWindowStateChangeEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

#include "addlinkdialog.h"
#include "adddialog.h"
#include "connectdialog.h"
#include "delegates.h"
#include "fileselectdialog.h"
#include "filterpanel.h"
#include "movedialog.h"
#include "connoptionsdialog.h"
#include "daemonoptionsdialog.h"
#include "colsetup.h"
#include "aboutdialog.h"
#include "daemonprofile.h"
#include "optionsdialog.h"
#include "pathmap.h"
#include "propsdialog.h"
#include "propsform.h"
#include "statsform.h"
#include "theme.h"
#include "tgui/util.h"

namespace {
constexpr int kRefreshIntervalMs = 2000;
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("transmissionGUI"));
    setWindowIcon(QIcon(tgui::theme::appIconPixmap(64)));
    resize(1200, 780);

    m_proxy.setSourceModel(&m_model);
    createUi();
    createActions();
    createMenus();
    createTray();

    // W07: интервал обновления из настроек (секунды), по умолчанию 2 (ранее 2000 ms).
    m_refreshTimer.setInterval(QSettings().value("ui/refreshInterval", kRefreshIntervalMs / 1000).toInt() * 1000);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefresh);

    {
        QSettings s;
        const bool autoRefresh = s.value("ui/autoRefresh", true).toBool();
        if (m_actAutoRefresh->isChecked() != autoRefresh)
            m_actAutoRefresh->setChecked(autoRefresh);
    }

    setConnectedUi(false);
    statusBar()->showMessage(tr("Disconnected"), 5000);
    maybeAutoConnect();
}

void MainWindow::createUi()
{
    m_view = new QTreeView(this);
    m_view->setObjectName(QStringLiteral("torrentView"));
    m_view->setModel(&m_proxy);
    m_view->setSortingEnabled(true);
    m_view->setAlternatingRowColors(true);
    m_view->setRootIsDecorated(false);
    m_view->setIndentation(0);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setUniformRowHeights(true);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setMouseTracking(true);
    m_view->setWordWrap(false);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &MainWindow::onContextMenu);
    connect(m_view, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) onTorrentsDoubleClicked();
    });
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->header()->setHighlightSections(false);
    m_view->header()->setMinimumSectionSize(44);
    // W10: раскладка колонок (порядок/видимость/ширины) — из QSettings (иначе дефолт Name..ETA).
    ColumnLayout::fromSettings(TorrentModel::ColumnCount).applyToView(m_view);
    // W10: сохраняем ширину при ресайзе, чтобы она восстанавливалась при старте.
    connect(m_view->header(), &QHeaderView::sectionResized, this,
            [](int logical, int, int newWidth) {
                QSettings().setValue(QStringLiteral("columns/width_%1").arg(logical), newWidth);
            });
    // Единый делегат: фон строки, статус-иконка, статус-точка, прогресс, скорости.
    m_view->setItemDelegate(new TorrentDelegate(this));

    // Левая панель (референс — lvFilter + edSearch): группы статусов,
    // группы папок/трекеров, поиск.
    m_filterPanel = new FilterPanel(this);
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &MainWindow::onFilterChanged);
    const bool folderGrouping = QSettings().value("ui/folderGrouping", true).toBool();
    const bool labelGrouping = QSettings().value("ui/labelGrouping", true).toBool();
    const bool trackerGrouping = QSettings().value("ui/trackerGrouping", true).toBool();
    m_filterPanel->setFolderGroupingEnabled(folderGrouping);
    m_filterPanel->setLabelGroupingEnabled(labelGrouping);
    m_filterPanel->setTrackerGroupingEnabled(trackerGrouping);
    m_filterPanel->setEnabledUi(false);
    m_filterPanel->setMinimumWidth(200);

    // Грид торрентов (всегда видим) — раскладка по референсу (см. WORKFLOW.md).
    auto* topPanel = new QWidget(this);
    auto* topLayout = new QHBoxLayout(topPanel);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);
    topLayout->addWidget(m_filterPanel);
    topLayout->addWidget(m_view, 1);

    // Вкладки деталей выбранного торрента: Файлы / Пиры / Трекеры.
    auto makeTable = [this](QAbstractTableModel* model) {
        auto* view = new QTableView(this);
        view->setModel(model);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setAlternatingRowColors(true);
        view->setSortingEnabled(false);
        view->setShowGrid(false);
        view->setWordWrap(false);
        view->setFrameShape(QFrame::NoFrame);
        view->verticalHeader()->setVisible(false);
        view->verticalHeader()->setDefaultSectionSize(20);
        view->verticalHeader()->setMinimumSectionSize(20);
        view->horizontalHeader()->setStretchLastSection(false);
        view->horizontalHeader()->setSectionsMovable(true);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->horizontalHeader()->setHighlightSections(false);
        return view;
    };

    m_fileTree = new FileTreeModel(this);
    m_fileTree->setFiles({});
    auto* filesView = new QTreeView(this);
    filesView->setObjectName(QStringLiteral("filesView"));
    filesView->setModel(m_fileTree);
    filesView->setUniformRowHeights(true);
    filesView->setAlternatingRowColors(true);
    filesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    filesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    filesView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    filesView->setFrameShape(QFrame::NoFrame);
    filesView->setIndentation(18);
    filesView->header()->setStretchLastSection(false);
    filesView->header()->setSectionsMovable(true);
    filesView->setItemDelegateForColumn(FileTreeModel::Progress, new ProgressDelegate(filesView));
    m_filesView = filesView;
    filesView->setColumnWidth(FileTreeModel::Name, 350);
    filesView->setColumnWidth(FileTreeModel::Size, 90);
    filesView->setColumnWidth(FileTreeModel::Done, 90);
    filesView->setColumnWidth(FileTreeModel::Progress, 70);
    filesView->setColumnWidth(FileTreeModel::Priority, 100);
    connect(filesView, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (m_fileTree->hasChildren(index.siblingAtColumn(0))) {
            const auto* torrent = m_general->torrent();
            if (torrent && torrent->downloadDir)
                openLocalPath(*torrent->downloadDir + QStringLiteral("/") +
                              index.data(FileTreeModel::PathRole).toString(), false);
            return;
        }
        const auto rows = m_fileTree->sourceRows(index);
        if (!rows.isEmpty()) onFilesDoubleClicked(rows.first());
    });
    connect(&m_fileModel, &QAbstractItemModel::modelReset, this, [this] {
        const bool first = m_fileTree->rowCount() == 0;
        const QByteArray header = m_filesView->header()->saveState();
        m_fileTree->setFiles(m_fileModel.files());
        m_filesView->header()->restoreState(header);
        if (first) {
            m_filesView->setColumnWidth(FileTreeModel::Name, 350);
            m_filesView->setColumnWidth(FileTreeModel::Size, 90);
            m_filesView->setColumnWidth(FileTreeModel::Done, 90);
            m_filesView->setColumnWidth(FileTreeModel::Progress, 70);
            m_filesView->setColumnWidth(FileTreeModel::Priority, 100);
            m_filesView->expandToDepth(0);
        }
    });
    connect(&m_fileModel, &FileListModel::wantChanged, this, &MainWindow::onFileWantToggled);
    // Контекстное меню вкладки «Файлы» (референс — pmFiles): приоритет high/normal/low
    // и want/skip для выделенных строк.
    filesView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(filesView, &QWidget::customContextMenuRequested, this, &MainWindow::onFilesContextMenu);

    auto* peersView = makeTable(&m_peerModel);
    peersView->setObjectName(QStringLiteral("peersView"));
    peersView->setColumnWidth(PeerListModel::ColAddress, 150);
    peersView->setColumnWidth(PeerListModel::ColClient, 150);
    peersView->setColumnWidth(PeerListModel::ColFlags, 70);
    peersView->setColumnWidth(PeerListModel::ColProgress, 70);
    peersView->setColumnWidth(PeerListModel::ColRateDown, 85);
    peersView->setColumnWidth(PeerListModel::ColRateUp, 85);
    peersView->hideColumn(PeerListModel::ColPort);
    peersView->horizontalHeader()->moveSection(
        peersView->horizontalHeader()->visualIndex(PeerListModel::ColFlags), 2);
    peersView->horizontalHeader()->moveSection(
        peersView->horizontalHeader()->visualIndex(PeerListModel::ColRateUp), 4);

    auto* trackersView = makeTable(&m_trackerModel);
    trackersView->setObjectName(QStringLiteral("trackersView"));
    trackersView->hideColumn(TrackerListModel::ColTier);
    trackersView->hideColumn(TrackerListModel::ColLeechers);
    trackersView->setColumnWidth(TrackerListModel::ColAnnounce, 250);
    trackersView->setColumnWidth(TrackerListModel::ColLastResult, 240);
    trackersView->setColumnWidth(TrackerListModel::ColUpdate, 90);
    trackersView->setColumnWidth(TrackerListModel::ColSeeders, 70);
    trackersView->horizontalHeader()->moveSection(
        trackersView->horizontalHeader()->visualIndex(TrackerListModel::ColLastResult), 1);
    trackersView->horizontalHeader()->moveSection(
        trackersView->horizontalHeader()->visualIndex(TrackerListModel::ColUpdate), 2);
    // Контекстное меню вкладки «Трекеры» (референс — pmTrackers): Add/Edit/Remove.
    trackersView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(trackersView, &QWidget::customContextMenuRequested,
            this, &MainWindow::onTrackersContextMenu);

    // Нижний уровень: вкладки выбранного торрента.
    m_general = new PropsForm(this);
    m_statsForm = new StatsForm(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("infoPanel"));
    m_tabs->setIconSize(QSize(16, 16));
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 3px 8px; margin-right: 0; margin-top: 2px; }"
        "QTabBar::tab:selected { padding-top: 5px; margin-top: 0; }"
        "QTreeView::item, QTableView::item { padding: 1px 3px; min-height: 18px; }"
        "QHeaderView::section { padding: 2px 5px; font-weight: normal; }"));
    // Названия вкладок 1:1 с референсом (tabControl: General/Trackers/Peers/
    // Files/Statistics).
    m_tabs->addTab(m_general, tgui::theme::icon(tgui::theme::Icon::TabGeneral),
                   tr("General"));
    m_tabs->addTab(trackersView, tgui::theme::icon(tgui::theme::Icon::TabTrackers),
                   tr("Trackers"));
    m_tabs->addTab(peersView, tgui::theme::icon(tgui::theme::Icon::TabPeers),
                   tr("Peers"));
    m_tabs->addTab(filesView, tgui::theme::icon(tgui::theme::Icon::TabFiles),
                   tr("Files"));
    m_tabs->addTab(m_statsForm, tgui::theme::icon(tgui::theme::Icon::TabStatistics),
                   tr("Statistics"));
    m_tabs->setMinimumHeight(80);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(4);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(topPanel);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({454, 326});
    setCentralWidget(splitter);

    if (m_view->selectionModel()) {
        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onSelectionChanged);
        // setCurrentIndex() (в т.ч. программный и навигация клавишами) меняет
        // current без selectionChanged — обновляем детали и по этому сигналу.
        connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex&) { onSelectionChanged(); });
    }

    m_statusDot = new QLabel(this);
    m_statusDot->setContentsMargins(8, 0, 2, 0);
    m_statusDot->setPixmap(tgui::theme::statusDotPixmap(tgui::theme::color::textFaint, 9));
    m_statusText = new QLabel(tr("Disconnected"), this);
    m_statusText->setObjectName(QStringLiteral("statusText"));

    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("statusSegment"));
    m_daemonLabel = new QLabel(this);
    m_daemonLabel->setObjectName(QStringLiteral("statusSegment"));
    m_speedLabel = new QLabel(this);
    m_speedLabel->setObjectName(QStringLiteral("statusSegment"));
    m_speedLabel->setTextFormat(Qt::RichText);

    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(m_statusDot);
    statusBar()->addWidget(m_statusText);
    statusBar()->addPermanentWidget(m_countLabel);
    statusBar()->addPermanentWidget(m_daemonLabel);
    statusBar()->addPermanentWidget(m_speedLabel);
    setConnectionState(tr("Disconnected"), tgui::theme::color::textFaint);
}

void MainWindow::setConnectionState(const QString& text, const QColor& color)
{
    if (m_statusText)
        m_statusText->setText(text);
    if (m_statusDot)
        m_statusDot->setPixmap(tgui::theme::statusDotPixmap(color, 9));
}

void MainWindow::createActions()
{
    using tgui::theme::Icon;

    m_actConnect = new QAction(tr("Connect"), this);
    m_actConnect->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    m_actConnect->setIcon(tgui::theme::icon(Icon::Connect));
    m_actConnect->setToolTip(tr("Connect to daemon (Ctrl+K)"));
    connect(m_actConnect, &QAction::triggered, this, &MainWindow::onConnect);

    m_actDisconnect = new QAction(tr("Disconnect from Transmission"), this);
    m_actDisconnect->setIcon(tgui::theme::icon(Icon::Disconnect));
    m_actDisconnect->setToolTip(tr("Disconnect from Transmission"));
    connect(m_actDisconnect, &QAction::triggered, this, &MainWindow::onDisconnect);

    // Сочетания — как в transgui/main.lfm (Insert, Ctrl+Insert, F3/F4, F7, F9,
    // Ctrl+F9, F11/F12, Shift+F11/Shift+F12).
    m_actAdd = new QAction(tr("Add torrent…"), this);
    m_actAdd->setShortcut(QKeySequence(Qt::Key_Insert));
    m_actAdd->setIcon(tgui::theme::icon(Icon::Add));
    m_actAdd->setToolTip(tr("Add torrent (Insert)"));
    m_actAdd->setEnabled(false);
    connect(m_actAdd, &QAction::triggered, this, &MainWindow::onAddTorrent);

    m_actAddLink = new QAction(tr("Add torrent link…"), this);
    m_actAddLink->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Insert));
    m_actAddLink->setIcon(tgui::theme::icon(Icon::AddLink));
    m_actAddLink->setToolTip(tr("Add torrent link (Ctrl+Insert)"));
    m_actAddLink->setEnabled(false);
    connect(m_actAddLink, &QAction::triggered, this, &MainWindow::onAddFromLink);

    m_actStart = new QAction(tr("Start"), this);
    m_actStart->setShortcut(QKeySequence(Qt::Key_F3));
    m_actStart->setIcon(tgui::theme::icon(Icon::Start));
    m_actStart->setToolTip(tr("Start selected torrents (F3)"));
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStartSelected);

    m_actStop = new QAction(tr("Pause"), this);
    m_actStop->setShortcut(QKeySequence(Qt::Key_F4));
    m_actStop->setIcon(tgui::theme::icon(Icon::Pause));
    m_actStop->setToolTip(tr("Pause selected torrents (F4)"));
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStopSelected);

    m_actStartAll = new QAction(tr("Start all torrents"), this);
    m_actStartAll->setIcon(tgui::theme::icon(Icon::StartAll));
    m_actStartAll->setToolTip(tr("Start all torrents"));
    connect(m_actStartAll, &QAction::triggered, this, &MainWindow::onStartAll);

    m_actStopAll = new QAction(tr("Pause all torrents"), this);
    m_actStopAll->setIcon(tgui::theme::icon(Icon::StopAll));
    m_actStopAll->setToolTip(tr("Pause all torrents"));
    connect(m_actStopAll, &QAction::triggered, this, &MainWindow::onStopAll);

    m_actRemove = new QAction(tr("Remove"), this);
    m_actRemove->setShortcut(QKeySequence::Delete);
    m_actRemove->setIcon(tgui::theme::icon(Icon::Remove));
    m_actRemove->setToolTip(tr("Remove selected torrents (Del)"));
    connect(m_actRemove, &QAction::triggered, this, &MainWindow::onRemoveSelected);

    m_actProperties = new QAction(tr("Properties…"), this);
    m_actProperties->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    m_actProperties->setIcon(tgui::theme::icon(Icon::Properties));
    m_actProperties->setToolTip(tr("Torrent properties (Alt+Enter)"));
    m_actProperties->setEnabled(false);
    connect(m_actProperties, &QAction::triggered, this, &MainWindow::onShowProperties);

    m_actMove = new QAction(tr("Set data location…"), this);
    m_actMove->setShortcut(QKeySequence(Qt::Key_F6));
    m_actMove->setIcon(tgui::theme::icon(Icon::Move));
    m_actMove->setToolTip(tr("Move torrent data (F6)"));
    m_actMove->setEnabled(false);
    connect(m_actMove, &QAction::triggered, this, &MainWindow::onMoveSelected);

    m_actRefresh = new QAction(tr("Refresh"), this);
    m_actRefresh->setShortcut(QKeySequence::Refresh);
    m_actRefresh->setIcon(tgui::theme::icon(Icon::Refresh));
    m_actRefresh->setToolTip(tr("Refresh torrent list (F5)"));
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);

    m_actAutoRefresh = new QAction(tr("Auto refresh"), this);
    m_actAutoRefresh->setCheckable(true);
    m_actAutoRefresh->setChecked(true);
    m_actAutoRefresh->setIcon(tgui::theme::icon(Icon::AutoRefresh));
    m_actAutoRefresh->setToolTip(tr("Automatically refresh the torrent list"));
    connect(m_actAutoRefresh, &QAction::toggled, this, [this](bool on) {
        if (on && m_connected)
            m_refreshTimer.start();
        else
            m_refreshTimer.stop();
        QSettings().setValue("ui/autoRefresh", on);
    });

    m_actAltSpeed = new QAction(tr("Use alternate bandwidth settings"), this);
    m_actAltSpeed->setShortcut(QKeySequence(Qt::Key_F7));
    m_actAltSpeed->setCheckable(true);
    m_actAltSpeed->setChecked(QSettings().value("ui/altSpeed", false).toBool());
    m_actAltSpeed->setIcon(tgui::theme::icon(Icon::AltSpeed));
    m_actAltSpeed->setToolTip(tr("Toggle the daemon's alternative speed limit (F7)"));
    connect(m_actAltSpeed, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("ui/altSpeed", on);
        if (m_rpc && m_connected)
            m_rpc->setAltSpeed(on);
    });

    m_actDaemonOptions = new QAction(tr("Transmission options…"), this);
    m_actDaemonOptions->setShortcut(QKeySequence(Qt::Key_F9));
    m_actDaemonOptions->setIcon(tgui::theme::icon(Icon::DaemonOptions));
    m_actDaemonOptions->setToolTip(tr("Transmission options (F9)"));
    connect(m_actDaemonOptions, &QAction::triggered, this, &MainWindow::onDaemonOptions);

    m_actOptions = new QAction(tr("Application options…"), this);
    m_actOptions->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F9));
    m_actOptions->setIcon(tgui::theme::icon(Icon::AppOptions));
    m_actOptions->setToolTip(tr("Application options (Ctrl+F9)"));
    connect(m_actOptions, &QAction::triggered, this, &MainWindow::onOptions);

    m_actAbout = new QAction(tr("About"), this);
    m_actAbout->setIcon(tgui::theme::icon(Icon::About));
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);

    // --- Toolbar: порядок 1:1 с transgui/main.lfm -----------------------
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setAllowedAreas(Qt::TopToolBarArea);
    m_toolBar->setIconSize(QSize(22, 22));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_toolBar->setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 2px; padding: 4px 8px; }"
        "QToolBar::separator { width: 1px; margin: 6px; background: #C7C7C7; }"
        "QToolBar > QToolButton { min-width: 30px; min-height: 30px; border-radius: 4px; padding: 4px 8px; }"
        "QToolButton#connectButton { background: white; border: 1px solid #ABADB3; min-height: 32px; padding: 2px 20px 2px 10px; }"
        "QToolButton#connectButton[connected=\"false\"] { background: #FFF6F5; border-color: #E5A4A0; }"
        "QToolButton#queueButton { padding: 4px 18px 4px 8px; }"
        "QToolButton#connectButton::menu-indicator, QToolButton#queueButton::menu-indicator { width: 8px; height: 8px; subcontrol-origin: padding; subcontrol-position: center right; right: 5px; }"));
    m_actAdd->setIconText(tr("Add"));
    m_actAddLink->setIconText(tr("Magnet"));
    m_actStart->setIconText(tr("Start"));
    m_actStop->setIconText(tr("Pause"));
    m_actRemove->setIconText(tr("Remove"));
    m_actMove->setIconText(tr("Move"));
    m_actAltSpeed->setIconText(tr("Limit"));

    // Connect — как OLD tbConnect (Style = tbsDropDown, DropdownMenu =
    // pmConnections): вся кнопка — дропдаун списка подключений, ВСЕГДА активна
    // (и при подключении, чтобы переключать профили). Подпись — как в OLD:
    // «Connect to Transmission» в отключённом состоянии / имя активного
    // профиля при подключении (см. setConnectedUi).
    m_connectButton = new QToolButton(m_toolBar);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    m_connectButton->setIcon(tgui::theme::icon(Icon::Connect));
    m_connectButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_connectButton->setText(tr("Connect to Transmission"));
    m_connectButton->setPopupMode(QToolButton::InstantPopup);
    m_connectButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    m_connectButton->setToolTip(tr("Connect to daemon (Ctrl+K)"));
    auto* profilesMenu = new QMenu(m_connectButton);
    connect(profilesMenu, &QMenu::aboutToShow, this, [this, profilesMenu] {
        profilesMenu->clear();
        auto* aConnect =
            profilesMenu->addAction(tr("Connect to Transmission"));
        aConnect->setToolTip(tr("Connect to the current connection (Ctrl+K)"));
        connect(aConnect, &QAction::triggered, this, &MainWindow::onConnect);
        ProfileStore store;
        const QStringList names = store.names();
        if (!names.isEmpty())
            profilesMenu->addSeparator();
        for (const QString& name : names) {
            auto* a = profilesMenu->addAction(name);
            a->setCheckable(true);
            a->setChecked(name == store.currentName());
            connect(a, &QAction::triggered, this, [this, name] { switchToProfile(name); });
        }
        profilesMenu->addSeparator();
        profilesMenu->addAction(m_actDisconnect);
        profilesMenu->addSeparator();
        auto* manage = profilesMenu->addAction(tr("Manage connections…"));
        connect(manage, &QAction::triggered, this, &MainWindow::onConnectionOptions);
    });
    m_connectButton->setMenu(profilesMenu);
    m_toolBar->addWidget(m_connectButton);
    m_toolBar->addSeparator();

    m_toolBar->addAction(m_actAdd);
    m_toolBar->addAction(m_actAddLink);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_actStart);
    m_toolBar->addAction(m_actStop);
    m_toolBar->addAction(m_actRemove);
    m_toolBar->addSeparator();

    m_actQueue = new QAction(tgui::theme::icon(Icon::Queue), tr("Queue"), this);
    m_actQueue->setToolTip(tr("Change queue position"));
    m_actQueue->setObjectName(QStringLiteral("queueAction"));
    auto* queueMenu = new QMenu(m_toolBar);
    auto* first = queueMenu->addAction(tr("Move top"));
    auto* up = queueMenu->addAction(tr("Move up"));
    auto* down = queueMenu->addAction(tr("Move down"));
    auto* last = queueMenu->addAction(tr("Move bottom"));
    connect(first, &QAction::triggered, this, &MainWindow::onQueueTop);
    connect(up, &QAction::triggered, this, &MainWindow::onQueueUp);
    connect(down, &QAction::triggered, this, &MainWindow::onQueueDown);
    connect(last, &QAction::triggered, this, &MainWindow::onQueueBottom);
    m_actQueue->setMenu(queueMenu);
    m_toolBar->addAction(m_actQueue);
    auto* queueButton = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(m_actQueue));
    queueButton->setObjectName(QStringLiteral("queueButton"));
    queueButton->setPopupMode(QToolButton::InstantPopup);
    m_toolBar->addAction(m_actMove);
    m_toolBar->addAction(m_actAltSpeed);

    // Поиск — в тулбаре, как edSearch в transgui.
    auto* spacer = new QWidget(m_toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);
    m_searchEdit = new QLineEdit(m_toolBar);
    m_searchEdit->setObjectName(QStringLiteral("searchEdit"));
    m_searchEdit->setPlaceholderText(tr("Search torrents"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->addAction(tgui::theme::icon(Icon::Search), QLineEdit::LeadingPosition);
    m_searchEdit->setMinimumWidth(140);
    m_searchEdit->setFixedHeight(28);
    m_searchEdit->setMaximumWidth(360);
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this](const QString& text) { m_proxy.setSearchText(text.trimmed()); });
    m_toolBar->addWidget(m_searchEdit);
    auto* find = new QAction(this);
    find->setShortcut(QKeySequence::Find);
    addAction(find);
    connect(find, &QAction::triggered, m_searchEdit, [this] { m_searchEdit->setFocus(); m_searchEdit->selectAll(); });
    auto applyStyle = [this](int value) {
        if (value < 0 || value > 2) value = 2;
        const Qt::ToolButtonStyle modes[]{Qt::ToolButtonIconOnly, Qt::ToolButtonTextBesideIcon, Qt::ToolButtonTextUnderIcon};
        m_toolBar->setToolButtonStyle(modes[value]);
        m_toolBar->setIconSize(value == 2 ? QSize(22, 22) : QSize(18, 18));
        m_connectButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    };
    applyStyle(QSettings().value("ui/toolbarButtonStyle", 2).toInt());
    m_toolBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_toolBar, &QWidget::customContextMenuRequested, this, [this, applyStyle](const QPoint& point) {
        QMenu menu(this);
        QActionGroup group(&menu);
        const QStringList names{tr("Icons only"), tr("Text beside icons"), tr("Text under icons")};
        for (int value = 0; value < names.size(); ++value) {
            auto* action = menu.addAction(names.at(value));
            action->setCheckable(true); group.addAction(action);
            const Qt::ToolButtonStyle modes[]{Qt::ToolButtonIconOnly, Qt::ToolButtonTextBesideIcon, Qt::ToolButtonTextUnderIcon};
            action->setChecked(m_toolBar->toolButtonStyle() == modes[value]);
            connect(action, &QAction::triggered, &menu, [applyStyle, value] {
                applyStyle(value); QSettings().setValue("ui/toolbarButtonStyle", value);
            });
        }
        menu.exec(m_toolBar->mapToGlobal(point));
    });

}

void MainWindow::setConnectedUi(bool on)
{
    m_filterPanel->setEnabledUi(on);
    m_actConnect->setEnabled(!on);
    // Подпись кнопки — как в OLD (tbConnect.Caption): «Connect to
    // Transmission» при отключении, имя активного профиля при подключении.
    // Сама кнопка-дропдаун остаётся активной всегда (переключение профилей).
    if (m_connectButton) {
        QString caption = tr("Not connected");
        if (on) {
            caption = m_rpc ? m_rpc->options().url.host() : tr("Connected");
            const QString cur = ProfileStore().currentName();
            if (!cur.isEmpty())
                caption = cur;
        }
        m_connectButton->setText(caption);
        m_connectButton->setIcon(QIcon(tgui::theme::statusDotPixmap(
            on ? tgui::theme::color::green : tgui::theme::color::red, 10)));
        m_connectButton->setIconSize(QSize(10, 10));
        m_connectButton->setProperty("connected", on);
        m_connectButton->style()->unpolish(m_connectButton);
        m_connectButton->style()->polish(m_connectButton);
    }
    m_actDisconnect->setEnabled(on);
    m_actAdd->setEnabled(on);
    m_actAddLink->setEnabled(on);
    m_actRefresh->setEnabled(on);
    m_actAutoRefresh->setEnabled(on);
    m_actAltSpeed->setEnabled(on);
    m_actStart->setEnabled(on);
    m_actStop->setEnabled(on);
    m_actStartAll->setEnabled(on);
    m_actStopAll->setEnabled(on);
    m_actRemove->setEnabled(on);
    m_actProperties->setEnabled(on);
    m_actMove->setEnabled(on);
    m_actDaemonOptions->setEnabled(on);
    updateSelectionActions();
}

// Действия над торрентами активны только при непустом выделении (как в
// transgui: acStartTorrent/acStopTorrent/… Enabled по SelectedCount).
void MainWindow::updateSelectionActions()
{
    const bool hasSel = m_connected && !selectedTorrentIds().isEmpty();
    m_actStart->setEnabled(hasSel);
    m_actStop->setEnabled(hasSel);
    m_actRemove->setEnabled(hasSel);
    m_actProperties->setEnabled(hasSel);
    m_actMove->setEnabled(m_connected && selectedTorrentIds().size() == 1);
    if (m_actQueue) m_actQueue->setEnabled(hasSel);
}

void MainWindow::onConnect()
{
    // Единая модель (по OLD): «Connect» подключает к активному серверу (профилю).
    // Серверов нет — открыть управление, чтобы добавить (сначала задают в
    // «Manage connections…», затем переключаются из подменю «Connect to Transmission»).
    ProfileStore store;
    if (store.hasCurrent()) {
        switchToProfile(store.currentName());
        return;
    }
    onConnectionOptions();
}

void MainWindow::startConnect(const tgui::RpcClient::Options& opts)
{
    m_rpc = std::make_unique<tgui::RpcClient>(opts, this);
    connect(m_rpc.get(), &tgui::RpcClient::connected, this, &MainWindow::onConnected);
    connect(m_rpc.get(), &tgui::RpcClient::sessionInfoReceived, this,
            [this](const tgui::SessionInfo& info) {
        m_sessionInfo = info;
        const QSignalBlocker blocker(m_actAltSpeed);
        m_actAltSpeed->setChecked(info.altSpeedEnabled);
        if (m_general->torrent()) m_general->setSession(info);
    });
    connect(m_rpc.get(), &tgui::RpcClient::connectionFailed, this,
            &MainWindow::onConnectionFailed);
    connect(m_rpc.get(), &tgui::RpcClient::authenticationFailed, this,
            &MainWindow::onAuthenticationFailed);
    connect(m_rpc.get(), &tgui::RpcClient::torrentsReceived, this,
            &MainWindow::onTorrentsReceived);
    connect(m_rpc.get(), &tgui::RpcClient::torrentDetailsReceived, this,
            &MainWindow::onTorrentDetailsReceived);
    connect(m_rpc.get(), &tgui::RpcClient::peersReceived, this,
            &MainWindow::onPeersReceived);
    connect(m_rpc.get(), &tgui::RpcClient::trackersReceived, this,
            &MainWindow::onTrackersReceived);
    connect(m_rpc.get(), &tgui::RpcClient::sessionStatsReceived, this,
            [this](const tgui::SessionStats& stats) { m_statsForm->setStats(stats); });
    connect(m_rpc.get(), &tgui::RpcClient::readError, this, &MainWindow::onReadError);
    connect(m_rpc.get(), &tgui::RpcClient::requestCompleted, this,
            &MainWindow::onRequestCompleted);

    statusBar()->showMessage(tr("Connecting to %1…").arg(opts.url.toString()), 5000);
    setConnectionState(tr("Connecting…"), tgui::theme::color::accent);
    m_rpc->connectToDaemon();
}

void MainWindow::setAutoConnect(const tgui::RpcClient::Options& opts)
{
    if (opts.url.isEmpty())
        return;
    m_autoConnectOpts = opts;
    // Вызывается после конструктора — подключаемся сразу.
    if (!m_rpc && !m_connected) {
        saveSettings(opts);
        startConnect(opts);
    }
}

void MainWindow::maybeAutoConnect()
{
    // W08: приоритет — параметры из командной строки; иначе — текущий профиль (ConnOptions).
    tgui::RpcClient::Options opts;
    if (!m_autoConnectOpts.url.isEmpty()) {
        opts = m_autoConnectOpts;
    } else {
        ProfileStore store;
        if (store.hasCurrent())
            opts = store.currentProfile().toOptions();
    }
    if (opts.url.isEmpty())
        return;
    saveSettings(opts);
    startConnect(opts);
}

void MainWindow::onDisconnect()
{
    m_refreshTimer.stop();
    m_connected = false;
    m_rpc.reset();
    m_model.setTorrents({});
    m_fileModel.setFiles({});
    m_peerModel.setPeers({});
    m_trackerModel.setTrackers({});
    m_general->clear();
    m_statsForm->clear();
    m_currentId = -1;
    setConnectedUi(false);
    setConnectionState(tr("Disconnected"), tgui::theme::color::textFaint);
    m_daemonLabel->clear();
    m_speedLabel->clear();
    m_countLabel->clear();
    statusBar()->showMessage(tr("Disconnected"), 5000);
}

void MainWindow::onConnected(const tgui::SessionInfo& info)
{
    m_connected = true;
    m_sessionInfo = info;
    {
        const QSignalBlocker blocker(m_actAltSpeed);
        m_actAltSpeed->setChecked(info.altSpeedEnabled);
    }
    if (m_general->torrent())
        m_general->setSession(info);
    m_daemonLabel->setText(
        tr("Transmission %1 · RPC %2").arg(info.version).arg(info.rpcVersion));
    if (m_rpc)
        m_daemonLabel->setToolTip(m_rpc->options().url.toString());
    setConnectionState(tr("Connected"), tgui::theme::color::green);
    setConnectedUi(true);
    if (m_actAutoRefresh->isChecked())
        m_refreshTimer.start();
    m_rpc->getTorrents();
    m_rpc->getSessionStats();
    statusBar()->showMessage(tr("Connected"), 3000);
}

void MainWindow::onConnectionFailed(const QString& error)
{
    m_refreshTimer.stop();
    m_connected = false;
    setConnectedUi(false);
    setConnectionState(tr("Connection failed"), tgui::theme::color::red);
    m_daemonLabel->clear();
    m_speedLabel->clear();
    m_countLabel->clear();
    const QString url = m_rpc ? m_rpc->options().url.toString() : QString();
    // Отложенная очистка: destroy-им клиента после завершения цепочки сигналов.
    QTimer::singleShot(0, this, [this, error, url] {
        m_rpc.reset();
        m_model.setTorrents({});
        m_general->clear();
        m_statsForm->clear();
        QMessageBox::critical(this, tr("Connection failed"),
                              tr("Address: %1\n\nReason: %2").arg(url, error));
        statusBar()->showMessage(tr("Disconnected"), 5000);
    });
}

void MainWindow::onAuthenticationFailed(const QString& error)
{
    Q_UNUSED(error);
    m_refreshTimer.stop();
    m_connected = false;
    setConnectedUi(false);
    setConnectionState(tr("Authentication required"), tgui::theme::color::red);
    // Откладываем: даём развернуться стеку сигнала, чтобы безопасно показать
    // диалог повторной авторизации и переподключиться (старый клиент уничтожится).
    QTimer::singleShot(0, this, [this] {
        if (!m_rpc)
            return;
        const auto opts = m_rpc->options();
        ConnectDialog dlg(opts, this);
        if (dlg.exec() != QDialog::Accepted) {
            onConnectionFailed(tr("Authentication failed (HTTP 401). Rejected by user."));
            return;
        }
        const auto newOpts = dlg.options();
        if (newOpts.url.isEmpty()) {
            onConnectionFailed(tr("Authentication failed (HTTP 401)."));
            return;
        }
        saveSettings(newOpts);
        startConnect(newOpts); // повторное подключение с новыми учётными данными
    });
}

void MainWindow::onRefresh()
{
    if (m_rpc && m_connected) {
        m_rpc->getTorrents();
        m_rpc->getSessionStats();
    }
}

void MainWindow::onTorrentsReceived(const QVector<tgui::Torrent>& torrents)
{
    QStringList completed;
    QHash<int, const tgui::Torrent*> previous;
    for (const auto& old : m_model.torrents()) previous.insert(old.id, &old);
    for (const auto& torrent : torrents) {
        const auto* old = previous.value(torrent.id, nullptr);
        if (old && old->status.value_or(-1) == 4 && old->leftUntilDone.value_or(0) > 0 &&
            torrent.leftUntilDone && *torrent.leftUntilDone == 0 &&
            torrent.sizeWhenDone.value_or(0) > 0 && torrent.status.value_or(-1) != 2)
            completed.append(torrent.name.value_or(QString::number(torrent.id)));
    }
    if (!completed.isEmpty() && QSettings().value("ui/trayNotify", false).toBool())
        emit downloadNotification(completed.size() == 1 ?
            tr("Download completed: %1").arg(completed.first()) :
            tr("Downloads completed: %1").arg(completed.size()));
    // setTorrents() (modelReset) сбрасывает выделение и текущий индекс вью,
    // а вью прокручивается к началу. Запоминаем ВСЁ выделение (Select all
    // должен переживать автообновление раз в 2 секунды), текущую строку и
    // позицию прокрутки ДО сброса.
    const QVector<int> prevSelected = selectedTorrentIds();
    const int prevId = m_currentId;
    const int prevScroll = m_view->verticalScrollBar()->value();
    const bool firstLoad = m_model.rowCount() == 0;

    // modelReset синхронно шлёт selectionChanged/currentChanged — не даём
    // onSelectionChanged сбросить текущий торрент посреди обновления.
    m_refreshing = true;
    m_model.setTorrents(torrents);
    updateFilterPanel(torrents);

    // Восстанавливаем все ранее выделенные ID (только видимые после фильтра).
    restoreTorrentSelection(prevSelected, prevId);

    int nextId = prevId;
    if (nextId >= 0 && proxyRowForId(nextId) < 0)
        nextId = -1;
    if (nextId < 0) {
        for (int id : prevSelected) {
            if (proxyRowForId(id) >= 0) {
                nextId = id;
                break;
            }
        }
    }
    // Первая загрузка: выбираем первую строку, как в эталоне, — иначе вкладка
    // General остаётся пустой до ручного клика.
    if (nextId < 0 && firstLoad && m_proxy.rowCount() > 0) {
        const QModelIndex first = m_proxy.index(0, TorrentModel::ColName);
        m_view->selectionModel()->select(
            QItemSelection(first, first),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_view->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
        nextId = m_proxy.torrentAt(0).id;
    }
    m_currentId = nextId;
    m_refreshing = false;

    // Панель «General» подтягивается к актуальному состоянию выбранного.
    if (nextId >= 0) {
        for (const auto& t : torrents) {
            if (t.id == nextId) {
                m_general->setTorrent(t);
                if (m_sessionInfo.rpcVersion > 0)
                    m_general->setSession(m_sessionInfo);
                break;
            }
        }
    } else {
        m_general->clear();
    }
    // Если текущая строка сменилась из-за удаления торрента — обновить вкладки
    // деталей (при обычном refresh тот же ID и запросы не нужны).
    if (nextId != prevId && nextId >= 0 && m_rpc) {
        m_rpc->getTorrentDetails(nextId);
        m_rpc->getPeers(nextId);
        m_rpc->getTrackers(nextId);
    }

    updateStatusBarTotals();
    const int count = static_cast<int>(torrents.size());
    m_countLabel->setText(count == 1 ? tr("1 torrent") : tr("%1 torrents").arg(count));

    m_view->doItemsLayout();
    m_view->verticalScrollBar()->setValue(prevScroll);
    updateSelectionActions();
}

int MainWindow::proxyRowForId(int id) const
{
    const auto& list = m_model.torrents();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id != id)
            continue;
        const QModelIndex proxyIdx =
            m_proxy.mapFromSource(m_model.index(i, TorrentModel::ColName));
        return proxyIdx.isValid() ? proxyIdx.row() : -1;
    }
    return -1;
}

void MainWindow::restoreTorrentSelection(const QVector<int>& ids, int currentId)
{
    if (!m_view->selectionModel())
        return;

    QVector<int> wanted = ids;
    if (wanted.isEmpty() && currentId >= 0)
        wanted.push_back(currentId);

    QItemSelection selection;
    int currentRow = -1;
    int firstRow = -1;
    for (int id : wanted) {
        const int row = proxyRowForId(id);
        if (row < 0)
            continue;
        const QModelIndex idx = m_proxy.index(row, TorrentModel::ColName);
        selection.select(idx, idx);
        if (firstRow < 0)
            firstRow = row;
        if (id == currentId)
            currentRow = row;
    }
    if (firstRow < 0)
        return;
    if (currentRow < 0)
        currentRow = firstRow;

    m_view->selectionModel()->select(selection,
                                     QItemSelectionModel::ClearAndSelect |
                                         QItemSelectionModel::Rows);
    m_view->selectionModel()->setCurrentIndex(
        m_proxy.index(currentRow, TorrentModel::ColName),
        QItemSelectionModel::NoUpdate);
}

void MainWindow::onFilterChanged(int group, const QString& folder,
                                 const QString& tracker, const QString& label)
{
    m_proxy.setStatusGroup(group);
    m_proxy.setFolderFilter(folder);
    m_proxy.setLabelFilter(label);
    m_proxy.setTrackerFilter(tracker);
}

// Счётчики групп и состав списков папок/трекеров (референс — FillTorrentsList).
void MainWindow::updateFilterPanel(const QVector<tgui::Torrent>& torrents)
{
    int downloading = 0, completed = 0, active = 0, stopped = 0, error = 0, waiting = 0;
    QHash<QString, int> folders;
    QHash<QString, int> labels;
    QHash<QString, int> trackers;
    for (const auto& t : torrents) {
        const auto matchesGroup = &TorrentModel::matchesGroup;
        if (matchesGroup(t, TorrentModel::GroupDownloading))
            downloading++;
        if (matchesGroup(t, TorrentModel::GroupCompleted))
            completed++;
        if (matchesGroup(t, TorrentModel::GroupActive))
            active++;
        if (matchesGroup(t, TorrentModel::GroupStopped))
            stopped++;
        if (matchesGroup(t, TorrentModel::GroupError))
            error++;
        if (matchesGroup(t, TorrentModel::GroupWaiting))
            waiting++;
        if (t.downloadDir.has_value() && !t.downloadDir->isEmpty())
            folders.insert(*t.downloadDir, folders.value(*t.downloadDir) + 1);
        if (t.labels.has_value()) {
            for (const QString& label : *t.labels) {
                if (!label.isEmpty())
                    labels.insert(label, labels.value(label) + 1);
            }
        }
        if (!t.trackerStats.isEmpty() && !t.trackerStats.first().announce.isEmpty()) {
            const QString announce = t.trackerStats.first().announce;
            trackers.insert(announce, trackers.value(announce) + 1);
        }
    }

    auto toPairs = [](const QHash<QString, int>& map) {
        QVector<QPair<QString, int>> list;
        list.reserve(map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            list.append({it.key(), it.value()});
        std::sort(list.begin(), list.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        return list;
    };

    const int inactive = static_cast<int>(torrents.size()) - active - stopped;
    m_filterPanel->setCounts(static_cast<int>(torrents.size()), downloading, completed,
                             active, inactive, stopped, error, waiting,
                             toPairs(folders), toPairs(labels), toPairs(trackers));
}

void MainWindow::onReadError(const QString& what, const QString& error)
{
    statusBar()->showMessage(tr("%1: %2").arg(what, error), 8000);
}

void MainWindow::onRequestCompleted(const QString& method, bool ok, const QString& error,
                                    const QJsonObject& arguments)
{
    if (method == QLatin1String("torrent-add")) {
        if (!ok) {
            QMessageBox::warning(this, tr("Add torrent"),
                                 error.isEmpty() ? tr("Unknown error.") : error);
            m_pendingAddId = -1;
            return;
        }
        int id = -1;
        const QJsonObject added = arguments.value("torrent-added").toObject();
        if (added.contains("id"))
            id = added.value("id").toInt();
        if (id < 0)
            id = arguments.value("id").toInt();
        if (id < 0)
            id = arguments.value("torrent-id").toInt();
        if (id >= 0) {
            m_pendingAddId = id;
            m_pendingStart = m_pendingAdd.start;
            if (m_pendingAdd.source == tgui::RpcClient::AddTorrent::Source::Url)
                finishAdd(); // magnet/URL — списка файлов нет
            else if (m_rpc)
                m_rpc->getTorrentDetails(id); // файлы → диалог want/skip
        }
        return;
    }
    Q_UNUSED(arguments);
    if (ok && method == QLatin1String("torrent-set-location") && m_rpc)
        m_rpc->getTorrents();
    if (method == QLatin1String("session-set") && m_rpc)
        m_rpc->getSession(); // Refresh on success and roll back the toggle on failure.
    if (ok)
        statusBar()->showMessage(tr("\"%1\" done").arg(method), 5000);
    else
        statusBar()->showMessage(tr("«%1»: %2").arg(method, error), 8000);
}

QVector<int> MainWindow::selectedTorrentIds() const
{
    QVector<int> ids;
    if (!m_view->selectionModel())
        return ids;
    // selectedRows() уже отдаёт по одному индексу на строку (без дублей по
    // колонкам). mapToSource() — ровно один раз: раньше индекс маппился
    // повторно, и результат всегда был пустым (все действия над торрентами
    // оказывались недоступны).
    for (const QModelIndex& vi : m_view->selectionModel()->selectedRows()) {
        const QModelIndex src = m_proxy.mapToSource(vi);
        if (!src.isValid())
            continue;
        const auto& t = m_model.torrentAt(src.row());
        if (!ids.contains(t.id))
            ids.push_back(t.id);
    }
    return ids;
}

void MainWindow::onStartSelected()
{
    if (!m_rpc)
        return;
    for (int id : selectedTorrentIds())
        m_rpc->startTorrent(id);
}

void MainWindow::onStopSelected()
{
    if (!m_rpc)
        return;
    for (int id : selectedTorrentIds())
        m_rpc->stopTorrent(id);
}

void MainWindow::onStartAll()
{
    if (m_rpc)
        m_rpc->startAll();
}

void MainWindow::onStopAll()
{
    if (m_rpc)
        m_rpc->stopAll();
}

void MainWindow::onRemoveSelected()
{
    if (!m_rpc)
        return;
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty())
        return;

    auto* box = new QMessageBox(this);
    box->setWindowTitle(tr("Remove torrents"));
    box->setIcon(QMessageBox::Question);
    box->setText(ids.size() == 1
                     ? tr("Remove the torrent from the daemon's list?")
                     : tr("Remove %1 torrents from the daemon's list?").arg(ids.size()));
    box->setInformativeText(tr("You can also remove the downloaded data on disk."));
    box->setStandardButtons(QMessageBox::Cancel);
    QAbstractButton* btnRemove = box->addButton(tr("Remove"), QMessageBox::AcceptRole);
    QAbstractButton* btnRemoveData =
        box->addButton(tr("Remove with data"), QMessageBox::DestructiveRole);
    box->exec();
    const auto clicked = box->clickedButton();
    box->deleteLater();
    if (clicked == btnRemove || clicked == btnRemoveData) {
        const bool deleteData = (clicked == btnRemoveData);
        for (int id : ids)
            m_rpc->removeTorrent(id, deleteData);
    }
}

void MainWindow::updateStatusBarTotals()
{
    double down = 0.0;
    double up = 0.0;
    for (const auto& t : m_model.torrents()) {
        down += t.rateDownload.value_or(0);
        up += t.rateUpload.value_or(0);
    }
    m_speedLabel->setText(
        QStringLiteral("<span style=\"color:%1;\">↓</span>&nbsp;%2&nbsp;&nbsp;"
                       "<span style=\"color:%3;\">↑</span>&nbsp;%4")
            .arg(tgui::theme::color::accent.name(),
                 QString::fromStdString(tgui::formatRate(down)),
                 tgui::theme::color::green.name(),
                 QString::fromStdString(tgui::formatRate(up))));
}

void MainWindow::onShowProperties()
{
    if (!m_rpc)
        return;
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty())
        return;
    const int id = ids.first();
    for (const auto& t : m_model.torrents()) {
        if (t.id == id) {
            PropsDialog dlg(t, this);
            if (dlg.exec() == QDialog::Accepted && !dlg.changes().isEmpty())
                m_rpc->setTorrent(id, dlg.changes());
            return;
        }
    }
}

void MainWindow::onMoveSelected()
{
    if (!m_rpc)
        return;
    const auto ids = selectedTorrentIds();
    if (ids.size() != 1) {
        if (!ids.isEmpty())
            QMessageBox::warning(this, tr("Move"),
                                 tr("Only one selected torrent can be moved."));
        return;
    }
    const tgui::Torrent* found = nullptr;
    for (const auto& t : m_model.torrents())
        if (t.id == ids.first()) {
            found = &t;
            break;
        }
    if (!found)
        return;

    MoveDialog dlg(found->downloadDir.value_or(QString()), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_rpc->setLocation(ids.first(), dlg.targetDir(), dlg.moveData());
}

void MainWindow::onAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::onColumns()
{
    const int count = TorrentModel::ColumnCount;
    QStringList titles;
    if (auto* model = m_view->model())
        for (int i = 0; i < count; ++i)
            titles << model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
    ColumnLayout cur = ColumnLayout::fromSettings(count);
    ColSetupDialog dlg(cur, titles, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const ColumnLayout nl = dlg.layout();
    nl.applyToView(m_view);
    nl.saveToSettings();
    statusBar()->showMessage(tr("Column layout updated."), 3000);
}

void MainWindow::onContextMenu(const QPoint& pos)
{
    // ПКМ по строке без выделения должен сначала выбрать эту строку —
    // иначе контекстное меню выглядит «серым» и не действует на торрент.
    const QModelIndex hit = m_view->indexAt(pos);
    if (hit.isValid() && m_view->selectionModel() &&
        !m_view->selectionModel()->isRowSelected(hit.row(), hit.parent())) {
        m_view->selectionModel()->select(
            QItemSelection(hit, hit),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_view->selectionModel()->setCurrentIndex(hit, QItemSelectionModel::NoUpdate);
    }

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    const bool hasSel = m_connected && !selectedTorrentIds().isEmpty();
    using tgui::theme::Icon;
    auto stdIcon = [this](QStyle::StandardPixmap sp) { return style()->standardIcon(sp); };

    // Открыть (папку, содержащую объект) — как в образце.
    auto* actOpen = menu->addAction(tgui::theme::icon(Icon::Folder), tr("Open folder containing object"));
    actOpen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    actOpen->setEnabled(hasSel);
    QObject::connect(actOpen, &QAction::triggered, this, &MainWindow::onOpenFolder);
    menu->addSeparator();

    auto* actStart = menu->addAction(tgui::theme::icon(Icon::Start), tr("Start"));
    actStart->setShortcut(QKeySequence(Qt::Key_F3));
    actStart->setEnabled(hasSel);
    QObject::connect(actStart, &QAction::triggered, this, &MainWindow::onStartSelected);
    auto* actForce = menu->addAction(stdIcon(QStyle::SP_MediaPlay), tr("Force start"));
    actForce->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    actForce->setEnabled(hasSel);
    QObject::connect(actForce, &QAction::triggered, this, &MainWindow::onStartForceSelected);
    auto* actStop = menu->addAction(tgui::theme::icon(Icon::Pause), tr("Pause"));
    actStop->setShortcut(QKeySequence(Qt::Key_F4));
    actStop->setEnabled(hasSel);
    QObject::connect(actStop, &QAction::triggered, this, &MainWindow::onStopSelected);
    menu->addSeparator();

    auto* actRemove = menu->addAction(tgui::theme::icon(Icon::Remove), tr("Remove…"));
    actRemove->setShortcut(QKeySequence(Qt::Key_Delete));
    actRemove->setEnabled(hasSel);
    QObject::connect(actRemove, &QAction::triggered, this, &MainWindow::onRemoveSelected);
    auto* actRemoveData = menu->addAction(tr("Remove with data…"));
    actRemoveData->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    actRemoveData->setEnabled(hasSel);
    QObject::connect(actRemoveData, &QAction::triggered, this, &MainWindow::onRemoveWithDataSelected);
    menu->addSeparator();

    auto* mPriority = menu->addMenu(tr("Priority"));
    mPriority->setEnabled(hasSel);
    auto* priHigh = mPriority->addAction(tr("High"));
    auto* priNormal = mPriority->addAction(tr("Normal"));
    auto* priLow = mPriority->addAction(tr("Low"));
    QObject::connect(priHigh, &QAction::triggered, this, &MainWindow::onPriorityHigh);
    QObject::connect(priNormal, &QAction::triggered, this, &MainWindow::onPriorityNormal);
    QObject::connect(priLow, &QAction::triggered, this, &MainWindow::onPriorityLow);

    auto* mQueue = menu->addMenu(tr("Queue"));
    mQueue->setEnabled(hasSel);
    auto* qTop = mQueue->addAction(tr("Move to top"));
    auto* qUp = mQueue->addAction(tr("Move up"));
    auto* qDown = mQueue->addAction(tr("Move down"));
    auto* qBottom = mQueue->addAction(tr("Move to bottom"));
    QObject::connect(qTop, &QAction::triggered, this, &MainWindow::onQueueTop);
    QObject::connect(qUp, &QAction::triggered, this, &MainWindow::onQueueUp);
    QObject::connect(qDown, &QAction::triggered, this, &MainWindow::onQueueDown);
    QObject::connect(qBottom, &QAction::triggered, this, &MainWindow::onQueueBottom);
    menu->addSeparator();

    auto* actAnnounce = menu->addAction(tr("Announce (get more peers)"));
    actAnnounce->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    actAnnounce->setEnabled(hasSel);
    QObject::connect(actAnnounce, &QAction::triggered, this, &MainWindow::onReannounceSelected);
    auto* actVerify = menu->addAction(tr("Verify…"));
    actVerify->setEnabled(hasSel);
    QObject::connect(actVerify, &QAction::triggered, this, &MainWindow::onVerifySelected);
    menu->addSeparator();

    auto* actCopyMagnet = menu->addAction(tr("Copy magnet link"));
    actCopyMagnet->setEnabled(hasSel);
    QObject::connect(actCopyMagnet, &QAction::triggered, this, &MainWindow::onCopyMagnet);
    auto* actMove = menu->addAction(tgui::theme::icon(Icon::Move), tr("Set location…"));
    actMove->setShortcut(QKeySequence(Qt::Key_F6));
    actMove->setEnabled(hasSel);
    QObject::connect(actMove, &QAction::triggered, this, &MainWindow::onMoveSelected);
    auto* actRename = menu->addAction(tr("Rename…"));
    actRename->setShortcut(QKeySequence(Qt::Key_F2));
    actRename->setEnabled(hasSel);
    QObject::connect(actRename, &QAction::triggered, this, &MainWindow::onRenameSelected);
    menu->addSeparator();

    auto* actProperties = menu->addAction(tgui::theme::icon(Icon::Properties), tr("Properties…"));
    actProperties->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    actProperties->setEnabled(hasSel);
    QObject::connect(actProperties, &QAction::triggered, this, &MainWindow::onShowProperties);
    menu->addSeparator();

    auto* actRefresh = menu->addAction(tgui::theme::icon(Icon::Refresh), tr("Refresh"));
    actRefresh->setEnabled(m_connected);
    QObject::connect(actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);
    auto* actSelectAll = menu->addAction(tr("Select all"));
    actSelectAll->setEnabled(m_connected);
    QObject::connect(actSelectAll, &QAction::triggered, this, &MainWindow::onSelectAll);

    menu->exec(m_view->mapToGlobal(pos));
}

void MainWindow::onAddTorrent()
{
    if (!m_rpc)
        return;
    const auto& s = m_rpc->sessionInfo();
    AddTorrentDialog dlg(QString(), s.downloadDir, m_rpc->rpcVersion(),
                         s.startAddedTorrents, m_rpc.get(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    startAdd(dlg.params(), dlg.saveEnabled(), dlg.savePath());
}

void MainWindow::startAdd(const tgui::RpcClient::AddTorrent& params, bool doSave,
                          const QString& savePath)
{
    if (!m_rpc)
        return;
    m_pendingAdd = params;
    m_pendingAddId = -1;

    if (doSave && !savePath.isEmpty()) {
        if (params.source == tgui::RpcClient::AddTorrent::Source::File) {
            if (!QFile::copy(params.file, savePath)) {
                QMessageBox::warning(this, tr("Saving"),
                                     tr("Unable to save file: %1").arg(savePath));
                return;
            }
        } else if (params.source == tgui::RpcClient::AddTorrent::Source::Url &&
                   (params.url.startsWith(QLatin1String("http://")) ||
                    params.url.startsWith(QLatin1String("https://")))) {
            downloadAndAdd(params, savePath);
            return;
        }
        // magnet — сохранять нечего.
    }
    m_rpc->addTorrent(params);
}

// Главное меню — структура по референсу (Torrent / View / Tools / Help,
// пункты — main.lfm).
void MainWindow::createMenus()
{
    // --- Torrent (пункты и порядок — по референсу transgui) ---
    auto* mTorrent = menuBar()->addMenu(tr("&Torrent"));

    // Connect to Transmission (подменю: New / Manage — как в OLD miConnect)
    auto* mConnect = mTorrent->addMenu(tr("Connect to Transmission"));
    connect(mConnect, &QMenu::aboutToShow, this, [this, mConnect]() {
        mConnect->clear();
        auto* aManage = mConnect->addAction(tr("Manage connections…"));
        connect(aManage, &QAction::triggered, this, &MainWindow::onConnectionOptions);
    });
    // Disconnect — сестринский пункт (как в OLD, не внутри подменю)
    mTorrent->addAction(m_actDisconnect);

    mTorrent->addSeparator();
    mTorrent->addAction(m_actAdd);
    mTorrent->addAction(m_actAddLink);
    mTorrent->addAction(m_actStart);
    auto* actForceStart = mTorrent->addAction(tr("Force start"));
    actForceStart->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(actForceStart, &QAction::triggered, this, &MainWindow::onStartForceSelected);
    mTorrent->addAction(m_actStartAll);
    mTorrent->addAction(m_actStop);
    mTorrent->addAction(m_actStopAll);
    mTorrent->addAction(m_actRemove);
    auto* actRemoveData = mTorrent->addAction(tr("Remove torrent and Data"));
    connect(actRemoveData, &QAction::triggered, this, &MainWindow::onRemoveWithDataSelected);
    mTorrent->addSeparator();

    auto* mPriority = mTorrent->addMenu(tr("Priority"));
    auto* priHigh = mPriority->addAction(tr("High priority"));
    auto* priNormal = mPriority->addAction(tr("Normal priority"));
    auto* priLow = mPriority->addAction(tr("Low priority"));
    connect(priHigh, &QAction::triggered, this, &MainWindow::onPriorityHigh);
    connect(priNormal, &QAction::triggered, this, &MainWindow::onPriorityNormal);
    connect(priLow, &QAction::triggered, this, &MainWindow::onPriorityLow);

    auto* mQueue = mTorrent->addMenu(tr("Queue"));
    auto* qTop = mQueue->addAction(tr("Move top"));
    auto* qUp = mQueue->addAction(tr("Move up"));
    auto* qDown = mQueue->addAction(tr("Move down"));
    auto* qBottom = mQueue->addAction(tr("Move bottom"));
    connect(qTop, &QAction::triggered, this, &MainWindow::onQueueTop);
    connect(qUp, &QAction::triggered, this, &MainWindow::onQueueUp);
    connect(qDown, &QAction::triggered, this, &MainWindow::onQueueDown);
    connect(qBottom, &QAction::triggered, this, &MainWindow::onQueueBottom);

    auto* actReannounce = mTorrent->addAction(tr("Reannounce (get more peers)"));
    auto* actVerify = mTorrent->addAction(tr("Verify"));
    connect(actReannounce, &QAction::triggered, this, &MainWindow::onReannounceSelected);
    connect(actVerify, &QAction::triggered, this, &MainWindow::onVerifySelected);
    mTorrent->addAction(m_actMove);  // "Set data location..."
    auto* actLabels = mTorrent->addAction(tr("Set labels…"));
    connect(actLabels, &QAction::triggered, this, &MainWindow::onSetLabels);
    auto* actRename = mTorrent->addAction(tr("Rename"));
    connect(actRename, &QAction::triggered, this, &MainWindow::onRenameSelected);
    mTorrent->addAction(m_actProperties);
    mTorrent->addSeparator();
    auto* actExport = mTorrent->addAction(tr("Export settings…"));
    connect(actExport, &QAction::triggered, this, &MainWindow::onExportSettings);
    auto* actImport = mTorrent->addAction(tr("Import settings…"));
    connect(actImport, &QAction::triggered, this, &MainWindow::onImportSettings);
    mTorrent->addSeparator();
    auto* actExit = mTorrent->addAction(tr("E&xit"));
    actExit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
    connect(actExit, &QAction::triggered, this, &MainWindow::onExit);

    // --- View (порядок — по референсу; Big Toolbar / Status bar - Sizes /
    // Menu в списке не показываем, как просили) ---
    auto* mView = menuBar()->addMenu(tr("&View"));
    auto* actSelectAll = mView->addAction(tr("Select all"));
    actSelectAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    connect(actSelectAll, &QAction::triggered, this, &MainWindow::onSelectAll);
    auto* actColumns = mView->addAction(tr("Setup columns…"));
    connect(actColumns, &QAction::triggered, this, &MainWindow::onColumns);
    mView->addSeparator();

    m_actFolderGrouping = mView->addAction(tr("Folder grouping"));
    m_actFolderGrouping->setCheckable(true);
    m_actFolderGrouping->setChecked(QSettings().value("ui/folderGrouping", true).toBool());
    connect(m_actFolderGrouping, &QAction::toggled, this, [this](bool on) {
        m_filterPanel->setFolderGroupingEnabled(on);
        QSettings().setValue("ui/folderGrouping", on);
        if (!on && !m_filterPanel->selectedFolder().isEmpty())
            m_filterPanel->selectAllTorrents();
    });

    m_actLabelGrouping = mView->addAction(tr("Label grouping"));
    m_actLabelGrouping->setCheckable(true);
    m_actLabelGrouping->setChecked(QSettings().value("ui/labelGrouping", true).toBool());
    connect(m_actLabelGrouping, &QAction::toggled, this, [this](bool on) {
        m_filterPanel->setLabelGroupingEnabled(on);
        QSettings().setValue("ui/labelGrouping", on);
        if (!on && !m_filterPanel->selectedLabel().isEmpty())
            m_filterPanel->selectAllTorrents();
    });

    m_actTrackerGrouping = mView->addAction(tr("Tracker grouping"));
    m_actTrackerGrouping->setCheckable(true);
    m_actTrackerGrouping->setChecked(QSettings().value("ui/trackerGrouping", true).toBool());
    connect(m_actTrackerGrouping, &QAction::toggled, this, [this](bool on) {
        m_filterPanel->setTrackerGroupingEnabled(on);
        QSettings().setValue("ui/trackerGrouping", on);
        if (!on && !m_filterPanel->selectedTracker().isEmpty())
            m_filterPanel->selectAllTorrents();
    });
    mView->addSeparator();

    // Видимость панелей. F11/F12 и Shift+F11/Shift+F12 работают и тогда,
    // когда меню/тулбар скрыты — действия добавлены в окно.
    // Ключи QSettings переименованы (ui/filterPane→ui/filterPanel и т.д.);
    // старое имя считывается как fallback, чтобы не потерять сохранённое состояние.
    auto uiBool = [](const char* key, const char* legacy, bool def) {
        QSettings s;
        if (s.contains(key))
            return s.value(key, def).toBool();
        if (s.contains(legacy))
            return s.value(legacy, def).toBool();
        return def;
    };

    m_actFilterPanel = mView->addAction(tr("Filter panel"));
    m_actFilterPanel->setCheckable(true);
    m_actFilterPanel->setShortcut(QKeySequence(Qt::Key_F11));
    m_actFilterPanel->setChecked(uiBool("ui/filterPanel", "ui/filterPane", true));
    connect(m_actFilterPanel, &QAction::toggled, this, [this](bool on) {
        m_filterPanel->setVisible(on);
        QSettings().setValue("ui/filterPanel", on);
    });

    m_actInfoPanel = mView->addAction(tr("Info panel"));
    m_actInfoPanel->setCheckable(true);
    m_actInfoPanel->setShortcut(QKeySequence(Qt::Key_F12));
    m_actInfoPanel->setChecked(uiBool("ui/infoPanel", "ui/infoPane", true));
    connect(m_actInfoPanel, &QAction::toggled, this, [this](bool on) {
        m_tabs->setVisible(on);
        QSettings().setValue("ui/infoPanel", on);
    });

    m_actToolbar = mView->addAction(tr("Toolbar"));
    m_actToolbar->setCheckable(true);
    m_actToolbar->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    m_actToolbar->setChecked(QSettings().value("ui/showToolbar", true).toBool());
    connect(m_actToolbar, &QAction::toggled, this, [this](bool on) {
        m_toolBar->setVisible(on);
        QSettings().setValue("ui/showToolbar", on);
    });

    m_actStatusBar = mView->addAction(tr("Status bar"));
    m_actStatusBar->setCheckable(true);
    m_actStatusBar->setChecked(QSettings().value("ui/statusBar", true).toBool());
    connect(m_actStatusBar, &QAction::toggled, this, [this](bool on) {
        statusBar()->setVisible(on);
        QSettings().setValue("ui/statusBar", on);
    });

    // «Menu» убран из видимого меню, но остаётся действием с Shift+F12,
    // чтобы скрытое меню можно было вернуть.
    m_actMenu = new QAction(tr("Menu"), this);
    m_actMenu->setCheckable(true);
    m_actMenu->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F12));
    m_actMenu->setChecked(QSettings().value("ui/showMenu", true).toBool());
    connect(m_actMenu, &QAction::toggled, this, [this](bool on) {
        menuBar()->setVisible(on);
        QSettings().setValue("ui/showMenu", on);
    });
    addAction(m_actMenu);

    applyViewVisibility();

    // --- Tools (порядок — точное соответствие OLD) ---
    auto* mTools = menuBar()->addMenu(tr("T&ools"));
    mTools->addAction(m_actDaemonOptions);
    mTools->addAction(m_actOptions);
    mTools->addSeparator();
    mTools->addAction(m_actAltSpeed);  // "Use alternate bandwidth settings"
    auto* actBlocklist = mTools->addAction(tr("Update blocklist"));
    connect(actBlocklist, &QAction::triggered, this, &MainWindow::onUpdateBlocklist);
    // --- Help (порядок — по референсу transgui) ---
    auto* mHelp = menuBar()->addMenu(tr("&Help"));
    auto* actCheck = mHelp->addAction(tr("Check for updates"));
    connect(actCheck, &QAction::triggered, this, &MainWindow::onCheckUpdate);
    auto* actHome = mHelp->addAction(tr("Visit home page"));
    connect(actHome, &QAction::triggered, this, &MainWindow::onHome);
    auto* actDev = mHelp->addAction(tr("Development site"));
    connect(actDev, &QAction::triggered, this, &MainWindow::onDevSite);
    mHelp->addSeparator();
    mHelp->addAction(m_actAbout);
}

// Применить сохранённую видимость панелей при старте (раньше checked
// восстанавливался, но сами панели оставались видимыми).
void MainWindow::applyViewVisibility()
{
    if (m_actFilterPanel && m_filterPanel)
        m_filterPanel->setVisible(m_actFilterPanel->isChecked());
    if (m_actInfoPanel && m_tabs)
        m_tabs->setVisible(m_actInfoPanel->isChecked());
    if (m_actToolbar && m_toolBar)
        m_toolBar->setVisible(m_actToolbar->isChecked());
    if (m_actStatusBar)
        statusBar()->setVisible(m_actStatusBar->isChecked());
    if (m_actMenu)
        menuBar()->setVisible(m_actMenu->isChecked());
}

void MainWindow::onSetLabels()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    // Стартовый текст — метки первого выбранного торрента.
    QStringList current;
    for (const auto& t : m_model.torrents()) {
        if (t.id == ids.first()) {
            if (t.labels)
                current = *t.labels;
            break;
        }
    }
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, tr("Set labels"), tr("Labels (comma-separated):"),
        QLineEdit::Normal, current.join(QStringLiteral(", ")), &ok);
    if (!ok)
        return;
    QStringList labels;
    for (const QString& piece : text.split(',', Qt::SkipEmptyParts)) {
        const QString s = piece.trimmed();
        if (!s.isEmpty())
            labels << s;
    }
    for (const int id : ids)
        m_rpc->setLabels(id, labels);
}

void MainWindow::onExportSettings()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export settings"), QStringLiteral("transmissiongui.ini"),
        tr("INI files (*.ini)"));
    if (path.isEmpty())
        return;
    QSettings cur;
    const QStringList keys = cur.allKeys();
    QSettings out(path, QSettings::IniFormat);
    for (const QString& k : keys)
        out.setValue(k, cur.value(k));
    out.sync();
    statusBar()->showMessage(tr("Settings exported to: %1").arg(path), 4000);
}

void MainWindow::onImportSettings()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import settings"), QString(), tr("INI files (*.ini)"));
    if (path.isEmpty() || !QFileInfo(path).exists())
        return;
    if (QMessageBox::question(this, tr("Import settings"),
            tr("Import settings from file? Current settings will be overwritten.\n"
               "Restart the application to apply the changes."))
            != QMessageBox::Yes)
        return;
    QSettings in(path, QSettings::IniFormat);
    QSettings cur;
    for (const QString& k : cur.allKeys())
        cur.remove(k);
    for (const QString& k : in.allKeys())
        cur.setValue(k, in.value(k));
    cur.sync();
    statusBar()->showMessage(tr("Settings imported. Restart to apply."), 4000);
}

void MainWindow::onSelectAll()
{
    m_view->selectAll();
    updateSelectionActions();
}

void MainWindow::onCopyPath()
{
    const auto ids = selectedTorrentIds();
    if (ids.size() != 1)
        return;
    for (const auto& t : m_model.torrents()) {
        if (t.id == ids.first()) {
            const QString dir = t.downloadDir.value_or(QString());
            QString name = t.name.value_or(QString());
            if (!name.isEmpty())
                name = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);
            const QString path = dir + QStringLiteral("/") + name;
            QApplication::clipboard()->setText(path);
            statusBar()->showMessage(tr("Path copied"), 3000);
            return;
        }
    }
}

void MainWindow::onOpenFolder()
{
    const auto ids = selectedTorrentIds();
    if (ids.size() != 1)
        return;
    for (const auto& t : m_model.torrents()) {
        if (t.id == ids.first()) {
            const QString dir = t.downloadDir.value_or(QString());
            if (dir.isEmpty())
                return;
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            return;
        }
    }
}

// Двойной клик по строке во вкладке «Файлы» (референс: lvFilesDblClick → acOpenFile):
// папка — открыть; файл, полностью скачанный — запустить; иначе — открыть каталог.
void MainWindow::onFilesDoubleClicked(int row)
{
    if (m_currentId < 0)
        return;
    const auto& files = m_fileModel.files();
    if (row < 0 || row >= files.size())
        return;
    const tgui::Torrent* t = nullptr;
    for (const auto& x : m_model.torrents()) {
        if (x.id == m_currentId) {
            t = &x;
            break;
        }
    }
    const QString dir = (t && t->downloadDir.has_value()) ? t->downloadDir.value() : QString();
    if (dir.isEmpty())
        return;
    const QString rel = files.at(row).name;
    if (rel.endsWith(QLatin1Char('/'))) {
        openLocalPath(dir + QStringLiteral("/") + rel, /*selectFile=*/false);  // папка
        return;
    }
    const QString path = dir + QStringLiteral("/") + rel;
    // Не скачанный до конца файл не имеет смысла запускать — открываем каталог.
    openLocalPath(path, /*selectFile=*/files.at(row).completion < 1.0);
}

// Двойной клик по строке списка торрентов (референс: gTorrentsDblClick):
// открыть файл (однофайловый) / папку торрента; fallback — каталог загрузки.
void MainWindow::onTorrentsDoubleClicked()
{
    if (m_currentId < 0)
        return;
    const tgui::Torrent* t = nullptr;
    for (const auto& x : m_model.torrents()) {
        if (x.id == m_currentId) {
            t = &x;
            break;
        }
    }
    if (!t)
        return;
    const QString dir = t->downloadDir.value_or(QString());
    if (dir.isEmpty())
        return;
    const QString name = t->name.value_or(QString());
    // Готовый торрент (leftUntilDone == 0) — открыть его файл/папку;
    // не готовый — просто каталог (файл ещё не скачан, запускать нечего).
    const bool finished = t->leftUntilDone.has_value() && t->leftUntilDone.value() <= 0.0
                          && (t->sizeWhenDone.value_or(0.0) > 0.0);
    if (finished && !name.isEmpty())
        openLocalPath(dir + QStringLiteral("/") + name, /*selectFile=*/false);
    else
        openLocalPath(dir, /*selectFile=*/false);
}

// Открыть локальный путь: каталог — в проводнике; файл — по умолчанию запустить;
// файла нет (или нужен «выбрать») — открыть родительский каталог. (аналог OLD
// ExecRemoteFile/OpenURL: SelectFile=false → ShellExecute "open"; else → каталог).
void MainWindow::openLocalPath(const QString& path, bool selectFile)
{
    if (path.isEmpty())
        return;
    // Маппинг remote→local: путь демона (на сервере) → локальный.
    // Правила задаются в Application options → Paths (строки "remote=local").
    const QStringList mappings = QSettings().value("paths/mappings").toStringList();
    const QString local = tgui::mapRemoteToLocal(path, mappings);

    const QFileInfo fi(local);
    if (fi.exists() && (fi.isDir() || !selectFile)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.filePath()));
        return;
    }
    const QDir d = fi.dir();
    if (d.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(d.absolutePath()));
        return;
    }
    statusBar()->showMessage(tr("Path not found: %1").arg(local), 4000);
}

void MainWindow::onCopyMagnet()
{
    const auto ids = selectedTorrentIds();
    if (ids.size() != 1)
        return;
    for (const auto& t : m_model.torrents()) {
        if (t.id == ids.first()) {
            QString magnet = t.magnetLink.value_or(QString());
            if (magnet.isEmpty() && t.hashString.has_value())
                magnet = QStringLiteral("magnet:?xt=urn:btih:%1").arg(t.hashString.value());
            if (magnet.isEmpty())
                return;
            QApplication::clipboard()->setText(magnet);
            statusBar()->showMessage(tr("Magnet link copied"), 3000);
            return;
        }
    }
}

void MainWindow::onRemoveWithDataSelected()
{
    if (!m_rpc)
        return;
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty())
        return;
    auto* box = new QMessageBox(this);
    box->setWindowTitle(tr("Remove with data"));
    box->setIcon(QMessageBox::Warning);
    box->setText(tr("Remove the selected torrent(s) AND delete their downloaded data files?"));
    box->setInformativeText(tr("This cannot be undone."));
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    const int r = box->exec();
    box->deleteLater();
    if (r != QMessageBox::Yes)
        return;
    for (int id : ids)
        m_rpc->removeTorrent(id, true);
    m_rpc->getTorrents();
}

void MainWindow::onVerifySelected()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    const auto res = QMessageBox::question(
        this, tr("Verify"),
        ids.size() == 1
            ? tr("Torrent verification may take a long time.\nAre you sure to start verification?")
            : tr("Torrent verification may take a long time.\nAre you sure to start verification of %1 torrents?")
                  .arg(ids.size()));
    if (res != QMessageBox::Yes)
        return;
    for (int id : ids)
        m_rpc->verify(id);
}

void MainWindow::onReannounceSelected()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->reannounce(id);
}

void MainWindow::onQueueTop()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->queueMoveTop(id);
    m_rpc->getTorrents();
}

void MainWindow::onQueueUp()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->queueMoveUp(id);
    m_rpc->getTorrents();
}

void MainWindow::onQueueDown()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->queueMoveDown(id);
    m_rpc->getTorrents();
}

void MainWindow::onQueueBottom()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->queueMoveBottom(id);
    m_rpc->getTorrents();
}

void MainWindow::onStartForceSelected()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->startTorrent(id, true); // torrent-start-now
    m_rpc->getTorrents();
}

void MainWindow::onAddFromLink()
{
    if (!m_rpc || !m_connected)
        return;
    AddLinkDialog dlg(QSettings().value("lastAddLink").toString(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString link = dlg.link();
    if (link.isEmpty())
        return;
    QSettings().setValue("lastAddLink", link);
    tgui::RpcClient::AddTorrent at;
    at.source = tgui::RpcClient::AddTorrent::Source::Url;
    at.url = link;
    at.start = dlg.startWhenAdded();
    m_rpc->addTorrent(at);
    m_rpc->getTorrents();
}

void MainWindow::onUpdateBlocklist()
{
    if (m_rpc && m_connected)
        m_rpc->updateBlocklist();
}

void MainWindow::onExit()
{
    close();
}

void MainWindow::onRenameSelected()
{
    const auto ids = selectedTorrentIds();
    if (ids.size() != 1 || !m_rpc || !m_connected) {
        if (ids.size() > 1)
            QMessageBox::warning(this, tr("Rename"), tr("Only one torrent can be renamed."));
        return;
    }
    const int id = ids.first();
    const tgui::Torrent* found = nullptr;
    for (const auto& t : m_model.torrents())
        if (t.id == id) {
            found = &t;
            break;
        }
    if (!found || !found->name.has_value())
        return;
    const QString currentName = found->name.value();
    if (currentName.isEmpty())
        return;
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename"), tr("New name:"),
        QLineEdit::Normal, currentName, &ok);
    const QString trimmed = newName.trimmed();
    if (!ok || trimmed.isEmpty() || trimmed == currentName)
        return;
    // path = корень (текущее имя), name = новое имя (torrent-rename-path).
    m_rpc->renamePath(id, currentName, trimmed);
    m_rpc->getTorrents();
}

void MainWindow::onHome()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://transmissionbt.com")));
}

void MainWindow::onDevSite()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/transmissionbt/transmission")));
}

void MainWindow::onCheckUpdate()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://transmissionbt.com/#release-notes")));
}

void MainWindow::onPriorityHigh()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->setPriority(id, 1);
    m_rpc->getTorrents();
}

void MainWindow::onPriorityNormal()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->setPriority(id, 0);
    m_rpc->getTorrents();
}

void MainWindow::onPriorityLow()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    for (int id : ids)
        m_rpc->setPriority(id, -1);
    m_rpc->getTorrents();
}

void MainWindow::onPortTest()
{
    if (!m_rpc || !m_connected)
        return;
    m_rpc->portTest();
}

void MainWindow::onOptions()
{
    // W07: диалог параметров (интерфейс: язык/масштаб/интервал; трей).
    OptionsDialog dlg(this, m_connected && m_rpc ? m_rpc->rpcVersion() : 0);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // Интервал обновления — применяем сразу (запущенный таймер подхватит новый интервал).
    m_refreshTimer.setInterval(QSettings().value("ui/refreshInterval", kRefreshIntervalMs / 1000).toInt() * 1000);
    // Иконка трея — применяем сразу (показать/скрыть существующую).
    if (!m_tray && QSettings().value("ui/trayIcon", true).toBool())
        createTray();
    if (m_tray)
        m_tray->setVisible(QSettings().value("ui/trayIcon", true).toBool());

    // Язык/масштаб — применяются при следующем запуске (см. main.cpp).
    if (dlg.restartRequired())
        QMessageBox::information(this, tr("Options"),
                                 tr("Language or interface scale changed. Restart the application to apply the new value."));
}

void MainWindow::onConnectionOptions()
{
    // W08: диалог параметров соединений (несколько демонов, TLS, RPC-путь, прокси).
    ConnOptionsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    // Профиль, над которым работали, сделан текущим (saveCurrent) — переключаемся
    // на него (работает и при активном соединении; аналог OLD ConnectionSettingsChanged).
    ProfileStore store;
    if (store.hasCurrent())
        switchToProfile(store.currentName());
}

// Переключение на другой сервер (аналог OLD DoConnectToHost): отключиться,
// сделать профиль активным, подключиться к нему.
void MainWindow::switchToProfile(const QString& name)
{
    if (name.isEmpty())
        return;
    ProfileStore store;
    if (!store.exists(name))
        return;
    // Уже подключены к этому серверу — не переключаем (OLD: DoConnectToHost
    // выходит, если FCurConn == выбранное имя).
    const auto opts = store.profile(name).toOptions();
    if (m_connected && m_rpc) {
        const auto& active = m_rpc->options();
        if (active.url == opts.url && active.rpcPath == opts.rpcPath &&
            active.username == opts.username && active.password == opts.password &&
            active.ignoreSslErrors == opts.ignoreSslErrors && active.proxy == opts.proxy) {
            store.setCurrent(name);
            setConnectedUi(true); // Renaming does not require reconnecting.
            return;
        }
    }
    onDisconnect();
    store.setCurrent(name);
    if (opts.url.isEmpty()) {
        statusBar()->showMessage(tr("Server '%1' has no host set.").arg(name), 4000);
        return;
    }
    saveSettings(opts);
    startConnect(opts);
}

void MainWindow::onDaemonOptions()
{
    // W09: редактируемые настройки демона (session-set), применяются к живому демону.
    if (!m_rpc || !m_connected) {
        QMessageBox::information(this, tr("Daemon options"), tr("Not connected to the daemon."));
        return;
    }
    DaemonOptionsDialog dlg(m_rpc->sessionInfo(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QJsonObject params = dlg.sessionSet();
    m_rpc->setSession(params); // фидбек — через onRequestCompleted (status bar)
    statusBar()->showMessage(tr("Applying daemon options…"), 3000);
}

void MainWindow::onToggleAltSpeed()
{
}

void MainWindow::downloadAndAdd(const tgui::RpcClient::AddTorrent& params,
                                const QString& dst)
{
    statusBar()->showMessage(tr("Downloading %1…").arg(params.url), 5000);
    auto* reply = m_nam.get(QNetworkRequest(QUrl(params.url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, params, dst] {
        const bool netOk = reply->error() == QNetworkReply::NoError;
        const QByteArray payload = reply->readAll();
        if (netOk) {
            QFile f(dst);
            if (!f.open(QIODevice::WriteOnly) || f.write(payload) < 0)
                QMessageBox::warning(this, tr("Saving"),
                                     tr("Unable to write file: %1").arg(dst));
        } else {
            QMessageBox::warning(this, tr("Downloading"), reply->errorString());
        }
        reply->deleteLater();
        if (m_rpc)
            m_rpc->addTorrent(params); // демон сам загрузит торрент
    });
}

void MainWindow::onTorrentDetailsReceived(const tgui::TorrentDetails& details)
{
    if (!m_rpc)
        return;
    // Ответ на запрос деталей добавляемого торрента — приоритетный сценарий.
    if (details.id == m_pendingAddId) {
        if (!details.files.isEmpty()) {
            FileSelectDialog dlg(details.files, this);
            if (dlg.exec() == QDialog::Accepted)
                m_rpc->setFilesWanted(details.id, dlg.wanted(), dlg.unwanted());
        }
        finishAdd();
        return;
    }
    // Вкладка «Файлы» для выбранного торрента.
    if (details.id != m_currentId)
        return;
    m_fileModel.setFiles(details.files);
}

void MainWindow::onPeersReceived(int id, const QVector<tgui::Peer>& peers)
{
    if (id != m_currentId)
        return;
    m_peerModel.setPeers(peers);
}

void MainWindow::onTrackersReceived(int id, const QVector<tgui::TrackerStat>& trackers)
{
    if (id != m_currentId)
        return;
    m_trackerModel.setTrackers(trackers);
}

void MainWindow::onSelectionChanged()
{
    // modelReset при автообновлении временно «сбрасывает» выделение —
    // это обрабатывается onTorrentsReceived (восстановление по prevId).
    if (m_refreshing)
        return;
    int id = -1;
    if (m_view->selectionModel()) {
        for (const QModelIndex& vi : m_view->selectionModel()->selectedIndexes()) {
            const QModelIndex src = m_proxy.mapToSource(vi);
            if (!src.isValid())
                continue;
            id = m_model.torrentAt(src.row()).id;
            break;
        }
        // Программный setCurrentIndex / навигация без выделения: берём current.
        if (id < 0) {
            const QModelIndex vi = m_view->selectionModel()->currentIndex();
            if (vi.isValid()) {
                const QModelIndex src = m_proxy.mapToSource(vi);
                if (src.isValid())
                    id = m_model.torrentAt(src.row()).id;
            }
        }
    }
    if (id == m_currentId) {
        updateSelectionActions();
        return;
    }
    m_currentId = id;
    // «General» обновляем из модели сразу — до ответа RPC.
    if (id >= 0) {
        for (const auto& t : m_model.torrents()) {
            if (t.id == id) {
                m_general->setTorrent(t);
                break;
            }
        }
    } else {
        m_general->clear();
    }
    if (id < 0 || !m_rpc) {
        updateSelectionActions();
        return;
    }
    m_rpc->getTorrentDetails(id);
    m_rpc->getPeers(id);
    m_rpc->getTrackers(id);
    updateSelectionActions();
}

void MainWindow::onFileWantToggled(int, bool)
{
    if (!m_rpc || m_currentId < 0)
        return;
    QVector<int> wanted, unwanted;
    for (const auto& f : m_fileModel.files())
        (f.wanted ? wanted : unwanted).push_back(f.index);
    m_rpc->setFilesWanted(m_currentId, wanted, unwanted);
    statusBar()->showMessage(tr("want/skip updated"), 4000);
}

void MainWindow::onFilesContextMenu(const QPoint& pos)
{
    if (!m_rpc || m_currentId < 0)
        return;
    auto* view = qobject_cast<QTreeView*>(sender());
    if (!view || !view->selectionModel())
        return;
    const auto selected = view->selectionModel()->selectedRows();
    const auto& files = m_fileModel.files();
    QVector<int> fileIds;
    for (const QModelIndex& idx : selected)
        for (int row : m_fileTree->sourceRows(idx))
            if (row >= 0 && row < files.size() && !fileIds.contains(files.at(row).index))
                fileIds.append(files.at(row).index);
    if (fileIds.isEmpty())
        return;

    auto* menu = new QMenu(this);
    auto* aHigh = menu->addAction(tr("High priority"));
    auto* aNormal = menu->addAction(tr("Normal priority"));
    auto* aLow = menu->addAction(tr("Low priority"));
    menu->addSeparator();
    auto* aWant = menu->addAction(tr("Wanted"));
    auto* aSkip = menu->addAction(tr("Skip (don't download)"));

    const QPoint global = view->mapToGlobal(pos);
    QObject::connect(aHigh, &QAction::triggered, this, [this, fileIds] { onFilePriority(fileIds, 1); });
    QObject::connect(aNormal, &QAction::triggered, this, [this, fileIds] { onFilePriority(fileIds, 0); });
    QObject::connect(aLow, &QAction::triggered, this, [this, fileIds] { onFilePriority(fileIds, -1); });
    QObject::connect(aWant, &QAction::triggered, this, [this, fileIds] { onFileWantSelected(fileIds); });
    QObject::connect(aSkip, &QAction::triggered, this, [this, fileIds] { onFileSkipSelected(fileIds); });
    menu->exec(global);
    menu->deleteLater();
}

void MainWindow::onFilePriority(const QVector<int>& fileIds, int priority)
{
    if (!m_rpc || m_currentId < 0 || fileIds.isEmpty())
        return;
    m_rpc->setFilePriority(m_currentId, fileIds, priority);
    m_rpc->getTorrentDetails(m_currentId); // перечитать детали (приоритет/статус)
}

void MainWindow::onFileWantSelected(const QVector<int>& fileIds)
{
    if (!m_rpc || m_currentId < 0 || fileIds.isEmpty())
        return;
    m_rpc->setFilesWanted(m_currentId, fileIds, {}); // только выделенные → wanted
    m_rpc->getTorrentDetails(m_currentId);
}

void MainWindow::onFileSkipSelected(const QVector<int>& fileIds)
{
    if (!m_rpc || m_currentId < 0 || fileIds.isEmpty())
        return;
    m_rpc->setFilesWanted(m_currentId, {}, fileIds); // только выделенные → skip
    m_rpc->getTorrentDetails(m_currentId);
}

void MainWindow::onTrackersContextMenu(const QPoint& pos)
{
    if (!m_rpc || m_currentId < 0)
        return;
    auto* view = qobject_cast<QTableView*>(sender());
    if (!view || !view->selectionModel())
        return;
    const auto selected = view->selectionModel()->selectedRows();
    const auto& trackers = m_trackerModel.trackers();
    bool hasSel = false;
    int trackerId = -1;
    QString announce;
    if (!selected.isEmpty()) {
        const int row = selected.first().row();
        if (row >= 0 && row < trackers.size()) {
            hasSel = true;
            trackerId = trackers.at(row).id;
            announce = trackers.at(row).announce;
        }
    }

    auto* menu = new QMenu(this);
    auto* aAdd = menu->addAction(tr("Add tracker…"));
    auto* aEdit = menu->addAction(tr("Edit tracker…"));
    auto* aRemove = menu->addAction(tr("Remove tracker…"));
    aEdit->setEnabled(hasSel);
    aRemove->setEnabled(hasSel);

    const QPoint global = view->mapToGlobal(pos);
    QObject::connect(aAdd, &QAction::triggered, this, [this] { onAddTracker(); });
    QObject::connect(aEdit, &QAction::triggered, this,
                     [this, trackerId, announce] { onEditTracker(trackerId, announce); });
    QObject::connect(aRemove, &QAction::triggered, this,
                     [this, trackerId, announce] { onRemoveTracker(trackerId, announce); });
    menu->exec(global);
    menu->deleteLater();
}

void MainWindow::onAddTracker()
{
    if (!m_rpc || m_currentId < 0)
        return;
    bool ok = false;
    const QString url = QInputDialog::getText(
                           this, tr("Add tracker"), tr("Tracker URL:"),
                           QLineEdit::Normal, QString(), &ok)
                           .trimmed();
    if (!ok)
        return;
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("Add tracker"), tr("No tracker URL was specified."));
        return;
    }
    m_rpc->addTracker(m_currentId, url);
    m_rpc->getTrackers(m_currentId); // обновить список
}

void MainWindow::onEditTracker(int trackerId, const QString& announce)
{
    if (!m_rpc || m_currentId < 0 || trackerId < 0)
        return;
    bool ok = false;
    const QString url = QInputDialog::getText(
                           this, tr("Edit tracker"), tr("New tracker URL:"),
                           QLineEdit::Normal, announce, &ok)
                           .trimmed();
    if (!ok)
        return;
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("Edit tracker"), tr("No tracker URL was specified."));
        return;
    }
    m_rpc->replaceTracker(m_currentId, trackerId, url);
    m_rpc->getTrackers(m_currentId);
}

void MainWindow::onRemoveTracker(int trackerId, const QString& announce)
{
    if (!m_rpc || m_currentId < 0 || trackerId < 0)
        return;
    const auto ret = QMessageBox::question(
        this, tr("Remove tracker"), tr("Remove tracker “%1”?").arg(announce));
    if (ret != QMessageBox::Yes)
        return;
    m_rpc->removeTracker(m_currentId, trackerId);
    m_rpc->getTrackers(m_currentId);
}

void MainWindow::finishAdd()
{
    if (m_rpc) {
        if (m_pendingAddId >= 0 && m_pendingStart)
            m_rpc->startTorrent(m_pendingAddId);
        m_rpc->getTorrents();
        statusBar()->showMessage(tr("Torrent added"), 5000);
    }
    m_pendingAddId = -1;
    m_pendingStart = false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!m_rpc || !m_connected)
        return;

    QStringList items;
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& u : event->mimeData()->urls())
            items << u.toString();
    } else if (event->mimeData()->hasText()) {
        for (const QString& line : event->mimeData()->text().split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty())
                items << line.trimmed();
    }

    const auto& s = m_rpc->sessionInfo();
    for (const QString& sItem : items) {
        if (sItem.startsWith(QLatin1String("magnet:")) ||
            sItem.startsWith(QLatin1String("http://")) ||
            sItem.startsWith(QLatin1String("https://"))) {
            tgui::RpcClient::AddTorrent p;
            p.source = tgui::RpcClient::AddTorrent::Source::Url;
            p.url = sItem;
            p.downloadDir = s.downloadDir;
            p.start = s.startAddedTorrents;
            startAdd(p, false, QString());
        } else {
            QString path = sItem;
            if (sItem.startsWith(QLatin1String("file:")))
                path = QUrl(sItem).toLocalFile();
            if (QFile::exists(path)) {
                tgui::RpcClient::AddTorrent p;
                p.source = tgui::RpcClient::AddTorrent::Source::File;
                p.file = path;
                p.downloadDir = s.downloadDir;
                p.start = s.startAddedTorrents;
                startAdd(p, false, QString());
            }
        }
    }
    event->acceptProposedAction();
}

void MainWindow::createTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    // W07: не создавать иконку, если в настройках выключен «always show tray icon».
    if (!QSettings().value("ui/trayIcon", true).toBool())
        return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeTrayIcon());
    setWindowIcon(makeTrayIcon());
    m_tray->setToolTip(QStringLiteral("transmissionGUI"));
    connect(this, &MainWindow::downloadNotification, m_tray, [this](const QString& message) {
        if (m_tray->isVisible())
            m_tray->showMessage(tr("Download completed"), message, QSystemTrayIcon::Information);
    });

    auto* menu = new QMenu(this);
    auto* actShow = menu->addAction(tr("Show"));
    auto* actStartAll = menu->addAction(tr("Start all"));
    auto* actStopAll = menu->addAction(tr("Pause all"));
    menu->addSeparator();
    auto* actRefresh = menu->addAction(tr("Refresh"));
    menu->addSeparator();
    auto* actExit = menu->addAction(tr("Exit"));

    QObject::connect(actShow, &QAction::triggered, this, [this] {
        show();
        activateWindow();
    });
    QObject::connect(actStartAll, &QAction::triggered, this, [this] { onStartAll(); });
    QObject::connect(actStopAll, &QAction::triggered, this, [this] { onStopAll(); });
    QObject::connect(actRefresh, &QAction::triggered, this, [this] { onRefresh(); });
    QObject::connect(actExit, &QAction::triggered, qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick ||
                    reason == QSystemTrayIcon::Trigger) {
                    show();
                    activateWindow();
                }
            });
    m_tray->show();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // W07: «close to tray» из настроек; иначе — выход.
    const bool closeToTray = QSettings().value("ui/closeToTray", true).toBool();
    if (closeToTray && m_tray && m_tray->isVisible()) {
        event->ignore();
        hide();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    // W07: «minimize to tray» — перехватываем переход окна в минимизированное состояние.
    if (event->type() == QEvent::WindowStateChange) {
        auto* wse = static_cast<QWindowStateChangeEvent*>(event);
        // Новое состояние — windowState() (уже обновлено к моменту события); старое — oldState().
        const bool nowMinimized = (windowState() & Qt::WindowMinimized) != 0;
        const bool wasMinimized = (wse->oldState() & Qt::WindowMinimized) != 0;
        if (nowMinimized && !wasMinimized &&
            QSettings().value("ui/minimizeToTray", false).toBool() &&
            m_tray && m_tray->isVisible()) {
            showNormal();
            hide();
            return;
        }
    }
    QMainWindow::changeEvent(event);
}

QPixmap MainWindow::makeTrayIcon()
{
    return tgui::theme::appIconPixmap(64);
}

void MainWindow::loadSettings(tgui::RpcClient::Options& opts)
{
    QSettings s;
    const QString url = s.value("connection/url").toString();
    if (!url.isEmpty())
        opts.url = QUrl(url);
    opts.rpcPath = s.value("connection/rpcPath").toString();
    opts.username = s.value("connection/user").toString();
    opts.password = s.value("connection/password").toString();
    opts.ignoreSslErrors = s.value("connection/ignoreSsl", false).toBool();
}

void MainWindow::saveSettings(const tgui::RpcClient::Options& opts)
{
    QSettings s;
    s.setValue("connection/url", opts.url.toString());
    s.setValue("connection/rpcPath", opts.rpcPath);
    s.setValue("connection/user", opts.username);
    s.setValue("connection/password", opts.password);
    s.setValue("connection/ignoreSsl", opts.ignoreSslErrors);
}
