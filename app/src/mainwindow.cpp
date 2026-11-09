#include "mainwindow.h"

#include <QAction>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QIcon>
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
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

#include "adddialog.h"
#include "connectdialog.h"
#include "delegates.h"
#include "fileselectdialog.h"
#include "filterpanel.h"
#include "movedialog.h"
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

    m_refreshTimer.setInterval(kRefreshIntervalMs);
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
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->header()->setHighlightSections(false);
    m_view->header()->setMinimumSectionSize(44);
    // Дефолтные ширины и видимый набор колонок — по референсу (main.lfm:
    // видны Name..ETA, остальные — Visible = False).
    m_view->setColumnWidth(TorrentModel::ColName, 280);
    m_view->setColumnWidth(TorrentModel::ColSize, 72);
    m_view->setColumnWidth(TorrentModel::ColDone, 108);
    m_view->setColumnWidth(TorrentModel::ColStatus, 96);
    m_view->setColumnWidth(TorrentModel::ColSeeds, 58);
    m_view->setColumnWidth(TorrentModel::ColPeers, 58);
    m_view->setColumnWidth(TorrentModel::ColRateDown, 82);
    m_view->setColumnWidth(TorrentModel::ColRateUp, 82);
    m_view->setColumnWidth(TorrentModel::ColEta, 80);
    for (int col = TorrentModel::ColRatio; col < TorrentModel::ColumnCount; ++col)
        m_view->header()->hideSection(col);
    // Единый делегат: фон строки, статус-иконка, статус-точка, прогресс, скорости.
    m_view->setItemDelegate(new TorrentDelegate(this));

    // Левая панель (референс — lvFilter + edSearch): группы статусов,
    // группы папок/трекеров, поиск.
    m_filterPanel = new FilterPanel(this);
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &MainWindow::onFilterChanged);
    connect(m_filterPanel->searchEdit(), &QLineEdit::textChanged, this,
            [this](const QString& text) { m_proxy.setSearchText(text.trimmed()); });
    const bool folderGrouping = QSettings().value("ui/folderGrouping", true).toBool();
    const bool trackerGrouping = QSettings().value("ui/trackerGrouping", true).toBool();
    m_filterPanel->setFolderGroupingEnabled(folderGrouping);
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
        view->verticalHeader()->setDefaultSectionSize(24);
        view->horizontalHeader()->setStretchLastSection(true);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->horizontalHeader()->setHighlightSections(false);
        return view;
    };

    auto* filesView = makeTable(&m_fileModel);
    filesView->setColumnWidth(FileListModel::ColName, 420);
    filesView->setColumnWidth(FileListModel::ColSize, 100);
    filesView->setColumnWidth(FileListModel::ColProgress, 90);
    filesView->setColumnWidth(FileListModel::ColPriority, 90);
    filesView->setColumnWidth(FileListModel::ColWanted, 70);
    connect(&m_fileModel, &FileListModel::wantChanged, this, &MainWindow::onFileWantToggled);

    auto* peersView = makeTable(&m_peerModel);
    peersView->setColumnWidth(PeerListModel::ColAddress, 190);
    peersView->setColumnWidth(PeerListModel::ColClient, 200);
    peersView->setColumnWidth(PeerListModel::ColProgress, 80);
    peersView->setColumnWidth(PeerListModel::ColRateDown, 110);

    auto* trackersView = makeTable(&m_trackerModel);
    trackersView->setColumnWidth(TrackerListModel::ColAnnounce, 340);
    trackersView->setColumnWidth(TrackerListModel::ColTier, 50);
    trackersView->setColumnWidth(TrackerListModel::ColSeeders, 70);
    trackersView->setColumnWidth(TrackerListModel::ColLeechers, 70);

    // Нижний уровень: вкладки выбранного торрента.
    m_general = new PropsForm(this);
    m_statsForm = new StatsForm(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setIconSize(QSize(16, 16));
    m_tabs->setUsesScrollButtons(true);
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
    m_tabs->setMinimumHeight(190);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(4);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(topPanel);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({540, 240});
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

    m_actDisconnect = new QAction(tr("Disconnect"), this);
    m_actDisconnect->setIcon(tgui::theme::icon(Icon::Disconnect));
    m_actDisconnect->setToolTip(tr("Disconnect from daemon"));
    connect(m_actDisconnect, &QAction::triggered, this, &MainWindow::onDisconnect);

    m_actAdd = new QAction(tr("Add torrent…"), this);
    m_actAdd->setShortcut(QKeySequence::Open);
    m_actAdd->setIcon(tgui::theme::icon(Icon::Add));
    m_actAdd->setToolTip(tr("Add torrent (Ctrl+O)"));
    m_actAdd->setEnabled(false);
    connect(m_actAdd, &QAction::triggered, this, &MainWindow::onAddTorrent);

    m_actStart = new QAction(tr("Start"), this);
    m_actStart->setIcon(tgui::theme::icon(Icon::Start));
    m_actStart->setToolTip(tr("Start selected torrents"));
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStartSelected);

    m_actStop = new QAction(tr("Stop"), this);
    m_actStop->setIcon(tgui::theme::icon(Icon::Stop));
    m_actStop->setToolTip(tr("Stop selected torrents"));
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStopSelected);

    m_actStartAll = new QAction(tr("Start all"), this);
    m_actStartAll->setIcon(tgui::theme::icon(Icon::StartAll));
    m_actStartAll->setToolTip(tr("Start all torrents"));
    connect(m_actStartAll, &QAction::triggered, this, &MainWindow::onStartAll);

    m_actStopAll = new QAction(tr("Stop all"), this);
    m_actStopAll->setIcon(tgui::theme::icon(Icon::StopAll));
    m_actStopAll->setToolTip(tr("Stop all torrents"));
    connect(m_actStopAll, &QAction::triggered, this, &MainWindow::onStopAll);

    m_actRemove = new QAction(tr("Remove"), this);
    m_actRemove->setShortcut(QKeySequence::Delete);
    m_actRemove->setIcon(tgui::theme::icon(Icon::Remove));
    m_actRemove->setToolTip(tr("Remove selected torrents (Del)"));
    connect(m_actRemove, &QAction::triggered, this, &MainWindow::onRemoveSelected);

    m_actProperties = new QAction(tr("Properties…"), this);
    m_actProperties->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    m_actProperties->setIcon(tgui::theme::icon(Icon::Properties));
    m_actProperties->setToolTip(tr("Torrent properties (Ctrl+I)"));
    m_actProperties->setEnabled(false);
    connect(m_actProperties, &QAction::triggered, this, &MainWindow::onShowProperties);

    m_actMove = new QAction(tr("Move…"), this);
    m_actMove->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_M));
    m_actMove->setIcon(tgui::theme::icon(Icon::Move));
    m_actMove->setToolTip(tr("Move torrent data (Ctrl+M)"));
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

    m_actAltSpeed = new QAction(tr("Alt speed limit"), this);
    m_actAltSpeed->setCheckable(true);
    m_actAltSpeed->setChecked(QSettings().value("ui/altSpeed", false).toBool());
    m_actAltSpeed->setIcon(tgui::theme::icon(Icon::AltSpeed));
    m_actAltSpeed->setToolTip(tr("Toggle the daemon's alternative speed limit"));
    connect(m_actAltSpeed, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("ui/altSpeed", on);
        if (m_rpc && m_connected)
            m_rpc->setAltSpeed(on, 50, 50);
    });

    m_actAbout = new QAction(tr("About"), this);
    m_actAbout->setIcon(tgui::theme::icon(Icon::About));
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);

    auto* tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setAllowedAreas(Qt::TopToolBarArea);
    tb->setIconSize(QSize(20, 20));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->addAction(m_actConnect);
    tb->addAction(m_actDisconnect);
    tb->addSeparator();
    tb->addAction(m_actAdd);
    tb->addSeparator();
    tb->addAction(m_actStart);
    tb->addAction(m_actStop);
    tb->addAction(m_actStartAll);
    tb->addAction(m_actStopAll);
    tb->addAction(m_actRemove);
    tb->addSeparator();
    tb->addAction(m_actProperties);
    tb->addAction(m_actMove);
    tb->addSeparator();
    tb->addAction(m_actRefresh);
    tb->addAction(m_actAutoRefresh);
    tb->addAction(m_actAltSpeed);
}

void MainWindow::setConnectedUi(bool on)
{
    m_filterPanel->setEnabledUi(on);
    m_actConnect->setEnabled(!on);
    m_actDisconnect->setEnabled(on);
    m_actAdd->setEnabled(on);
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
}

void MainWindow::onConnect()
{
    tgui::RpcClient::Options opts;
    loadSettings(opts);

    ConnectDialog dlg(opts, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    opts = dlg.options();
    if (opts.url.isEmpty()) {
        QMessageBox::warning(this, tr("Connection"), tr("Enter the daemon address."));
        return;
    }
    saveSettings(opts);
    startConnect(opts);
}

void MainWindow::startConnect(const tgui::RpcClient::Options& opts)
{
    m_rpc = std::make_unique<tgui::RpcClient>(opts, this);
    connect(m_rpc.get(), &tgui::RpcClient::connected, this, &MainWindow::onConnected);
    connect(m_rpc.get(), &tgui::RpcClient::connectionFailed, this,
            &MainWindow::onConnectionFailed);
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
    if (m_autoConnectOpts.url.isEmpty())
        return;
    saveSettings(m_autoConnectOpts);
    startConnect(m_autoConnectOpts);
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

void MainWindow::onRefresh()
{
    if (m_rpc && m_connected) {
        m_rpc->getTorrents();
        m_rpc->getSessionStats();
    }
}

void MainWindow::onTorrentsReceived(const QVector<tgui::Torrent>& torrents)
{
    // setTorrents() (modelReset) сбрасывает выделение и текущий индекс вью,
    // а вью прокручивается к началу. Запоминаем предыдущее состояние ДО
    // сброса, чтобы после восстановления не «сбрасывалось» ни выделение,
    // ни позиция прокрутки, ни вкладки деталей при каждом автообновлении
    // (раз в 2 секунды).
    const int prevId = m_currentId;
    const int prevScroll = m_view->verticalScrollBar()->value();

    // modelReset синхронно шлёт selectionChanged/currentChanged — не даём
    // onSelectionChanged сбросить текущий торрент посреди обновления.
    m_refreshing = true;
    m_model.setTorrents(torrents);
    m_refreshing = false;

    updateFilterPanel(torrents);
    // Панель «General» подтягивается к актуальному состоянию выбранного.
    if (prevId >= 0) {
        for (const auto& t : torrents) {
            if (t.id == prevId) {
                m_general->setTorrent(t);
                if (m_sessionInfo.rpcVersion > 0)
                    m_general->setSession(m_sessionInfo);
                break;
            }
        }
    }
    updateStatusBarTotals();
    const int count = static_cast<int>(torrents.size());
    m_countLabel->setText(count == 1 ? tr("1 torrent") : tr("%1 torrents").arg(count));

    if (!m_view->selectionModel())
        return;

    // Восстанавливаем выделение и позицию прокрутки выбранного торрента.
    if (prevId >= 0) {
        int srcRow = -1;
        const auto& list = m_model.torrents();
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).id == prevId) {
                srcRow = i;
                break;
            }
        }
        if (srcRow >= 0) {
            const QModelIndex src = m_model.index(srcRow, TorrentModel::ColName);
            const QModelIndex proxyIdx = m_proxy.mapFromSource(src);
            if (proxyIdx.isValid()) {
                // select() шлёт onSelectionChanged: id == m_currentId → no-op
                // (без очистки «General» и лишних RPC-запросов).
                m_view->selectionModel()->select(
                    QItemSelection(proxyIdx, proxyIdx),
                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                // Refresh the scrollbar range before restoring its offset.
                m_view->doItemsLayout();
                m_view->verticalScrollBar()->setValue(prevScroll);
                return;
            }
        }
        // Торрент исчез (удалён) либо скрыт фильтром — снимаем выделение
        // и очищаем вкладки деталей.
        m_currentId = -1;
        m_general->clear();
        m_view->doItemsLayout();
        m_view->verticalScrollBar()->setValue(prevScroll);
        return;
    }

    // Первая загрузка: выбираем первую строку, как в эталоне, — иначе вкладка
    // General остаётся пустой до ручного клика.
    if (m_proxy.rowCount() > 0) {
        const QModelIndex first = m_proxy.index(0, TorrentModel::ColName);
        m_view->selectionModel()->select(
            QItemSelection(first, first),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    m_view->doItemsLayout();
    m_view->verticalScrollBar()->setValue(prevScroll);
}

void MainWindow::onFilterChanged(int group, const QString& folder, const QString& tracker)
{
    m_proxy.setStatusGroup(group);
    m_proxy.setFolderFilter(folder);
    m_proxy.setTrackerFilter(tracker);
}

// Счётчики групп и состав списков папок/трекеров (референс — FillTorrentsList).
void MainWindow::updateFilterPanel(const QVector<tgui::Torrent>& torrents)
{
    int downloading = 0, completed = 0, active = 0, stopped = 0, error = 0, waiting = 0;
    QHash<QString, int> folders;
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
                             toPairs(folders), toPairs(trackers));
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
    const auto indexes = m_view->selectionModel()->selectedIndexes();
    for (const QModelIndex& vi : indexes) {
        const QModelIndex fi = m_proxy.mapToSource(vi);
        if (!fi.isValid())
            continue;
        const QModelIndex src = m_proxy.mapToSource(fi);
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
            dlg.exec();
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

    QJsonObject params;
    params.insert("location", dlg.targetDir());
    if (dlg.renameFiles())
        params.insert("rename-partially", true);
    m_rpc->setTorrent(ids.first(), params);
    m_rpc->getTorrents();
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this, tr("About"),
        tr("<b>transmissionGUI</b> %1<br/>"
           "GUI client for the Transmission daemon (C++/Qt 6).<br/><br/>"
           "License: GPL v3<br/>"
           "Mail: <a href=\"mailto:don10077@mail.ru\">don10077@mail.ru</a>")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::onContextMenu(const QPoint& pos)
{
    Q_UNUSED(pos);
    auto* menu = new QMenu(this);
    auto* actStart = menu->addAction(tr("Start"));
    auto* actStop = menu->addAction(tr("Stop"));
    menu->addSeparator();
    auto* actRemove = menu->addAction(tr("Remove…"));
    menu->addSeparator();
    auto* actProperties = menu->addAction(tr("Properties…"));
    auto* actMove = menu->addAction(tr("Move…"));
    menu->addSeparator();
    auto* actRefresh = menu->addAction(tr("Refresh"));
    auto* actSelectAll = menu->addAction(tr("Select all"));

    const bool on = m_connected;
    actStart->setEnabled(on);
    actStop->setEnabled(on);
    actRemove->setEnabled(on);
    actProperties->setEnabled(on);
    actMove->setEnabled(on);
    actRefresh->setEnabled(on);

    const QPoint global = m_view->mapToGlobal(pos);
    QObject::connect(actSelectAll, &QAction::triggered, this, [this] { m_view->selectAll(); });
    QObject::connect(actStart, &QAction::triggered, this, [this] { onStartSelected(); });
    QObject::connect(actStop, &QAction::triggered, this, [this] { onStopSelected(); });
    QObject::connect(actRemove, &QAction::triggered, this, [this] { onRemoveSelected(); });
    QObject::connect(actProperties, &QAction::triggered, this, [this] { onShowProperties(); });
    QObject::connect(actMove, &QAction::triggered, this, [this] { onMoveSelected(); });
    QObject::connect(actRefresh, &QAction::triggered, this, [this] { onRefresh(); });
    menu->exec(global);
    menu->deleteLater();
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
    // --- Torrent ---
    auto* mTorrent = menuBar()->addMenu(tr("Torrent"));
    mTorrent->addAction(m_actConnect);
    mTorrent->addAction(m_actDisconnect);
    mTorrent->addSeparator();
    mTorrent->addAction(m_actAdd);
    mTorrent->addSeparator();
    mTorrent->addAction(m_actStart);
    mTorrent->addAction(m_actStop);
    mTorrent->addSeparator();
    mTorrent->addAction(m_actStartAll);
    mTorrent->addAction(m_actStopAll);
    mTorrent->addSeparator();

    auto* mPriority = mTorrent->addMenu(tr("Priority"));
    auto* priHigh = mPriority->addAction(tr("High"));
    auto* priNormal = mPriority->addAction(tr("Normal"));
    auto* priLow = mPriority->addAction(tr("Low"));
    connect(priHigh, &QAction::triggered, this, &MainWindow::onPriorityHigh);
    connect(priNormal, &QAction::triggered, this, &MainWindow::onPriorityNormal);
    connect(priLow, &QAction::triggered, this, &MainWindow::onPriorityLow);

    auto* mQueue = mTorrent->addMenu(tr("Queue"));
    auto* qTop = mQueue->addAction(tr("Move to top"));
    auto* qUp = mQueue->addAction(tr("Move up"));
    auto* qDown = mQueue->addAction(tr("Move down"));
    connect(qTop, &QAction::triggered, this, &MainWindow::onQueueTop);
    connect(qUp, &QAction::triggered, this, &MainWindow::onQueueUp);
    connect(qDown, &QAction::triggered, this, &MainWindow::onQueueDown);

    mTorrent->addSeparator();
    auto* actVerify = mTorrent->addAction(tr("Verify…"));
    auto* actReannounce = mTorrent->addAction(tr("Update trackers"));
    connect(actVerify, &QAction::triggered, this, &MainWindow::onVerifySelected);
    connect(actReannounce, &QAction::triggered, this, &MainWindow::onReannounceSelected);
    mTorrent->addSeparator();
    mTorrent->addAction(m_actProperties);
    mTorrent->addAction(m_actMove);
    auto* actCopyPath = mTorrent->addAction(tr("Copy path"));
    connect(actCopyPath, &QAction::triggered, this, &MainWindow::onCopyPath);
    mTorrent->addSeparator();
    auto* actSelectAll = mTorrent->addAction(tr("Select all"));
    connect(actSelectAll, &QAction::triggered, this, &MainWindow::onSelectAll);
    mTorrent->addAction(m_actRefresh);

    // --- View ---
    auto* mView = menuBar()->addMenu(tr("View"));
    auto* actFolders = mView->addAction(tr("Group by folder"));
    actFolders->setCheckable(true);
    actFolders->setChecked(QSettings().value("ui/folderGrouping", true).toBool());
    auto* actTrackers = mView->addAction(tr("Group by tracker"));
    actTrackers->setCheckable(true);
    actTrackers->setChecked(QSettings().value("ui/trackerGrouping", true).toBool());
    connect(actFolders, &QAction::toggled, this, [this, actFolders](bool on) {
        m_filterPanel->setFolderGroupingEnabled(on);
        QSettings().setValue("ui/folderGrouping", on);
    });
    connect(actTrackers, &QAction::toggled, this, [this, actTrackers](bool on) {
        m_filterPanel->setTrackerGroupingEnabled(on);
        QSettings().setValue("ui/trackerGrouping", on);
    });
    mView->addSeparator();
    mView->addAction(m_actAutoRefresh);
    mView->addSeparator();
    auto* actStatusBar = mView->addAction(tr("Status bar"));
    actStatusBar->setCheckable(true);
    actStatusBar->setChecked(true);
    connect(actStatusBar, &QAction::toggled, this, [this, actStatusBar](bool on) {
        statusBar()->setVisible(on);
    });

    // --- Tools ---
    auto* mTools = menuBar()->addMenu(tr("Tools"));
    mTools->addAction(m_actAltSpeed);
    auto* actPortTest = mTools->addAction(tr("Test port"));
    connect(actPortTest, &QAction::triggered, this, &MainWindow::onPortTest);
    auto* actDaemonOpts = mTools->addAction(tr("Daemon options"));
    connect(actDaemonOpts, &QAction::triggered, this, &MainWindow::onDaemonOptions);

    // --- Help ---
    auto* mHelp = menuBar()->addMenu(tr("Help"));
    mHelp->addAction(m_actAbout);
}

void MainWindow::onSelectAll()
{
    m_view->selectAll();
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

void MainWindow::onDaemonOptions()
{
    if (!m_rpc || !m_connected)
        return;
    const auto& s = m_rpc->sessionInfo();
    const QString text = tr("<b>Transmission</b> %1 (RPC %2)<br/>")
                             .arg(s.version)
                             .arg(s.rpcVersion) +
         tr("Alt speed: %1 (down %2 KB/s, up %3 KB/s)<br/>")
             .arg(s.altSpeedEnabled ? tr("on") : tr("off"))
             .arg(s.altSpeedDown)
             .arg(s.altSpeedUp) +
         tr("Speed limit down: %1<br/>Speed limit up: %2<br/>")
             .arg(s.speedLimitDownEnabled ? QString::number(s.speedLimitDown) + " KB/s"
                                          : tr("Unlimited"))
             .arg(s.speedLimitUpEnabled ? QString::number(s.speedLimitUp) + " KB/s"
                                        : tr("Unlimited"));
    QMessageBox::information(this, tr("Daemon options"), text);
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
    if (id == m_currentId)
        return;
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
    if (id < 0 || !m_rpc)
        return;
    m_rpc->getTorrentDetails(id);
    m_rpc->getPeers(id);
    m_rpc->getTrackers(id);
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
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeTrayIcon());
    setWindowIcon(makeTrayIcon());
    m_tray->setToolTip(QStringLiteral("transmissionGUI"));

    auto* menu = new QMenu(this);
    auto* actShow = menu->addAction(tr("Show"));
    auto* actStartAll = menu->addAction(tr("Start all"));
    auto* actStopAll = menu->addAction(tr("Stop all"));
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
    // Закрытие = сворачивание в трей; выход — по пункту меню.
    if (m_tray && m_tray->isVisible()) {
        event->ignore();
        hide();
        return;
    }
    QMainWindow::closeEvent(event);
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
