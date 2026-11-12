#include "mainwindow.h"
#include "settingsbackup.h"
#include "externalopen.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QFrame>
#include <QFormLayout>
#include <QHash>
#include <QMenuBar>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QFileDialog>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QEventLoop>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <zlib.h>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
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
#include <QWidgetAction>
#include <QVersionNumber>
#include <algorithm>
#include <limits>
#include <utility>

#include "addlinkdialog.h"
#include "connectbutton.h"
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
#include "sequentialpolicy.h"
#include "openpath.h"
#include "pathmap.h"
#include "propsdialog.h"
#include "propsform.h"
#include "statsform.h"
#include "theme.h"
#include "torrentlabels.h"
#include "tgui/util.h"

namespace {
/** Идентификатор magnet для отсева повторов с другими dn/tr. */
QString magnetIdentifier(const QUrl& url)
{
    if (url.scheme() != QLatin1String("magnet"))
        return {};
    static const QRegularExpression identifier(
        QStringLiteral("^(urn:btih:(?:[a-f0-9]{40}|[a-z2-7]{32})|urn:btmh:1220[a-f0-9]{64})$"),
        QRegularExpression::CaseInsensitiveOption);
    for (const auto& value : QUrlQuery(url).allQueryItemValues("xt", QUrl::FullyDecoded))
        if (identifier.match(value).hasMatch())
            return value.toLower();
    return {};
}

/** Показывает поле в тулбаре и обычную команду поиска в меню переполнения. */
class ToolbarSearchAction : public QWidgetAction {
public:
    /** Привязывает уже созданное поле поиска.
     * @param edit Поле в панели инструментов.
     */
    explicit ToolbarSearchAction(QLineEdit* edit)
        : QWidgetAction(edit->parentWidget()), m_edit(edit)
    {}

protected:
    /** Возвращает поле только для тулбара. В меню используется текст команды.
     * @param parent Контейнер действия.
     */
    QWidget* createWidget(QWidget* parent) override
    {
        return m_edit && parent == m_edit->parentWidget() ? m_edit.data() : nullptr;
    }

private:
    QPointer<QLineEdit> m_edit;
};

constexpr int kRefreshIntervalMs = 2000;

// Распаковка gzip через zlib (windowBits=31 = gzip).
bool gunzipData(const QByteArray& input, QByteArray& output)
{
    z_stream strm{};
    if (inflateInit2(&strm, 31) != Z_OK)
        return false;
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    strm.avail_in = uInt(input.size());
    char buf[16384];
    int ret;
    do {
        strm.next_out = reinterpret_cast<Bytef*>(buf);
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            return false;
        }
        output.append(buf, sizeof(buf) - strm.avail_out);
    } while (strm.avail_out == 0 && ret != Z_STREAM_END);
    inflateEnd(&strm);
    return ret == Z_STREAM_END;
}
} // namespace

MainWindow::MainWindow(QWidget* parent, const tgui::RpcClient::Options& startupOptions)
    : QMainWindow(parent)
    , m_autoConnectOpts(startupOptions)
    , m_labels(torrentlabels::configured())
{
    setWindowTitle(QStringLiteral("transmissionGUI"));
    setWindowIcon(tgui::theme::icon(tgui::theme::Icon::App));
    resize(1200, 780);

    m_proxy.setSourceModel(&m_model);
    const QSettings preferences;
    m_totalsMode = qBound(0, preferences.value("ui/totalsMode", 3).toInt(), 3);
    createUi();
    createActions();
    createMenus();
    updateFilterPanel(m_model.torrents());
    createTray();

    // W11b: загрузить GeoIP-базу из appdata (если есть), привязать к модели пиров.
    {
        const QString geoipPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                  + QStringLiteral("/GeoIP.dat");
        if (QFileInfo::exists(geoipPath)) {
            m_geoip.load(geoipPath.toStdString());
        }
        m_peerModel.setGeoIp(&m_geoip);
        // W11: Resolve Host — состояние из настроек (при вкл. lookup стартует с загрузкой пиров).
        m_peerModel.setResolveHost(QSettings().value("peers/resolveHost", false).toBool());
        const bool showFlags = QSettings().value("peers/showFlag", false).toBool();
        if (showFlags)
            ensureFlagImages(false);
        m_peerModel.setShowFlags(showFlags);
    }

    // W07: интервал обновления из настроек (секунды), по умолчанию 2 (ранее 2000 ms).
    m_refreshTimer.setParent(this);
    m_refreshTimer.setObjectName(QStringLiteral("refreshTimer"));
    connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefresh);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_reconnectOptions)
            startConnect(*std::exchange(m_reconnectOptions, std::nullopt), true);
    });
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::onClipboardChanged);
    m_watchTimer.setInterval(1000);
    connect(&m_watchTimer, &QTimer::timeout, this, &MainWindow::scanWatchFolder);

    // После переноса демон может ответить раньше, чем обновится список.
    m_moveVerifyTimer.setSingleShot(true);
    m_moveVerifyTimer.setInterval(250);
    connect(&m_moveVerifyTimer, &QTimer::timeout, this, [this] {
        if (m_rpc && m_connected)
            m_rpc->getTorrents();
    });

    {
        QSettings s;
        const bool autoRefresh = s.value("ui/autoRefresh", true).toBool();
        if (m_actAutoRefresh->isChecked() != autoRefresh)
            m_actAutoRefresh->setChecked(autoRefresh);
    }

    setConnectedUi(false);
    const QSettings layout;
    restoreGeometry(layout.value("ui/windowGeometry").toByteArray());
    m_mainSplitter->restoreState(layout.value("ui/mainSplitter").toByteArray());
    m_model.setSmoothRates(layout.value("ui/smoothRates", false).toBool());
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::saveWindowLayout);
    statusBar()->showMessage(tr("Disconnected"), 5000);
    maybeAutoConnect();
}

void MainWindow::createUi()
{
    m_view = new QTreeView(this);
    m_view->setObjectName(QStringLiteral("torrentView"));
    m_view->setModel(&m_proxy);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(TorrentModel::ColName, Qt::AscendingOrder);
    m_view->setAlternatingRowColors(true);
    m_view->setRootIsDecorated(false);
    m_view->setIndentation(0);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setUniformRowHeights(true);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setMouseTracking(true);
    m_view->viewport()->setAttribute(Qt::WA_Hover);
    m_view->setWordWrap(false);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &MainWindow::onContextMenu);
    connect(m_view, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (index.isValid()) onTorrentsDoubleClicked();
    });
    m_view->header()->setObjectName(QStringLiteral("torrentHeader"));
    m_view->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->header()->setHighlightSections(false);
    m_view->header()->setMinimumSectionSize(30);
    // Сохранённые порядок, видимость и ширины имеют приоритет над макетом.
    ColumnLayout::fromSettings(TorrentModel::ColumnCount).applyToView(m_view);
    // W10: сохраняем ширину при ресайзе, чтобы она восстанавливалась при старте.
    connect(m_view->header(), &QHeaderView::sectionResized, this,
            [](int logical, int, int newWidth) {
                QSettings().setValue(QStringLiteral("columns/width_%1").arg(logical), newWidth);
            });
    // Единый делегат: выделение строки, статус, прогресс и метки.
    m_view->setItemDelegate(new TorrentDelegate(this));

    // Левая панель (референс — lvFilter + edSearch): группы статусов,
    // группы папок/трекеров, поиск.
    m_filterPanel = new FilterPanel(this);
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &MainWindow::onFilterChanged);
    const bool folderGrouping = QSettings().value("ui/folderGrouping", true).toBool();
    const bool trackerGrouping = QSettings().value("ui/trackerGrouping", true).toBool();
    m_filterPanel->setFolderGroupingEnabled(folderGrouping);
    m_filterPanel->setTrackerGroupingEnabled(trackerGrouping);
    m_filterPanel->setEnabledUi(false);

    // Разделитель между фильтрами и списком торрентов.
    auto* topPanel = new QSplitter(Qt::Horizontal, this);
    topPanel->setHandleWidth(4);
    topPanel->setChildrenCollapsible(false);
    topPanel->addWidget(m_filterPanel);
    topPanel->addWidget(m_view);
    topPanel->setStretchFactor(1, 1);
    topPanel->setSizes({215, width() - 215 - topPanel->handleWidth()});

    // Вкладки деталей выбранного торрента: Файлы / Пиры / Трекеры.
    auto makeTable = [this](QAbstractTableModel* model) {
        auto* view = new QTableView(this);
        view->setModel(model);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setMouseTracking(true);
        view->viewport()->setAttribute(Qt::WA_Hover);
        view->setSortingEnabled(false);
        view->setShowGrid(false);
        view->setWordWrap(false);
        view->setFrameShape(QFrame::NoFrame);
        view->verticalHeader()->setVisible(false);
        const int rowHeight = qMax(21, view->fontMetrics().height() + 5);
        view->verticalHeader()->setMinimumSectionSize(rowHeight);
        view->verticalHeader()->setDefaultSectionSize(rowHeight);
        view->horizontalHeader()->setStretchLastSection(false);
        view->horizontalHeader()->setSectionsMovable(true);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->horizontalHeader()->setHighlightSections(false);
        view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return view;
    };

    m_fileTree = new FileTreeModel(this);
    m_fileTree->setFiles({});
    auto* filesView = new QTreeView(this);
    filesView->setObjectName(QStringLiteral("filesView"));
    filesView->setModel(m_fileTree);
    filesView->setUniformRowHeights(true);
    filesView->setMouseTracking(true);
    filesView->viewport()->setAttribute(Qt::WA_Hover);
    filesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    filesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    filesView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    filesView->setFrameShape(QFrame::NoFrame);
    filesView->setIndentation(16);
    filesView->setIconSize(QSize(16, 16));
    filesView->setTreePosition(FileTreeModel::Name);
    filesView->header()->setStretchLastSection(false);
    filesView->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    filesView->header()->setSectionsMovable(true);
    filesView->setItemDelegateForColumn(FileTreeModel::Progress, new ProgressDelegate(filesView));
    filesView->setItemDelegateForColumn(FileTreeModel::Want, new WantCheckDelegate(filesView));
    m_filesView = filesView;
    filesView->setColumnWidth(FileTreeModel::Want, 36);
    filesView->setColumnWidth(FileTreeModel::Name, 350);
    filesView->setColumnWidth(FileTreeModel::Size, 90);
    filesView->setColumnWidth(FileTreeModel::Done, 90);
    filesView->setColumnWidth(FileTreeModel::Progress, 86);
    filesView->setColumnWidth(FileTreeModel::Priority, 100);
    // Восстановить сохранённую раскладку вкладки «Файлы» (порядок/видимость/ширины).
    const bool filesColumnsSaved =
        QSettings().contains(QStringLiteral("files/columns/order"));
    if (filesColumnsSaved)
        ColumnLayout::fromSettings(FileTreeModel::ColumnCount, QStringLiteral("files/columns"))
            .applyToView(filesView);
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
    connect(&m_fileModel, &QAbstractItemModel::modelReset, this, [this, filesColumnsSaved] {
        const bool first = m_fileTree->rowCount() == 0;
        const QByteArray header = m_filesView->header()->saveState();
        m_fileTree->setFiles(m_fileModel.files());
        m_filesView->header()->restoreState(header);
        if (first) {
            // Без сохранённой раскладки — ширины по умолчанию; сохранённые не перетираем.
            if (!filesColumnsSaved) {
                m_filesView->setColumnWidth(FileTreeModel::Want, 36);
                m_filesView->setColumnWidth(FileTreeModel::Name, 350);
                m_filesView->setColumnWidth(FileTreeModel::Size, 90);
                m_filesView->setColumnWidth(FileTreeModel::Done, 90);
                m_filesView->setColumnWidth(FileTreeModel::Progress, 86);
                m_filesView->setColumnWidth(FileTreeModel::Priority, 100);
            }
            m_filesView->expandToDepth(0);
        }
    });
    connect(&m_fileModel, &FileListModel::wantChanged, this, &MainWindow::onFileWantToggled);
    // Чекбокс Want: модель эмитит wantToggled(rows, wanted) → единый RPC.
    connect(m_fileTree, &FileTreeModel::wantToggled, this, &MainWindow::onFilesWantToggled);
    // Контекстное меню вкладки «Файлы» (референс — pmFiles): приоритет high/normal/low
    // и want/skip для выделенных строк.
    filesView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(filesView, &QWidget::customContextMenuRequested, this, &MainWindow::onFilesContextMenu);

    auto* peersView = makeTable(&m_peerModel);
    peersView->setObjectName(QStringLiteral("peersView"));
    peersView->setIconSize(QSize(18, 12));
    peersView->setItemDelegateForColumn(PeerListModel::ColProgress, new ProgressDelegate(peersView));
    m_peerEmpty = new QLabel(tr("No connected peers."), peersView->viewport());
    m_peerEmpty->setObjectName(QStringLiteral("peerEmpty"));
    m_peerEmpty->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_peerEmpty->move(8, 6);
    m_peerEmpty->adjustSize();
    m_peerEmpty->hide();
    connect(&m_peerModel, &QAbstractItemModel::modelReset, this, &MainWindow::updateInfoTitle);
    peersView->setColumnWidth(PeerListModel::ColAddress, 150);
    peersView->setColumnWidth(PeerListModel::ColCountry, 150);
    peersView->setColumnWidth(PeerListModel::ColClient, 150);
    peersView->setColumnWidth(PeerListModel::ColFlags, 70);
    peersView->setColumnWidth(PeerListModel::ColProgress, 86);
    peersView->setColumnWidth(PeerListModel::ColRateDown, 85);
    peersView->setColumnWidth(PeerListModel::ColRateUp, 85);
    peersView->hideColumn(PeerListModel::ColPort);
    peersView->horizontalHeader()->moveSection(
        peersView->horizontalHeader()->visualIndex(PeerListModel::ColCountry), 0);
    peersView->horizontalHeader()->moveSection(
        peersView->horizontalHeader()->visualIndex(PeerListModel::ColFlags), 2);
    // Контекстное меню вкладки «Пиры» (референс — pmPeers).
    peersView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(peersView, &QWidget::customContextMenuRequested, this, &MainWindow::onPeersContextMenu);
    // W11: восстановить сохранённую раскладку колонок пиров (иначе — дефолт выше).
    if (QSettings().contains(QStringLiteral("peers/columns/order")))
        ColumnLayout::fromSettings(PeerListModel::ColumnCount, QStringLiteral("peers/columns"))
            .applyToView(peersView);
    m_peersView = peersView; // для периодического обновления вкладки (см. refreshFilesDetails)

    auto* trackersView = makeTable(&m_trackerModel);
    m_trackersView = trackersView;
    trackersView->setObjectName(QStringLiteral("trackersView"));
    trackersView->setColumnWidth(TrackerListModel::ColAnnounce, 250);
    trackersView->setColumnWidth(TrackerListModel::ColTier, 64);
    trackersView->setColumnWidth(TrackerListModel::ColLastResult, 240);
    trackersView->setColumnWidth(TrackerListModel::ColUpdate, 90);
    trackersView->setColumnWidth(TrackerListModel::ColSeeders, 70);
    trackersView->setColumnWidth(TrackerListModel::ColLeechers, 70);
    trackersView->horizontalHeader()->moveSection(
        trackersView->horizontalHeader()->visualIndex(TrackerListModel::ColLastResult), 2);
    trackersView->horizontalHeader()->moveSection(
        trackersView->horizontalHeader()->visualIndex(TrackerListModel::ColUpdate), 3);
    // Восстановить сохранённую раскладку вкладки «Трекеры».
    if (QSettings().contains(QStringLiteral("trackers/columns/order")))
        ColumnLayout::fromSettings(TrackerListModel::ColumnCount, QStringLiteral("trackers/columns"))
            .applyToView(trackersView);
    // Контекстное меню вкладки «Трекеры» (референс — pmTrackers): Add/Edit/Remove.
    trackersView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(trackersView, &QWidget::customContextMenuRequested,
            this, &MainWindow::onTrackersContextMenu);
    connect(trackersView, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const auto& trackers = m_trackerModel.trackers();
        if (index.isValid() && index.row() < trackers.size()) {
            const auto tracker = trackers.at(index.row());
            onEditTracker(tracker.id, tracker.announce);
        }
    });
    trackersView->installEventFilter(this);

    // Нижний уровень: вкладки выбранного торрента.
    m_general = new PropsForm(this);
    m_statsForm = new StatsForm(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("infoPanel"));
    m_tabs->setIconSize(QSize(14, 14));
    m_tabs->setUsesScrollButtons(true);
    m_infoTitle = new QLabel(m_tabs);
    m_infoTitle->setObjectName(QStringLiteral("infoTorrentTitle"));
    m_infoTitle->setTextFormat(Qt::PlainText);
    m_tabs->setCornerWidget(m_infoTitle, Qt::TopRightCorner);
    m_tabs->installEventFilter(this);
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
    // При переключении на «Файлы» проценты могли устареть (проверка или
    // загрузка шли, пока открыта другая вкладка) — запрашиваем детали сразу.
    connect(m_tabs, &QTabWidget::currentChanged, this,
            [this](int) { refreshFilesDetails(); });

    auto* splitter = new QSplitter(Qt::Vertical, this);
    m_mainSplitter = splitter;
    splitter->setHandleWidth(4);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(topPanel);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({qMax(60, height() - 284), 280});
    setCentralWidget(splitter);

    if (m_view->selectionModel()) {
        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &MainWindow::onSelectionChanged);
        // setCurrentIndex() (в т.ч. программный и навигация клавишами) меняет
        // current без selectionChanged — обновляем детали и по этому сигналу.
        connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex& current, const QModelIndex& previous) {
                    if (current.row() != previous.row()) {
                        auto* viewport = m_view->viewport();
                        for (const auto& index : {current, previous}) {
                            const QRect cell = m_view->visualRect(index);
                            if (cell.isValid())
                                viewport->update(QRect(0, cell.top(), viewport->width(), cell.height()));
                        }
                    }
                    onSelectionChanged();
                });
    }

    auto* bar = statusBar();
    m_statusStrip = new QWidget(bar);
    m_statusStrip->setObjectName(QStringLiteral("statusStrip"));
    m_statusStrip->setAttribute(Qt::WA_StyledBackground);
    m_statusStrip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* strip = new QHBoxLayout(m_statusStrip);
    strip->setSizeConstraint(QLayout::SetNoConstraint);
    strip->setContentsMargins(0, 0, 0, 0);
    strip->setSpacing(0);

    const auto addGroup = [&] {
        auto* group = new QWidget(m_statusStrip);
        group->setObjectName(QStringLiteral("statusSegmentGroup"));
        group->setAttribute(Qt::WA_StyledBackground);
        auto* layout = new QHBoxLayout(group);
        layout->setContentsMargins(10, 0, 11, 0);
        layout->setSpacing(6);
        strip->addWidget(group);
        return qMakePair(group, layout);
    };

    const auto [connection, connectionLayout] = addGroup();
    m_statusConnection = connection;
    m_statusDot = new QLabel(connection);
    m_statusDot->setFixedSize(8, 8);
    connectionLayout->addWidget(m_statusDot);
    m_statusText = new QLabel(connection);
    m_statusText->setTextFormat(Qt::PlainText);
    m_statusText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connectionLayout->addWidget(m_statusText, 1);

    const auto addRate = [&](bool download) {
        auto* button = new QToolButton(m_statusStrip);
        button->setObjectName(QStringLiteral("statusRate"));
        button->setAutoRaise(true);
        button->setIconSize(QSize(14, 14));
        button->setIcon(tgui::theme::icon(download ? tgui::theme::Icon::StatusDown
                                                  : tgui::theme::Icon::StatusSeed));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setPopupMode(QToolButton::InstantPopup);
        auto* menu = createSpeedLimitMenu(button, download);
        menu->setObjectName(download ? QStringLiteral("statusDownloadSpeed") : QStringLiteral("statusUploadSpeed"));
        button->setMenu(menu);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, button, &QToolButton::showMenu);
        strip->addWidget(button);
        return button;
    };
    m_statusDownload = addRate(true);
    m_statusUpload = addRate(false);

    const auto [alternative, alternativeLayout] = addGroup();
    m_statusAlternative = alternative;
    auto* alternativeIcon = new QLabel(alternative);
    alternativeIcon->setPixmap(tgui::theme::icon(tgui::theme::Icon::AltSpeed)
                                  .pixmap(QSize(14, 14), bar->devicePixelRatioF()));
    alternativeLayout->addWidget(alternativeIcon);
    alternativeLayout->addWidget(new QLabel(tr("Alt speeds"), alternative));

    const auto addLabel = [&] {
        auto* label = new QLabel(m_statusStrip);
        label->setObjectName(QStringLiteral("statusSegment"));
        label->setTextFormat(Qt::PlainText);
        strip->addWidget(label);
        return label;
    };
    m_statusFree = addLabel();
    m_statusSize = addLabel();
    m_statusSelectedSize = addLabel();
    m_statusDone = addLabel();
    m_statusLeft = addLabel();
    strip->addStretch(1);
    m_countLabel = addLabel();

    bar->setSizeGripEnabled(true);
    bar->addPermanentWidget(m_statusStrip, 1);
    connect(bar, &QStatusBar::messageChanged, this, &MainWindow::updateStatusBarPresentation);
    m_statusStrip->installEventFilter(this);
    setConnectionState(tr("Disconnected"), tgui::theme::palette().statusOfflineDot);
}

void MainWindow::setConnectionState(const QString& text, const QColor& color)
{
    m_connectionStateText = text;
    if (m_statusDot)
        m_statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(color.name()));
    updateConnectionButton();
    updateStatusBarPresentation();
}

/** Обновляет подпись профиля и адрес или состояние подключения. */
void MainWindow::updateConnectionButton()
{
    if (!m_connectButton)
        return;

    if (!m_connected) {
        m_connectButton->setConnection(tr("Connect"), m_connectionStateText, false);
        return;
    }

    const QUrl url = m_rpc ? m_rpc->options().url : QUrl{};
    const QString title = m_connectionProfile.isEmpty() ? url.host() : m_connectionProfile;
    const QString address = url.adjusted(QUrl::RemoveUserInfo).authority();
    m_connectButton->setConnection(title.isEmpty() ? tr("Connected") : title,
                                   address.isEmpty() ? tr("Connected") : address, true);
}

void MainWindow::createActions()
{
    using tgui::theme::Icon;

    m_actConnect = new QAction(tr("Connect"), this);
    m_actConnect->setIcon(tgui::theme::icon(Icon::Connect));
    m_actConnect->setToolTip(tr("Connect to daemon (Ctrl+K)"));
    connect(m_actConnect, &QAction::triggered, this, &MainWindow::onConnect);

    m_actDisconnect = new QAction(tr("Disconnect from Transmission"), this);
    m_actDisconnect->setObjectName(QStringLiteral("disconnectAction"));
    m_actDisconnect->setToolTip(tr("Disconnect from Transmission"));
    connect(m_actDisconnect, &QAction::triggered, this, &MainWindow::onDisconnect);

    // Сочетания — как в transgui/main.lfm (Insert, Ctrl+Insert, F3/F4, F7, F8, F9,
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
    m_actStartAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F3));
    m_actStartAll->setToolTip(tr("Start all torrents"));
    connect(m_actStartAll, &QAction::triggered, this, &MainWindow::onStartAll);

    m_actStopAll = new QAction(tr("Pause all torrents"), this);
    m_actStopAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F4));
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
    m_actAutoRefresh->setObjectName(QStringLiteral("autoRefreshAction"));
    m_actAutoRefresh->setCheckable(true);
    m_actAutoRefresh->setChecked(true);
    m_actAutoRefresh->setToolTip(tr("Automatically refresh the torrent list"));
    connect(m_actAutoRefresh, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("ui/autoRefresh", on);
        updateRefreshSchedule();
    });

    m_actAltSpeed = new QAction(tr("Use alternate bandwidth settings"), this);
    m_actAltSpeed->setShortcut(QKeySequence(Qt::Key_F8));
    m_actAltSpeed->setCheckable(true);
    m_actAltSpeed->setChecked(QSettings().value("ui/altSpeed", false).toBool());
    m_actAltSpeed->setIcon(tgui::theme::icon(Icon::AltSpeed));
    m_actAltSpeed->setToolTip(tr("Toggle the daemon's alternative speed limit (F8)"));
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

    // Панель инструментов по design/mockup.html.
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setAllowedAreas(Qt::TopToolBarArea);
    m_toolBar->setIconSize(QSize(22, 22));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    const auto addToolbarButton = [this](QAction* action) {
        m_toolBar->addAction(action);
        auto* button = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(action));
        button->setProperty("toolbarAction", true);
        button->setFocusPolicy(Qt::StrongFocus);
        return button;
    };
    m_actAdd->setIconText(tr("Add"));
    m_actAddLink->setIconText(tr("Link"));
    m_actStart->setIconText(tr("Start"));
    m_actStop->setIconText(tr("Pause"));
    m_actRemove->setIconText(tr("Remove"));
    m_actMove->setIconText(tr("Move"));
    m_actAltSpeed->setIconText(tr("Limit"));

    m_actDaemonOptions->setIconText(QStringLiteral("Transmission"));
    m_actOptions->setIconText(tr("Options"));

    m_connectButton = new ConnectButton(m_toolBar);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    m_connectButton->setToolTip(tr("Connection profiles (Ctrl+K)"));
    updateConnectionButton();
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
    auto* profilesShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(profilesShortcut, &QShortcut::activated, this, [this, profilesMenu] {
        if (m_connectButton->isVisible())
            m_connectButton->showMenu();
        else
            profilesMenu->popup(mapToGlobal(QPoint(0, menuBar()->height())));
    });
    m_toolBar->addWidget(m_connectButton);
    m_toolBar->addSeparator();

    addToolbarButton(m_actAdd);
    addToolbarButton(m_actAddLink);
    m_toolBar->addSeparator();
    addToolbarButton(m_actStart);
    addToolbarButton(m_actStop);
    addToolbarButton(m_actRemove);
    m_toolBar->addSeparator();

    m_actQueue = new QAction(tgui::theme::icon(Icon::Queue), tr("Queue"), this);
    m_actQueue->setToolTip(tr("Change queue position"));
    m_actQueue->setObjectName(QStringLiteral("queueAction"));
    auto* queueMenu = new QMenu(m_toolBar);
    auto* first = queueMenu->addAction(tr("Move top"));
    auto* up = queueMenu->addAction(tgui::theme::icon(Icon::QueueUp), tr("Move up"));
    auto* down = queueMenu->addAction(tgui::theme::icon(Icon::QueueDown), tr("Move down"));
    auto* last = queueMenu->addAction(tr("Move bottom"));
    connect(first, &QAction::triggered, this, &MainWindow::onQueueTop);
    connect(up, &QAction::triggered, this, &MainWindow::onQueueUp);
    connect(down, &QAction::triggered, this, &MainWindow::onQueueDown);
    connect(last, &QAction::triggered, this, &MainWindow::onQueueBottom);
    m_actQueue->setMenu(queueMenu);
    auto* queueButton = addToolbarButton(m_actQueue);
    queueButton->setObjectName(QStringLiteral("queueButton"));
    queueButton->setPopupMode(QToolButton::InstantPopup);
    addToolbarButton(m_actMove);
    addToolbarButton(m_actAltSpeed);
    m_toolBar->addSeparator();
    addToolbarButton(m_actDaemonOptions);
    addToolbarButton(m_actOptions);

    // Поиск — в тулбаре, как edSearch в transgui.
    auto* spacer = new QWidget(m_toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);
    m_searchEdit = new QLineEdit(m_toolBar);
    m_searchEdit->setObjectName(QStringLiteral("searchEdit"));
    const QKeySequence searchShortcut(QKeySequence::Find);
    const QString searchHint = searchShortcut.toString(QKeySequence::NativeText);
    m_searchEdit->setPlaceholderText(tr("Search torrents") + QStringLiteral("  ") + searchHint);
    m_searchEdit->setAccessibleName(tr("Search torrents"));
    m_searchEdit->setToolTip(m_searchEdit->placeholderText());
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->addAction(tgui::theme::icon(Icon::Search), QLineEdit::LeadingPosition);
    m_searchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_searchEdit->setMinimumWidth(120);
    m_searchEdit->setMaximumWidth(250);
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                m_proxy.setSearchText(text.trimmed());
                updateStatusBarTotals();
            });
    auto* find = new ToolbarSearchAction(m_searchEdit);
    find->setText(tr("Search torrents"));
    find->setIcon(tgui::theme::icon(Icon::Search));
    find->setShortcut(searchShortcut);
    m_toolBar->addAction(find);
    addAction(find);
    connect(find, &QAction::triggered, m_searchEdit, [this] {
        if (m_searchEdit->isVisible()) {
            m_searchEdit->setFocus();
            m_searchEdit->selectAll();
            return;
        }
        bool accepted = false;
        const QString text = QInputDialog::getText(this, tr("Search torrents"), tr("Search torrents"),
                                                   QLineEdit::Normal, m_searchEdit->text(), &accepted);
        if (accepted)
            m_searchEdit->setText(text);
    });
    auto applyStyle = [this](int value) {
        if (value < 0 || value > 2) value = 2;
        const Qt::ToolButtonStyle modes[]{Qt::ToolButtonIconOnly, Qt::ToolButtonTextBesideIcon, Qt::ToolButtonTextUnderIcon};
        m_toolBar->setToolButtonStyle(modes[value]);
        m_toolBar->setIconSize(value == 2 ? QSize(22, 22) : QSize(18, 18));
        if (value == 0) {
            m_toolBar->setStyleSheet(QStringLiteral(
                "QToolBar > QToolButton[toolbarAction=\"true\"] { min-width: 18px; padding: 5px; }"
                "QToolBar > QToolButton#queueButton { padding-right: 16px; }"));
        } else if (value == 1) {
            m_toolBar->setStyleSheet(QStringLiteral(
                "QToolBar > QToolButton[toolbarAction=\"true\"] { padding: 4px 7px; }"
                "QToolBar > QToolButton#queueButton { padding-right: 17px; }"));
        } else {
            m_toolBar->setStyleSheet(QString());
        }
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
        menu.addSeparator();
        auto* hide = menu.addAction(tr("Hide toolbar"));
        connect(hide, &QAction::triggered, this, [this] { m_actToolbar->setChecked(false); });
        menu.exec(m_toolBar->mapToGlobal(point));
    });

}

void MainWindow::setConnectedUi(bool on)
{
    m_filterPanel->setEnabledUi(on);
    m_actConnect->setEnabled(!on && !m_rpc);
    updateConnectionButton();
    m_actDisconnect->setEnabled(m_rpc || m_reconnectOptions.has_value());
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
    for (auto* menu : m_speedLimitMenus)
        menu->setEnabled(on);
    m_statusDownload->setEnabled(on);
    m_statusUpload->setEnabled(on);
    updateStatusBarTotals();
    updateSelectionActions();
}

// Действия над торрентами активны только при непустом выделении (как в
// transgui: acStartTorrent/acStopTorrent/… Enabled по SelectedCount).
void MainWindow::updateSelectionActions()
{
    const bool hasSel = m_connected && !selectedTorrentIds().isEmpty();
    updateInfoTitle();
    m_actStart->setEnabled(hasSel);
    m_actStop->setEnabled(hasSel);
    m_actRemove->setEnabled(hasSel);
    m_actProperties->setEnabled(hasSel);
    m_actMove->setEnabled(m_connected && selectedTorrentIds().size() == 1);
    if (m_actQueue) m_actQueue->setEnabled(hasSel);
    m_actChooseLabel->setEnabled(hasSel && m_rpc && m_rpc->supports(16));
}

/** Обновляет подпись выбранного торрента справа от вкладок. */
void MainWindow::updateInfoTitle()
{
    const auto* torrent = m_general ? m_general->torrent() : nullptr;
    if (m_peerEmpty)
        m_peerEmpty->setVisible(m_currentId >= 0 && m_peerModel.rowCount() == 0);
    if (!m_infoTitle)
        return;
    QString title = torrent ? torrent->name.value_or(QString()) : QString();
    if (!title.isEmpty()) {
        const qsizetype count = selectedTorrentIds().size();
        if (count > 1)
            title += tr(" (+%1 more)").arg(count - 1);
    }
    const int textWidth = qMax(0, qMin(360, m_tabs->width() * 45 / 100) - 16);
    m_infoTitle->setText(m_infoTitle->fontMetrics().elidedText(title, Qt::ElideRight, textWidth));
    m_infoTitle->setToolTip(title);
}

/** Синхронизирует пункты меню и правую панель со списком меток. */
void MainWindow::updateLabelUi()
{
    const bool configured = !m_labels.isEmpty();
    const bool hasLabelFilters = configured || m_hasUnlabelledTorrents;
    m_actChooseLabel->setVisible(configured);
    m_actLabelGrouping->setVisible(hasLabelFilters);
    m_filterPanel->setLabelGroupingEnabled(hasLabelFilters && m_actLabelGrouping->isChecked());
}

void MainWindow::onConnect()
{
    if (m_reconnectOptions) {
        startConnect(*std::exchange(m_reconnectOptions, std::nullopt));
        return;
    }
    if (m_rpc && !m_connected)
        return;
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

void MainWindow::startConnect(const tgui::RpcClient::Options& opts, bool retry)
{
    m_reconnectTimer.stop();
    m_reconnectOptions.reset();
    if (!retry)
        m_reconnectDelayMs = 1000;
    m_watchTimer.stop();
    m_watchCandidates.clear();
    m_addQueue.clear();
    m_addingTorrent = false;
    m_addSelectionId = -1;
    m_rpc = std::make_unique<tgui::RpcClient>(opts, this);
    connect(m_rpc.get(), &tgui::RpcClient::connected, this, &MainWindow::onConnected);
    connect(m_rpc.get(), &tgui::RpcClient::sessionInfoReceived, this,
            [this](const tgui::SessionInfo& info) {
        const auto free = info.downloadDir == m_sessionInfo.downloadDir
            ? m_sessionInfo.downloadDirFreeSpace : std::nullopt;
        m_sessionInfo = info;
        if (!m_sessionInfo.downloadDirFreeSpace)
            m_sessionInfo.downloadDirFreeSpace = free;
        const QSignalBlocker blocker(m_actAltSpeed);
        m_actAltSpeed->setChecked(info.altSpeedEnabled);
        if (m_general->torrent()) m_general->setSession(info);
        updateStatusBarTotals();
    });
    connect(m_rpc.get(), &tgui::RpcClient::connectionFailed, this,
            &MainWindow::onConnectionFailed);
    connect(m_rpc.get(), &tgui::RpcClient::connectionInterrupted, this,
            [this](const QString& error) { handleConnectionError(error, true); });
    connect(m_rpc.get(), &tgui::RpcClient::authenticationFailed, this,
            &MainWindow::onAuthenticationFailed);
    connect(m_rpc.get(), &tgui::RpcClient::clientTlsPassphraseRequired, this,
            &MainWindow::onClientTlsPassphraseRequired);
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

    statusBar()->showMessage(tr("Connecting to %1…").arg(opts.url.toDisplayString(QUrl::RemoveUserInfo)), 5000);
    setConnectedUi(false);
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
        m_connectionProfile.clear();
        saveSettings(opts);
        startConnect(opts);
    }
}

/** Открывает входные файлы и ссылки или ждёт подключения к демону. */
void MainWindow::openExternalTorrents(const QStringList& inputs)
{
    QStringList accepted;
    for (const auto& value : inputs) {
        const auto input = externalTorrentInput(value);
        if (!input.isEmpty() && !accepted.contains(input))
            accepted.append(input);
    }
    if (accepted.isEmpty())
        return;
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();

    if (!m_rpc || !m_connected) {
        for (const auto& input : accepted)
            if (!m_pendingExternalTorrents.contains(input))
                m_pendingExternalTorrents.append(input);
        statusBar()->showMessage(tr("Connect to a daemon to add the files or links you opened."), 8000);
        return;
    }
    const auto& session = m_rpc->sessionInfo();
    for (const auto& input : accepted) {
        tgui::RpcClient::AddTorrent params;
        if (input.startsWith("magnet:", Qt::CaseInsensitive)) {
            params.source = tgui::RpcClient::AddTorrent::Source::Url;
            params.url = input;
        } else {
            params.file = input;
        }
        params.downloadDir = session.downloadDir;
        params.start = session.startAddedTorrents;
        startAdd(params, AddOrigin::External);
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
        if (store.hasCurrent()) {
            m_connectionProfile = store.currentName();
            opts = store.profile(m_connectionProfile).toOptions();
        }
    }
    if (opts.url.isEmpty())
        return;
    saveSettings(opts);
    startConnect(opts);
}

void MainWindow::onDisconnect()
{
    m_connectionProfile.clear();
    m_reconnectTimer.stop();
    m_reconnectOptions.reset();
    m_reconnectDelayMs = 1000;
    clearConnection();
    setConnectionState(tr("Disconnected"), tgui::theme::palette().statusOfflineDot);
    statusBar()->showMessage(tr("Disconnected"), 5000);
}

/** Блокирует действия на старом соединении до завершения его обработчиков. */
void MainWindow::suspendConnection()
{
    m_refreshTimer.stop();
    m_watchTimer.stop();
    m_moveVerifyTimer.stop();
    m_connected = false;
    setConnectedUi(false);
}

/** Удаляет клиент и данные, привязанные к его сессии. */
void MainWindow::clearConnection()
{
    suspendConnection();
    m_watchCandidates.clear();
    m_movePendingIds.clear();
    m_movePendingTarget.clear();
    m_moveVerifyAttempts = 0;
    m_addQueue.clear();
    m_addingTorrent = false;
    m_addSelectionId = -1;
    m_rpc.reset();
    m_model.setTorrents({});
    updateFilterPanel(m_model.torrents());
    m_fileModel.setFiles({});
    m_peerModel.setPeers({});
    m_trackerModel.setTrackers({});
    m_general->clear();
    m_statsForm->clear();
    m_currentId = -1;
    setConnectedUi(false);
}

void MainWindow::onConnected(const tgui::SessionInfo& info)
{
    m_reconnectDelayMs = 1000;
    m_connected = true;
    m_sessionInfo = info;
    {
        const QSignalBlocker blocker(m_actAltSpeed);
        m_actAltSpeed->setChecked(info.altSpeedEnabled);
    }
    if (m_general->torrent())
        m_general->setSession(info);
    setConnectionState(tr("Connected"), tgui::theme::color::green);
    setConnectedUi(true);
    updateRefreshSchedule();
    m_rpc->getTorrents();
    m_rpc->getSessionStats();
    updateWatchFolder();
    statusBar()->showMessage(tr("Connected"), 3000);
    openExternalTorrents(std::exchange(m_pendingExternalTorrents, {}));
}

void MainWindow::onConnectionFailed(const QString& error)
{
    handleConnectionError(error, false);
}

/** Планирует новую сессию после временного сбоя или показывает постоянную ошибку. */
void MainWindow::handleConnectionError(const QString& error, bool retry)
{
    suspendConnection();
    const QPointer<tgui::RpcClient> failed(m_rpc.get());
    QTimer::singleShot(0, this, [this, failed, error, retry] {
        if (!failed || failed != m_rpc.get())
            return;
        const auto opts = failed->options();
        clearConnection();
        if (retry) {
            m_reconnectOptions = opts;
            const int delay = m_reconnectDelayMs;
            m_reconnectDelayMs = std::min(delay * 2, 60000);
            m_reconnectTimer.start(delay);
            setConnectedUi(false);
            setConnectionState(tr("Reconnecting…"), tgui::theme::color::accent);
            statusBar()->showMessage(tr("Retrying in %1 s: %2").arg(delay / 1000).arg(error), delay);
            return;
        }
        m_reconnectDelayMs = 1000;
        setConnectionState(tr("Connection failed"), tgui::theme::color::red);
        statusBar()->showMessage(tr("Disconnected"), 5000);
        QMessageBox::critical(this, tr("Connection failed"),
                              tr("Address: %1\n\nReason: %2").arg(opts.url.toDisplayString(QUrl::RemoveUserInfo), error));
    });
}

void MainWindow::onAuthenticationFailed(const QString& error)
{
    Q_UNUSED(error);
    suspendConnection();
    setConnectionState(tr("Authentication required"), tgui::theme::color::red);
    // Откладываем: даём развернуться стеку сигнала, чтобы безопасно показать
    // диалог повторной авторизации и переподключиться (старый клиент уничтожится).
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    QTimer::singleShot(0, this, [this, rpc] {
        if (!rpc || rpc != m_rpc.get())
            return;
        auto* dialog = new ConnectDialog(rpc->options(), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(rpc.data(), &QObject::destroyed, dialog, &QDialog::reject);
        connect(dialog, &QDialog::finished, this, [this, rpc, dialog](int result) {
            if (!rpc || rpc != m_rpc.get())
                return;
            if (result != QDialog::Accepted || dialog->options().url.isEmpty()) {
                onDisconnect();
                return;
            }
            const auto opts = dialog->options();
            saveSettings(opts);
            startConnect(opts);
        });
        dialog->open();
    });
}

/** Запрашивает пароль файла ключа и повторяет подключение с тем же профилем. */
void MainWindow::onClientTlsPassphraseRequired()
{
    suspendConnection();
    setConnectionState(tr("Authentication required"), tgui::theme::color::red);
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    QTimer::singleShot(0, this, [this, rpc] {
        if (!rpc || rpc != m_rpc.get())
            return;
        auto* dialog = new QInputDialog(this);
        dialog->setWindowTitle(tr("Client TLS passphrase"));
        dialog->setLabelText(tr("Passphrase for the client certificate or key:"));
        dialog->setTextEchoMode(QLineEdit::Password);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(rpc.data(), &QObject::destroyed, dialog, &QDialog::reject);
        connect(dialog, &QDialog::finished, this, [this, rpc, dialog](int result) {
            if (!rpc || rpc != m_rpc.get())
                return;
            if (result != QDialog::Accepted) {
                onDisconnect();
                return;
            }
            auto opts = rpc->options();
            opts.clientKeyPassphrase = dialog->textValue().toUtf8();
            startConnect(opts);
        });
        dialog->open();
    });
}

void MainWindow::onRefresh()
{
    if (m_rpc && m_connected) {
        m_rpc->getTorrents();
        m_rpc->getSessionStats();
        refreshFilesDetails();
    }
}

/** Выбирает интервал по состоянию окна, сохраняя пользовательский режим автообновления. */
void MainWindow::updateRefreshSchedule()
{
    if (!m_actAutoRefresh || !m_connected || !m_actAutoRefresh->isChecked()) {
        m_refreshTimer.stop();
        return;
    }
    const bool background = !isVisible() || isMinimized();
    const auto key = background ? "ui/backgroundRefreshInterval" : "ui/refreshInterval";
    const int fallback = background ? 10 : kRefreshIntervalMs / 1000;
    const int interval = qBound(1, QSettings().value(key, fallback).toInt(), 999) * 1000;
    if (m_refreshTimer.interval() != interval)
        m_refreshTimer.setInterval(interval);
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
}

void MainWindow::refreshFilesDetails()
{
    // Прогресс файлов меняется между опросами (проверка торрента, загрузка),
    // поэтому видимую вкладку «Файлы» обновляем вместе со списком торрентов.
    // Иначе остаются проценты, полученные в момент выбора торрента: при
    // начатой проверке они так и висят на нуле.
    // «Пиры» — то же самое: во время скачивания набор подключённых пиров
    // постоянно меняется, а при выборе торрента пиров ещё могло не быть —
    // без периодического запроса вкладка остаётся пустой до переподключения
    // (старый transgui перезапрашивал детали каждый цикл обновления).
    if (!m_rpc || m_currentId < 0 || !m_tabs || m_tabs->isHidden())
        return;
    if (m_filesView && m_tabs->currentWidget() == m_filesView)
        m_rpc->getTorrentDetails(m_currentId);
    if (m_peersView && m_tabs->currentWidget() == m_peersView)
        m_rpc->getPeers(m_currentId);
    if (m_trackersView && m_tabs->currentWidget() == m_trackersView)
        m_rpc->getTrackers(m_currentId);
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

    // После добавления или обновления дубликата выбираем строку из подтверждённого списка.
    int addedRow = -1;
    if (m_addSelectionId >= 0 && std::any_of(torrents.cbegin(), torrents.cend(),
            [this](const auto& torrent) { return torrent.id == m_addSelectionId; })) {
        addedRow = proxyRowForId(m_addSelectionId);
        if (addedRow < 0) {
            m_filterPanel->selectAllTorrents();
            m_searchEdit->clear();
            addedRow = proxyRowForId(m_addSelectionId);
        }
    }

    int nextId = prevId;
    if (addedRow >= 0) {
        nextId = std::exchange(m_addSelectionId, -1);
        restoreTorrentSelection({nextId}, nextId);
    } else {
        restoreTorrentSelection(prevSelected, prevId);
    }
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

    if (nextId != prevId) {
        m_fileModel.setFiles({});
        m_peerModel.setPeers({});
        m_trackerModel.setTrackers({});
    }

    // Панель «General» подтягивается к актуальному состоянию выбранного.
    if (nextId >= 0) {
        for (const auto& t : torrents) {
            if (t.id == nextId) {
                const auto& currentFiles = m_fileModel.files();
                const int status = t.status.value_or(-1);
                if (!currentFiles.isEmpty() &&
                    (status == static_cast<int>(tgui::TorrentStatus::Check) ||
                     std::any_of(currentFiles.cbegin(), currentFiles.cend(),
                         [](const tgui::FileStat& file) { return file.checkProgress >= 0.0; }))) {
                    auto files = currentFiles;
                    tgui::applyCheckProgress(files, status, t.recheckProgress,
                                             t.pieceCount.value_or(0));
                    m_fileModel.setFiles(files);
                }
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
    m_view->doItemsLayout();
    if (addedRow >= 0)
        m_view->scrollTo(m_view->currentIndex());
    else
        m_view->verticalScrollBar()->setValue(prevScroll);
    updateSelectionActions();
    checkPendingMove();
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
                                 const QString& tracker, const QString& label, bool withoutLabels)
{
    m_proxy.setStatusGroup(group);
    m_proxy.setFolderFilter(folder);
    m_proxy.setLabelFilter(label, withoutLabels);
    m_proxy.setTrackerFilter(tracker);
    updateStatusBarTotals();
}

// Счётчики групп и состав списков папок/трекеров (референс — FillTorrentsList).
void MainWindow::updateFilterPanel(const QVector<tgui::Torrent>& torrents)
{
    int downloading = 0, completed = 0, active = 0, stopped = 0, error = 0, waiting = 0;
    int unlabelled = 0;
    QHash<QString, int> folders;
    QHash<QString, int> labels;
    for (const auto& label : m_labels)
        labels.insert(label, 0);
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
        if (!t.labels || t.labels->isEmpty()) {
            ++unlabelled;
        } else {
            for (const QString& label : *t.labels) {
                if (auto it = labels.find(label); it != labels.end())
                    ++it.value();
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
    auto labelRows = toPairs(labels);
    if (unlabelled > 0)
        labelRows.append({QString{}, unlabelled});
    m_hasUnlabelledTorrents = unlabelled > 0;
    updateLabelUi();
    m_filterPanel->setCounts(static_cast<int>(torrents.size()), downloading, completed,
                             active, inactive, stopped, error, waiting,
                             toPairs(folders), labelRows, toPairs(trackers));
}

void MainWindow::onReadError(const QString& what, const QString& error)
{
    statusBar()->showMessage(tr("%1: %2").arg(what, error), 8000);
}

void MainWindow::onRequestCompleted(const QString& method, bool ok, const QString& error,
                                    const QJsonObject& arguments)
{
    Q_UNUSED(arguments);
    if (ok && method == QLatin1String("torrent-set-location") && m_rpc)
        m_rpc->getTorrents();
    if (method == QLatin1String("session-set") && m_rpc && m_connected)
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
    if (ids.isEmpty()) {
        const auto current = m_proxy.mapToSource(m_view->currentIndex());
        if (current.isValid())
            ids.append(m_model.torrentAt(current.row()).id);
    }
    return ids;
}

/** Обновляет список и нужные детали после успешного ответа того же соединения. */
tgui::RpcClient::RequestCallback MainWindow::afterTorrentChange(const QString& method,
                                                                RefreshAfter refresh, int id,
                                                                const QString& successMessage)
{
    return [this, rpc = QPointer(m_rpc.get()), method, refresh, id, successMessage]
           (const tgui::JsonRpcResult& result) {
        if (!rpc || rpc != m_rpc.get())
            return;
        if (result.ok && !successMessage.isEmpty())
            statusBar()->showMessage(successMessage, 3000);
        else
            onRequestCompleted(method, result.ok, result.error, result.arguments);
        if (!result.ok)
            return;
        rpc->getTorrents();
        if (id != m_currentId)
            return;
        if (refresh == RefreshAfter::Files || refresh == RefreshAfter::Properties)
            rpc->getTorrentDetails(id);
        if (refresh == RefreshAfter::Trackers || refresh == RefreshAfter::Properties)
            rpc->getTrackers(id);
    };
}

/** Отправляет весь выбор одним RPC с обновлением списка по подтверждению. */
void MainWindow::runTorrentAction(const QString& method, const QVector<int>& ids,
                                  const QJsonObject& params)
{
    if (m_rpc && m_connected && !ids.isEmpty())
        m_rpc->torrentAction(method, ids, params, afterTorrentChange(method, RefreshAfter::List));
}

void MainWindow::onStartSelected()
{
    runTorrentAction("torrent-start", selectedTorrentIds());
}

void MainWindow::onStopSelected()
{
    runTorrentAction("torrent-stop", selectedTorrentIds());
}

void MainWindow::onStartAll()
{
    if (m_rpc)
        m_rpc->startAll(false, afterTorrentChange("torrent-start", RefreshAfter::List));
}

void MainWindow::onStopAll()
{
    if (m_rpc)
        m_rpc->stopAll(afterTorrentChange("torrent-stop", RefreshAfter::List));
}

void MainWindow::onRemoveSelected()
{
    if (!m_rpc)
        return;
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty())
        return;

    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    QMessageBox box(QMessageBox::Question, tr("Remove torrents"),
                 ids.size() == 1
                     ? tr("Remove the torrent from the daemon's list?")
                     : tr("Remove %1 torrents from the daemon's list?").arg(ids.size()),
                 QMessageBox::Cancel, this);
    box.setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Warning).pixmap(QSize(32, 32), devicePixelRatioF()));
    const auto* remove = box.addButton(tr("Remove"), QMessageBox::AcceptRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (rpc && box.clickedButton() == remove)
        runTorrentAction("torrent-remove", ids);
}

/** Считает скорости, объёмы и число выбранных торрентов для строки состояния. */
void MainWindow::updateStatusBarTotals()
{
    if (!m_statusStrip)
        return;

    struct Sizes {
        double total = 0.0;
        double done = 0.0;
        double left = 0.0;

        void add(const tgui::Torrent& torrent)
        {
            const double wanted = qMax(0.0, torrent.sizeWhenDone.value_or(torrent.totalSize.value_or(0.0)));
            const double remaining = qBound(0.0, torrent.leftUntilDone.value_or(wanted), wanted);
            total += torrent.totalSize.value_or(wanted);
            done += wanted - remaining;
            left += remaining;
        }
    };

    m_statusDownRate = 0.0;
    m_statusUpRate = 0.0;
    Sizes totals;
    for (const auto& torrent : m_model.torrents()) {
        const auto rates = m_model.displayedRates(torrent);
        m_statusDownRate += rates.first;
        m_statusUpRate += rates.second;
        if (m_totalsMode == 3)
            totals.add(torrent);
    }
    if (m_totalsMode == 1)
        for (int row = 0; row < m_proxy.rowCount(); ++row)
            totals.add(m_proxy.torrentAt(row));

    const auto selected = m_view->selectionModel()->selectedRows();
    Sizes selectedSizes;
    if (m_totalsMode != 0)
        for (const auto& row : selected)
            selectedSizes.add(m_proxy.torrentAt(row.row()));
    if (m_totalsMode == 2)
        totals = selectedSizes;

    const auto format = [](double bytes) { return QString::fromStdString(tgui::formatSize(bytes)); };
    const QString sizeCaption = m_totalsMode == 1 ? tr("Visible: %1")
        : m_totalsMode == 2 ? tr("Selected: %1") : tr("Total: %1");
    m_statusSize->setText(sizeCaption.arg(format(totals.total)));
    m_statusSelectedSize->setText(tr("Selected: %1").arg(format(selectedSizes.total)));
    m_statusDone->setText(tr("Done: %1").arg(format(totals.done)));
    m_statusLeft->setText(tr("Left: %1").arg(format(totals.left)));
    if (const auto free = m_sessionInfo.downloadDirFreeSpace; free && *free >= 0) {
        m_statusFree->setText(tr("Free: %1").arg(format(static_cast<double>(*free))));
    } else {
        m_statusFree->clear();
    }

    const int count = m_model.rowCount();
    QString countText = count == 1 ? tr("1 torrent") : tr("%1 torrents").arg(count);
    if (selected.size() > 1)
        countText += tr(", %1 selected").arg(selected.size());
    m_countLabel->setText(countText);
    updateStatusBarPresentation();
}

/** Уплотняет сегменты при сужении окна, сохраняя доступ к обеим скоростям. */
void MainWindow::updateStatusBarPresentation()
{
    if (!m_statusStrip)
        return;

    constexpr int connectionPadding = 35; // края, точка и зазор
    constexpr int ratePadding = 42;       // края, иконка и зазор
    const int width = m_statusStrip->width();
    QString connection = m_connectionStateText;
    if (m_connected) {
        connection = tr("Transmission %1 (RPC %2)").arg(m_sessionInfo.version).arg(m_sessionInfo.rpcVersion);
        if (m_rpc) {
            const QString address = m_rpc->options().url.adjusted(QUrl::RemoveUserInfo).authority();
            if (!address.isEmpty())
                connection += tr(" on %1").arg(address);
        }
    }

    const QString message = statusBar()->currentMessage();
    const bool showMessage = !message.isEmpty() && message != tr("Connected") && message != tr("Disconnected");
    if (showMessage)
        connection = message;

    const auto limitText = [this](bool download) {
        if (m_sessionInfo.altSpeedEnabled)
            return tr("%1 KB/s").arg(download ? m_sessionInfo.altSpeedDown : m_sessionInfo.altSpeedUp);
        const bool enabled = download ? m_sessionInfo.speedLimitDownEnabled : m_sessionInfo.speedLimitUpEnabled;
        return enabled ? tr("%1 KB/s").arg(download ? m_sessionInfo.speedLimitDown : m_sessionInfo.speedLimitUp)
                       : QString();
    };

    const QString downLimit = limitText(true);
    const QString upLimit = limitText(false);
    const auto withLimit = [](QString text, const QString& limit) {
        if (!limit.isEmpty())
            text += QStringLiteral(" (%1)").arg(limit);
        return text;
    };
    const auto rateText = [this, &withLimit](bool download, const QString& rate, const QString& limit) {
        return withLimit(download ? tr("DL: %1").arg(rate) : tr("UL: %1").arg(rate), limit);
    };
    const QString sample = QStringLiteral("999.9 MB/s");
    const auto metrics = m_statusDownload->fontMetrics();
    const int wideDown = metrics.horizontalAdvance(rateText(true, sample, downLimit)) + ratePadding;
    const int wideUp = metrics.horizontalAdvance(rateText(false, sample, upLimit)) + ratePadding;
    const int narrowRate = qMax(metrics.horizontalAdvance(withLimit(sample, downLimit)),
                                metrics.horizontalAdvance(withLimit(sample, upLimit))) + ratePadding;
    const bool compact = width < wideDown + wideUp + 250;
    const int compactWidth = qMax(0, qMin(narrowRate, (width - connectionPadding - 24) / 2));
    const int downWidth = compact ? compactWidth : wideDown;
    const int upWidth = compact ? compactWidth : wideUp;
    const auto showRate = [&](QToolButton* button, bool download, double rate, const QString& limit, int segmentWidth) {
        button->setVisible(m_connected);
        if (!m_connected)
            return;
        const QString current = QString::fromStdString(tgui::formatRate(rate));
        const QString full = rateText(download, current, limit);
        button->setFixedWidth(segmentWidth);
        const int textRoom = qMax(0, segmentWidth - ratePadding);
        const QString compactText = withLimit(current, limit);
        const QString shown = !compact ? full
            : metrics.horizontalAdvance(compactText) <= textRoom ? compactText : current;
        button->setText(metrics.elidedText(shown, Qt::ElideRight, textRoom));
        button->setToolTip(full + QLatin1Char('
') +
            (download ? tr("Max download speed") : tr("Max upload speed")));
        button->setAccessibleName(full);
    };
    showRate(m_statusDownload, true, m_statusDownRate, downLimit, downWidth);
    showRate(m_statusUpload, false, m_statusUpRate, upLimit, upWidth);

    int budget = qMax(0, width - (m_connected ? downWidth + upWidth : 0));
    const auto textMetrics = m_statusText->fontMetrics();
    const int maxConnection = m_connected ? qMin(budget, showMessage ? 480 : 360) : budget;
    const int textWidth = qMax(0, maxConnection - connectionPadding);
    QString shown = textMetrics.elidedText(connection, Qt::ElideRight, textWidth);
    if (m_connected && compact && !showMessage && shown != connection)
        shown = textMetrics.elidedText(m_connectionStateText, Qt::ElideRight, textWidth);
    m_statusText->setText(shown);
    m_statusText->setToolTip(shown == connection ? QString() : connection);
    const int connectionWidth = qMin(budget, textMetrics.horizontalAdvance(shown) + connectionPadding);
    m_statusConnection->setFixedWidth(connectionWidth);
    budget -= connectionWidth;

    const auto showSegment = [&](QWidget* segment, bool wanted) {
        const int segmentWidth = segment->sizeHint().width();
        const bool visible = m_connected && wanted && segmentWidth <= budget;
        segment->setVisible(visible);
        if (visible)
            budget -= segmentWidth;
    };
    // Приоритет не зависит от визуального порядка: счётчик находится справа.
    showSegment(m_countLabel, true);
    showSegment(m_statusAlternative, m_sessionInfo.altSpeedEnabled);
    showSegment(m_statusFree, !m_statusFree->text().isEmpty());
    showSegment(m_statusSize, m_totalsMode != 0);
    showSegment(m_statusSelectedSize, m_totalsMode != 0 && m_totalsMode != 2);
    showSegment(m_statusDone, m_totalsMode != 0);
    showSegment(m_statusLeft, m_totalsMode != 0);
}

void MainWindow::onShowProperties()
{
    const auto ids = selectedTorrentIds();
    if (ids.size() == 1) {
        showProperties(ids.first(), 0);
    } else if (ids.size() > 1 && m_rpc && m_connected) {
        const QPointer<tgui::RpcClient> rpc(m_rpc.get());
        const auto profile = m_connectionProfile;
        PropsDialog dialog(static_cast<int>(ids.size()), this, rpc->rpcVersion());
        if (dialog.exec() != QDialog::Accepted || !rpc || rpc != m_rpc.get() ||
            !m_connected || profile != m_connectionProfile || dialog.changes().isEmpty())
            return;
        rpc->torrentAction("torrent-set", ids, dialog.changes(),
            afterTorrentChange("torrent-set", RefreshAfter::Properties, m_currentId));
    }
}

/** Открывает свойства конкретного торрента на указанной вкладке. */
void MainWindow::showProperties(int id, int page)
{
    if (!m_rpc)
        return;
    for (const auto& t : m_model.torrents()) {
        if (t.id == id) {
            const QPointer<tgui::RpcClient> rpc(m_rpc.get());
            PropsDialog dlg(t, this, rpc->rpcVersion(), page);
            if (dlg.exec() == QDialog::Accepted && rpc && !dlg.changes().isEmpty())
                rpc->torrentAction("torrent-set", {id}, dlg.changes(),
                                   afterTorrentChange("torrent-set", RefreshAfter::Properties, id));
            return;
        }
    }
}

void MainWindow::onMoveSelected()
{
    if (!m_rpc || !m_connected)
        return;
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty())
        return;
    const tgui::Torrent* found = torrentById(ids.first());
    if (!found)
        return;

    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const auto profile = m_connectionProfile;
    MoveDialog dlg(found->downloadDir.value_or(QString()), this, profile);
    if (ids.size() > 1)
        dlg.setWindowTitle(tr("Move torrents - %1 selected").arg(ids.size()));
    if (dlg.exec() != QDialog::Accepted || !rpc || rpc != m_rpc.get() ||
        !m_connected || profile != m_connectionProfile)
        return;

    const QString target = dlg.targetDir();
    const bool moveData = dlg.moveData();
    if (target.isEmpty())
        return;
    rpc->setLocation(ids, target, moveData,
                     [this, rpc, ids, target, profile](const tgui::JsonRpcResult& result) {
        if (!rpc || rpc != m_rpc.get())
            return;
        if (!result.ok) {
            onRequestCompleted(QStringLiteral("torrent-set-location"), false, result.error,
                               result.arguments);
            return;
        }
        ProfilePreferences(profile).rememberMove(target);
        statusBar()->showMessage(tr("Move started: %1").arg(target), 4000);
        // Демон не возвращает новый путь в ответе — проверяем его в списке.
        m_movePendingIds = ids;
        m_movePendingTarget = target;
        m_moveVerifyAttempts = 0;
        rpc->getTorrents();
    });
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

    const auto selectedIds = selectedTorrentIds();
    const bool hasSel = m_connected && !selectedIds.isEmpty();
    const auto connectSelectionAction = [this, rpc = QPointer(m_rpc.get()), selectedIds]
                                       (QAction* action, auto handler) {
        connect(action, &QAction::triggered, this, [this, rpc, selectedIds, handler] {
            const auto current = selectedTorrentIds();
            if (rpc && rpc == m_rpc.get() &&
                std::is_permutation(current.cbegin(), current.cend(), selectedIds.cbegin(), selectedIds.cend()))
                (this->*handler)();
        });
    };
    using tgui::theme::Icon;

    // Открыть (папку, содержащую объект) — как в образце.
    auto* actOpen = menu->addAction(tgui::theme::icon(Icon::Folder), tr("Open folder containing object"));
    actOpen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    actOpen->setEnabled(hasSel);
    connectSelectionAction(actOpen, &MainWindow::onOpenFolder);
    menu->addSeparator();

    auto* actStart = menu->addAction(tgui::theme::icon(Icon::Start), tr("Start"));
    actStart->setShortcut(QKeySequence(Qt::Key_F3));
    actStart->setEnabled(hasSel);
    connectSelectionAction(actStart, &MainWindow::onStartSelected);
    auto* actForce = menu->addAction(tgui::theme::icon(Icon::ForceStart), tr("Force start"));
    actForce->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    actForce->setEnabled(hasSel);
    connectSelectionAction(actForce, &MainWindow::onStartForceSelected);
    auto* actStop = menu->addAction(tgui::theme::icon(Icon::Pause), tr("Pause"));
    actStop->setShortcut(QKeySequence(Qt::Key_F4));
    actStop->setEnabled(hasSel);
    connectSelectionAction(actStop, &MainWindow::onStopSelected);
    menu->addSeparator();

    auto* actRemove = menu->addAction(tgui::theme::icon(Icon::Remove), tr("Remove…"));
    actRemove->setShortcut(QKeySequence(Qt::Key_Delete));
    actRemove->setEnabled(hasSel);
    connectSelectionAction(actRemove, &MainWindow::onRemoveSelected);
    auto* actRemoveData = menu->addAction(tr("Remove with data…"));
    actRemoveData->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    actRemoveData->setEnabled(hasSel);
    connectSelectionAction(actRemoveData, &MainWindow::onRemoveWithDataSelected);
    menu->addSeparator();

    auto* mPriority = menu->addMenu(tr("Priority"));
    mPriority->setEnabled(hasSel);
    auto* priHigh = mPriority->addAction(tr("High"));
    auto* priNormal = mPriority->addAction(tr("Normal"));
    auto* priLow = mPriority->addAction(tr("Low"));
    connectSelectionAction(priHigh, &MainWindow::onPriorityHigh);
    connectSelectionAction(priNormal, &MainWindow::onPriorityNormal);
    connectSelectionAction(priLow, &MainWindow::onPriorityLow);

    auto* mQueue = menu->addMenu(tr("Queue"));
    mQueue->setEnabled(hasSel);
    auto* qTop = mQueue->addAction(tr("Move to top"));
    auto* qUp = mQueue->addAction(tgui::theme::icon(Icon::QueueUp), tr("Move up"));
    auto* qDown = mQueue->addAction(tgui::theme::icon(Icon::QueueDown), tr("Move down"));
    auto* qBottom = mQueue->addAction(tr("Move to bottom"));
    connectSelectionAction(qTop, &MainWindow::onQueueTop);
    connectSelectionAction(qUp, &MainWindow::onQueueUp);
    connectSelectionAction(qDown, &MainWindow::onQueueDown);
    connectSelectionAction(qBottom, &MainWindow::onQueueBottom);
    menu->addSeparator();

    auto* actAnnounce = menu->addAction(tr("Announce (get more peers)"));
    actAnnounce->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    actAnnounce->setEnabled(hasSel);
    connectSelectionAction(actAnnounce, &MainWindow::onReannounceSelected);
    auto* actVerify = menu->addAction(tr("Verify…"));
    actVerify->setEnabled(hasSel);
    connectSelectionAction(actVerify, &MainWindow::onVerifySelected);
    menu->addSeparator();

    auto* actCopyMagnet = menu->addAction(tgui::theme::icon(Icon::Copy), tr("Copy magnet link"));
    actCopyMagnet->setEnabled(hasSel);
    connectSelectionAction(actCopyMagnet, &MainWindow::onCopyMagnet);
    auto* actMove = menu->addAction(tgui::theme::icon(Icon::Move), tr("Set location…"));
    actMove->setShortcut(QKeySequence(Qt::Key_F6));
    actMove->setEnabled(hasSel);
    connectSelectionAction(actMove, &MainWindow::onMoveSelected);
    auto* actRename = menu->addAction(tr("Rename…"));
    actRename->setShortcut(QKeySequence(Qt::Key_F2));
    actRename->setEnabled(hasSel);
    connectSelectionAction(actRename, &MainWindow::onRenameSelected);
    menu->addSeparator();

    auto* actProperties = menu->addAction(tgui::theme::icon(Icon::Properties), tr("Properties…"));
    actProperties->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    actProperties->setEnabled(hasSel);
    connectSelectionAction(actProperties, &MainWindow::onShowProperties);
    menu->addSeparator();

    if (!m_labels.isEmpty()) {
        QString title = m_actChooseLabel->text();
        if (selectedIds.size() == 1) {
            const auto& torrents = m_model.torrents();
            const auto torrent = std::find_if(torrents.cbegin(), torrents.cend(),
                [id = selectedIds.first()](const auto& entry) { return entry.id == id; });
            if (torrent != torrents.cend() && torrent->labels && !torrent->labels->isEmpty()) {
                auto names = torrent->labels->join(QStringLiteral(", "));
                title = tr("Label: %1").arg(names.replace('&', "&&"));
            }
        }
        auto* labelAction = menu->addAction(tgui::theme::icon(Icon::Label), title);
        connectSelectionAction(labelAction, &MainWindow::onSetLabels);
        labelAction->setEnabled(m_actChooseLabel->isEnabled());
    }
    auto* actSetupColumns = menu->addAction(tr("Setup columns…"));
    QObject::connect(actSetupColumns, &QAction::triggered, this, &MainWindow::onColumns);
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
    if (!m_rpc || !m_connected)
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const auto paths = QFileDialog::getOpenFileNames(this, tr("Select a .torrent file"), {},
                                                     tr("Torrent files (*.torrent);;All files (*)"));
    if (!rpc || rpc != m_rpc.get() || !m_connected)
        return;
    const auto& session = rpc->sessionInfo();
    for (const auto& path : paths) {
        tgui::RpcClient::AddTorrent params;
        params.file = path;
        params.downloadDir = session.downloadDir;
        params.start = session.startAddedTorrents;
        startAdd(params);
    }
}

void MainWindow::startAdd(const tgui::RpcClient::AddTorrent& params, AddOrigin origin)
{
    m_addQueue.enqueue({params, origin, m_connectionProfile});
    if (!m_addingTorrent)
        addNextTorrent();
}

/** Добавляет новую ссылку из буфера при включённой настройке и активном подключении. */
void MainWindow::onClipboardChanged()
{
    if (!m_rpc || !m_connected || !QSettings().value(OptionsDialog::clipboardLinksKey, false).toBool())
        return;

    auto text = QApplication::clipboard()->text().trimmed();
    static const QRegularExpression bareHash(QStringLiteral("^[A-Fa-f0-9]{40}$"));
    if (bareHash.match(text).hasMatch())
        text = QStringLiteral("magnet:?xt=urn:btih:") + text.toLower();

    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid())
        return;
    url.setFragment(QString());
    const auto magnet = magnetIdentifier(url);
    if (magnet.isEmpty() && ((url.scheme() != "http" && url.scheme() != "https") ||
                            url.host().isEmpty() || !url.path().endsWith(".torrent", Qt::CaseInsensitive)))
        return;
    const auto link = url.toString(QUrl::FullyEncoded);
    const auto key = magnet.isEmpty() ? link : magnet;
    if (m_clipboardLinksSeen.contains(key))
        return;
    if (!magnet.isEmpty() && std::any_of(m_model.torrents().cbegin(), m_model.torrents().cend(),
            [&magnet](const auto& torrent) {
                return magnet == QStringLiteral("urn:btih:") + torrent.hashString.value_or(QString()).toLower() ||
                    magnet == magnetIdentifier(QUrl(torrent.magnetLink.value_or(QString())));
            }))
        return;
    m_clipboardLinksSeen.insert(key);

    tgui::RpcClient::AddTorrent params;
    params.source = tgui::RpcClient::AddTorrent::Source::Url;
    params.url = link;
    params.downloadDir = m_rpc->sessionInfo().downloadDir;
    params.start = m_rpc->sessionInfo().startAddedTorrents;
    startAdd(params);
}

/** Применяет настройку наблюдения при подключении или сохранении параметров. */
void MainWindow::updateWatchFolder()
{
    const auto path = QSettings().value(OptionsDialog::watchFolderKey).toString();
    const auto folder = path.isEmpty() ? QString() : QDir(path).absolutePath();
    if (m_watchFolder != folder) {
        m_watchFolder = folder;
        m_watchCandidates.clear();
    }
    if (m_connected && !m_watchFolder.isEmpty())
        m_watchTimer.start();
    else
        m_watchTimer.stop();
}

/** Передаёт в очередь один стабильный файл, версия которого ещё не отправлялась. */
void MainWindow::scanWatchFolder()
{
    if (!m_rpc || !m_connected || m_addingTorrent || !m_addQueue.isEmpty() || m_watchFolder.isEmpty())
        return;
    const QDir directory(m_watchFolder);
    if (!directory.exists() || !directory.isReadable()) {
        m_watchCandidates.clear();
        return;
    }

    QSettings settings;
    const auto historyKey = QStringLiteral("downloads/watchHistory/") +
        QString::fromLatin1(QUrl::toPercentEncoding(m_watchFolder));
    auto history = settings.value(historyKey).toMap();
    const auto previousHistory = history;
    QHash<QString, QString> candidates;
    QString selected;
    for (const auto& file : directory.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        const auto name = file.fileName();
        if (!name.endsWith(".torrent", Qt::CaseInsensitive))
            continue;
        const auto stamp = QString::number(file.size()) + '/' +
            QString::number(file.lastModified().toMSecsSinceEpoch());
        candidates.insert(name, stamp);
        if (selected.isEmpty() && file.size() > 0 && m_watchCandidates.value(name) == stamp &&
            history.value(name).toString() != stamp) {
            QFile input(file.absoluteFilePath());
            if (input.open(QIODevice::ReadOnly))
                selected = name;
        }
    }
    m_watchCandidates = std::move(candidates);
    for (auto it = history.begin(); it != history.end();) {
        if (!m_watchCandidates.contains(it.key()))
            it = history.erase(it);
        else
            ++it;
    }
    if (!selected.isEmpty())
        history.insert(selected, m_watchCandidates.value(selected));
    if (history != previousHistory)
        settings.setValue(historyKey, history);
    if (selected.isEmpty())
        return;

    tgui::RpcClient::AddTorrent params;
    params.file = directory.absoluteFilePath(selected);
    params.downloadDir = settings.value(OptionsDialog::watchDestinationKey).toString();
    if (params.downloadDir.isEmpty())
        params.downloadDir = m_rpc->sessionInfo().downloadDir;
    params.start = m_rpc->sessionInfo().startAddedTorrents;
    startAdd(params, AddOrigin::WatchFolder);
}

void MainWindow::addNextTorrent()
{
    if (!m_rpc || !m_connected || m_addQueue.isEmpty())
        return;

    m_addingTorrent = true;
    auto [params, origin, profile] = m_addQueue.dequeue();
    const bool fromWatchFolder = origin == AddOrigin::WatchFolder;
    const bool requireLocalFile = origin != AddOrigin::Interactive &&
        params.source == tgui::RpcClient::AddTorrent::Source::File;
    const bool start = params.start;
    params.start = false;

    const QSettings settings;
    const bool skipDialog = settings.value(OptionsDialog::skipAddDialogKey, false).toBool();
    auto request = params;
    QByteArray sourceBytes;
    const bool deleteSource = params.source == tgui::RpcClient::AddTorrent::Source::File &&
        params.file.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive) &&
        settings.value(OptionsDialog::deleteSourceTorrentKey, false).toBool();
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    if (requireLocalFile || deleteSource) {
        params.file = QFileInfo(params.file).absoluteFilePath();
        request.file = params.file;
        QFile file(params.file);
        if (file.open(QIODevice::ReadOnly)) {
            sourceBytes = file.readAll();
            if (file.error() != QFileDevice::NoError)
                sourceBytes.clear();
            if (!sourceBytes.isEmpty()) {
                // Отправляем те же байты, по которым проверим исходник перед удалением.
                request.source = tgui::RpcClient::AddTorrent::Source::Base64;
                request.metainfoBase64 = QString::fromLatin1(sourceBytes.toBase64());
            }
        }
        if (requireLocalFile && sourceBytes.isEmpty()) {
            onReadError(tr("Add torrent"), tr("Could not read the local torrent file: %1").arg(params.file));
            finishAdd(rpc);
            return;
        }
        if (!deleteSource)
            sourceBytes.clear();
    }

    // Берём список файлов у демона, пока торрент на паузе.
    rpc->addTorrent(request, [this, rpc, start, params, skipDialog, sourceBytes, fromWatchFolder, profile](
                                const tgui::JsonRpcResult& addedResult, const QByteArray& metainfo) {
        if (!rpc || rpc != m_rpc.get())
            return;
        const auto duplicate = addedResult.arguments.value("torrent-duplicate");
        if ((addedResult.ok && duplicate.isObject()) ||
            addedResult.status == "duplicate torrent" || addedResult.error == "duplicate torrent") {
            offerDuplicateUpdate(params, duplicate.toObject(), metainfo);
            return;
        }
        const auto added = addedResult.arguments.value("torrent-added").toObject();
        const int id = added.value("id").toInt(-1);
        if (!addedResult.ok || id < 0) {
            const QString message = !addedResult.ok ? addedResult.error : tr("Unable to add torrent.");
            onReadError(tr("Add torrent"), message);
            finishAdd(rpc);
            return;
        }
        rpc->getTorrentDetails(id, [this, rpc, id, start, params, skipDialog, sourceBytes, fromWatchFolder, profile](const auto& details) {
            if (!rpc || rpc != m_rpc.get() || !m_connected)
                return;
            const auto remove = [this, rpc, id] {
                rpc->removeTorrent(id, false, [this, rpc](const tgui::JsonRpcResult& result) {
                    if (!rpc || rpc != m_rpc.get())
                        return;
                    if (!result.ok)
                        onReadError(tr("Remove torrent"), result.error);
                    finishAdd(rpc);
                });
            };
            if (!details) {
                remove();
                return;
            }
            const auto configured = [this, rpc, id, path = params.file, sourceBytes](
                                        const tgui::JsonRpcResult& result) {
                if (!rpc || rpc != m_rpc.get())
                    return;
                if (!result.ok) {
                    onReadError(tr("Torrent added with incomplete settings"), result.error);
                } else if (!sourceBytes.isEmpty()) {
                    QFile original(path);
                    const bool unchanged = original.open(QIODevice::ReadOnly) &&
                        original.readAll() == sourceBytes && original.error() == QFileDevice::NoError;
                    original.close();
                    if (!unchanged || !original.remove())
                        onReadError(tr("Torrent added"), tr("Could not remove the original file: %1").arg(path));
                    else
                        statusBar()->showMessage(tr("Torrent added"), 5000);
                } else {
                    statusBar()->showMessage(tr("Torrent added"), 5000);
                }
                finishAdd(rpc, id);
            };
            if (skipDialog) {
                auto options = params;
                options.downloadDir = details->downloadDir;
                options.start = start;
                options.sequential = options.sequential ||
                    tgui::automaticSequential(details->files, rpc->rpcVersion());
                rpc->configureAddedTorrent(*details, options, {}, {}, configured);
                return;
            }
            auto* dialog = new FileSelectDialog(*details, start, rpc->rpcVersion(), rpc, this, !fromWatchFolder, profile);
            dialog->offerMetainfoCopy(params);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(rpc.data(), &QObject::destroyed, dialog, &QDialog::reject);
            connect(dialog, &QDialog::finished, this, [this, rpc, dialog, remove, details, configured](int dialogResult) {
                if (!rpc || rpc != m_rpc.get())
                    return;
                if (dialogResult != QDialog::Accepted) {
                    remove();
                    return;
                }
                rpc->configureAddedTorrent(*details, dialog->options(), dialog->saveAs(),
                                           dialog->unwanted(), configured);
            });
            dialog->open();
        });
    });
}

/** Offer to merge the incoming tracker URLs into an already registered torrent. */
void MainWindow::offerDuplicateUpdate(const tgui::RpcClient::AddTorrent& source,
                                      const QJsonObject& duplicate, const QByteArray& metainfo)
{
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const QString name = duplicate.value("name").toString();
    const QString message = name.isEmpty() ? tr("Torrent already exists.") :
        tr("The torrent “%1” already exists.").arg(name);
    auto* box = new QMessageBox(QMessageBox::Question, tr("Update torrent"), message,
                                QMessageBox::Cancel, this);
    box->setTextFormat(Qt::PlainText);
    box->setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Information).pixmap(QSize(32, 32), devicePixelRatioF()));
    box->setInformativeText(tr("Add missing trackers to the existing torrent?"));
    auto* update = box->addButton(tr("Update trackers"), QMessageBox::AcceptRole);
    box->setDefaultButton(QMessageBox::Cancel);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(rpc.data(), &QObject::destroyed, box, &QDialog::reject);
    connect(box, &QDialog::finished, this, [this, rpc, box, update, duplicate, source, metainfo] {
        if (!rpc || rpc != m_rpc.get())
            return;
        if (box->clickedButton() != update) {
            finishAdd(rpc);
            return;
        }
        const int id = duplicate.value("id").toInt(-1);
        const auto merge = [this, rpc, id](const QString& magnet, const QByteArray& bytes,
                                          const QString& expectedHash = {}) {
            rpc->updateDuplicateTrackers(id, magnet, bytes, [this, rpc, id](const QString& error,
                                                                            bool updated) {
                if (!rpc || rpc != m_rpc.get())
                    return;
                if (!error.isEmpty())
                    onReadError(tr("Update torrent"), error);
                else
                    statusBar()->showMessage(updated ? tr("Torrent trackers updated.") :
                                                         tr("No new trackers to add."), 5000);
                if (updated && m_currentId >= 0 && (id < 0 || id == m_currentId))
                    rpc->getTrackers(m_currentId);
                finishAdd(rpc, error.isEmpty() ? id : -1);
            }, expectedHash);
        };
        const QUrl url(source.url);
        if (source.source == tgui::RpcClient::AddTorrent::Source::Url &&
            (url.scheme() == "http" || url.scheme() == "https")) {
            QNetworkRequest request(url);
            request.setTransferTimeout(rpc->options().timeoutMs);
            auto* reply = m_nam.get(request);
            const auto expectedHash = duplicate.value("hashString").toString();
            connect(reply, &QNetworkReply::finished, this, [this, rpc, reply, merge, expectedHash] {
                reply->deleteLater();
                if (!rpc || rpc != m_rpc.get())
                    return;
                if (reply->error() == QNetworkReply::NoError) {
                    merge({}, reply->readAll(), expectedHash);
                } else {
                    onReadError(tr("Update torrent"), reply->errorString());
                    finishAdd(rpc);
                }
            });
        } else {
            merge(url.scheme() == "magnet" ? source.url : QString(), metainfo);
        }
    });
    box->open();
}

// Главное меню — структура по референсу (Torrent / View / Tools / Help,
// пункты — main.lfm).
void MainWindow::createMenus()
{
    using tgui::theme::Icon;
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
    auto* actForceStart = mTorrent->addAction(tgui::theme::icon(Icon::ForceStart), tr("Force start"));
    actForceStart->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(actForceStart, &QAction::triggered, this, &MainWindow::onStartForceSelected);
    mTorrent->addAction(m_actStartAll);
    mTorrent->addAction(m_actStop);
    mTorrent->addAction(m_actStopAll);
    mTorrent->addAction(m_actRemove);
    auto* actRemoveData = mTorrent->addAction(tr("Remove torrent and Data"));
    actRemoveData->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    connect(actRemoveData, &QAction::triggered, this, &MainWindow::onRemoveWithDataSelected);
    mTorrent->addSeparator();

    auto* mPriority = mTorrent->addMenu(tr("Priority"));
    auto* priHigh = mPriority->addAction(tr("High priority"));
    priHigh->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    auto* priNormal = mPriority->addAction(tr("Normal priority"));
    priNormal->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    auto* priLow = mPriority->addAction(tr("Low priority"));
    priLow->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(priHigh, &QAction::triggered, this, &MainWindow::onPriorityHigh);
    connect(priNormal, &QAction::triggered, this, &MainWindow::onPriorityNormal);
    connect(priLow, &QAction::triggered, this, &MainWindow::onPriorityLow);

    auto* mQueue = mTorrent->addMenu(tr("Queue"));
    auto* qTop = mQueue->addAction(tr("Move top"));
    qTop->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1));
    auto* qUp = mQueue->addAction(tgui::theme::icon(Icon::QueueUp), tr("Move up"));
    qUp->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    auto* qDown = mQueue->addAction(tgui::theme::icon(Icon::QueueDown), tr("Move down"));
    qDown->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    auto* qBottom = mQueue->addAction(tr("Move bottom"));
    qBottom->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2));
    connect(qTop, &QAction::triggered, this, &MainWindow::onQueueTop);
    connect(qUp, &QAction::triggered, this, &MainWindow::onQueueUp);
    connect(qDown, &QAction::triggered, this, &MainWindow::onQueueDown);
    connect(qBottom, &QAction::triggered, this, &MainWindow::onQueueBottom);

    auto* actReannounce = mTorrent->addAction(tr("Reannounce (get more peers)"));
    actReannounce->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    auto* actVerify = mTorrent->addAction(tr("Verify"));
    connect(actReannounce, &QAction::triggered, this, &MainWindow::onReannounceSelected);
    connect(actVerify, &QAction::triggered, this, &MainWindow::onVerifySelected);
    mTorrent->addAction(m_actMove);  // "Set data location..."
    m_actChooseLabel = mTorrent->addAction(tgui::theme::icon(Icon::Label), tr("Choose label…"));
    m_general->setEditLabelsAction(m_actChooseLabel);
    m_actChooseLabel->setShortcut(QKeySequence(Qt::Key_F7));
    connect(m_actChooseLabel, &QAction::triggered, this, &MainWindow::onSetLabels);
    auto* actRename = mTorrent->addAction(tr("Rename"));
    actRename->setShortcut(QKeySequence(Qt::Key_F2));
    connect(actRename, &QAction::triggered, this, &MainWindow::onRenameSelected);
    mTorrent->addAction(m_actProperties);
    mTorrent->addSeparator();
    auto* actExport = mTorrent->addAction(tr("Export settings…"));
    connect(actExport, &QAction::triggered, this, &MainWindow::onExportSettings);
    auto* actImport = mTorrent->addAction(tr("Import settings…"));
    connect(actImport, &QAction::triggered, this, &MainWindow::onImportSettings);
    auto* exportTorrents = mTorrent->addAction(tr("Export torrent list…"));
    connect(exportTorrents, &QAction::triggered, this, &MainWindow::onExportTorrentList);
    auto* importTorrents = mTorrent->addAction(tr("Import torrent list…"));
    connect(importTorrents, &QAction::triggered, this, &MainWindow::onImportTorrentList);
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
        QSettings().setValue("ui/labelGrouping", on);
        updateLabelUi();
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
    auto* totals = mView->addMenu(tr("Size totals"));
    auto* totalChoices = new QActionGroup(totals);
    const QStringList captions{tr("Hide size totals"), tr("Visible torrents"), tr("Selected torrents"), tr("All torrents")};
    for (int i = 0; i < captions.size(); ++i) {
        auto* action = totals->addAction(captions.at(i));
        action->setCheckable(true);
        action->setChecked(m_totalsMode == i);
        totalChoices->addAction(action);
        connect(action, &QAction::triggered, this, [this, i] {
            m_totalsMode = i;
            QSettings().setValue("ui/totalsMode", i);
            updateStatusBarTotals();
        });
    }

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
    connect(m_filterPanel, &FilterPanel::contextMenuRequested, this, [this](const QPoint& position) {
        QMenu menu(this);
        menu.addAction(m_actFolderGrouping);
        menu.addAction(m_actLabelGrouping);
        menu.addAction(m_actTrackerGrouping);
        menu.addSeparator();
        auto* expand = menu.addAction(tr("Expand all sections"));
        connect(expand, &QAction::triggered, m_filterPanel, &FilterPanel::expandSections);
        auto* hide = menu.addAction(tr("Hide filter panel"));
        connect(hide, &QAction::triggered, this, [this] { m_actFilterPanel->setChecked(false); });
        menu.exec(position);
    });

    m_actInfoPanel = mView->addAction(tr("Info panel"));
    m_actInfoPanel->setCheckable(true);
    m_actInfoPanel->setShortcut(QKeySequence(Qt::Key_F12));
    m_actInfoPanel->setChecked(uiBool("ui/infoPanel", "ui/infoPane", true));
    connect(m_actInfoPanel, &QAction::toggled, this, [this](bool on) {
        m_tabs->setVisible(on);
        QSettings().setValue("ui/infoPanel", on);
        if (on)
            refreshFilesDetails();
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
    addSpeedLimitMenu(mTools, true);
    addSpeedLimitMenu(mTools, false);
    mTools->addAction(m_actDaemonOptions);
    mTools->addAction(m_actOptions);
    mTools->addSeparator();
    mTools->addAction(m_actAltSpeed);  // "Use alternate bandwidth settings"
    auto* actBlocklist = mTools->addAction(tr("Update blocklist"));
    connect(actBlocklist, &QAction::triggered, this, &MainWindow::onUpdateBlocklist);
    auto* actGeoIp = mTools->addAction(tr("Update GeoIP database"));
    connect(actGeoIp, &QAction::triggered, this, &MainWindow::onUpdateGeoIp);
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
    updateLabelUi();
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
    if (ids.isEmpty() || m_labels.isEmpty() || !m_rpc || !m_connected || !m_rpc->supports(16))
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    QStringList current;
    if (ids.size() == 1) {
        for (const auto& t : m_model.torrents()) {
            if (t.id == ids.first()) {
                current = t.labels.value_or(QStringList{});
                break;
            }
        }
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Choose label…"));
    auto* form = new QFormLayout(&dialog);
    auto* picker = torrentlabels::createPicker(&dialog, m_labels, current);
    if (ids.size() > 1) {
        picker->insertItem(0, tr("Keep current labels"));
        picker->setCurrentIndex(0);
    }
    form->addRow(tr("Label"), picker);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !rpc || !picker->currentData().isValid())
        return;
    const auto labels = picker->currentData().toStringList();
    if (ids.size() == 1 && labels == current)
        return;
    QJsonArray torrentIds;
    for (const int id : ids)
        torrentIds.append(id);
    rpc->setTorrent(ids.first(), {{"ids", torrentIds}, {"labels", QJsonArray::fromStringList(labels)}});
}

void MainWindow::onExportSettings()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export settings"), QStringLiteral("transmissiongui.ini"),
        tr("INI files (*.ini)"));
    if (path.isEmpty())
        return;
    QSettings cur;
    cur.setFallbacksEnabled(false);
    if (!settingsbackup::write(path, settingsbackup::snapshot(cur))) {
        QMessageBox::warning(this, tr("Export settings"), tr("Could not write the settings backup."));
        return;
    }
    statusBar()->showMessage(tr("Settings exported to: %1").arg(path), 4000);
}

void MainWindow::onImportSettings()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import settings"), QString(), tr("INI files (*.ini)"));
    if (path.isEmpty())
        return;
    const auto data = settingsbackup::read(path);
    if (!data) {
        QMessageBox::warning(this, tr("Import settings"), tr("The file is not a supported transmissionGUI settings backup."));
        return;
    }
    if (QMessageBox::question(this, tr("Import settings"),
            tr("Import settings from file? Current settings will be overwritten.\n"
               "Restart the application to apply the changes."))
            != QMessageBox::Yes)
        return;
    QSettings cur;
    cur.setFallbacksEnabled(false);
    if (!settingsbackup::replace(cur, *data)) {
        QMessageBox::warning(this, tr("Import settings"), tr("Could not apply the settings backup."));
        return;
    }
    m_settingsImported = true;
    onDisconnect();
    statusBar()->showMessage(tr("Settings imported. Restart to apply."), 4000);
}

/** Экспортирует весь подтверждённый список в переносимый файл magnet-ссылок. */
void MainWindow::onExportTorrentList()
{
    QStringList links;
    QSet<QString> identifiers;
    for (const auto& torrent : m_model.torrents()) {
        auto link = torrent.magnetLink.value_or(QString());
        if (link.isEmpty() && torrent.hashString)
            link = QStringLiteral("magnet:?xt=urn:btih:") + *torrent.hashString;
        const auto key = magnetIdentifier(QUrl(link, QUrl::StrictMode));
        if (!key.isEmpty() && !identifiers.contains(key)) {
            identifiers.insert(key);
            links.append(link);
        }
    }
    if (links.isEmpty()) {
        QMessageBox::information(this, tr("Export torrent list"), tr("There are no torrent links to export."));
        return;
    }
    const auto path = QFileDialog::getSaveFileName(this, tr("Export torrent list"), "torrents.txt",
                                                  tr("Magnet lists (*.txt)"));
    if (!path.isEmpty() && !settingsbackup::writeFile(path, (links.join('\n') + '\n').toUtf8()))
        QMessageBox::warning(this, tr("Export torrent list"), tr("Could not write the torrent list."));
}

/** Проверяет весь файл и ставит ссылки в обычную очередь добавления после подтверждения. */
void MainWindow::onImportTorrentList()
{
    if (!m_rpc || !m_connected) {
        QMessageBox::information(this, tr("Import torrent list"), tr("Not connected to the daemon."));
        return;
    }
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const auto profile = m_connectionProfile;
    const auto path = QFileDialog::getOpenFileName(this, tr("Import torrent list"), {},
                                                  tr("Magnet lists (*.txt)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    const auto invalidList = [this] {
        QMessageBox::warning(this, tr("Import torrent list"), tr("The file must contain one magnet link per line."));
    };
    if (!file.open(QIODevice::ReadOnly) || file.size() > settingsbackup::maxBytes) {
        invalidList();
        return;
    }
    QStringList links;
    QSet<QString> identifiers;
    auto contents = QString::fromUtf8(file.read(settingsbackup::maxBytes + 1));
    if (file.error() != QFileDevice::NoError || !file.atEnd()) {
        invalidList();
        return;
    }
    if (contents.startsWith(QChar::ByteOrderMark))
        contents.remove(0, 1);
    for (const auto& line : contents.split('\n')) {
        const auto text = line.trimmed();
        if (text.isEmpty())
            continue;
        const QUrl url(text, QUrl::StrictMode);
        const auto key = magnetIdentifier(url);
        if (!url.isValid() || key.isEmpty()) {
            invalidList();
            return;
        }
        if (!identifiers.contains(key)) {
            identifiers.insert(key);
            links.append(url.toString(QUrl::FullyEncoded));
        }
    }
    if (links.isEmpty()) {
        invalidList();
        return;
    }
    if (QMessageBox::question(this, tr("Import torrent list"),
            tr("Add %1 torrent links to the connected daemon?").arg(links.size())) != QMessageBox::Yes)
        return;
    if (rpc && rpc == m_rpc.get() && m_connected && profile == m_connectionProfile)
        openExternalTorrents(links);
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
    openTorrentObject(ids.first(), /*containingFolder=*/true);
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
// завершённый торрент с существующим объектом — открыть его файл/папку,
// иначе — свойства торрента.
void MainWindow::onTorrentsDoubleClicked()
{
    if (m_currentId < 0)
        return;
    const tgui::Torrent* t = torrentById(m_currentId);
    if (!t)
        return;
    // Готовый торрент (leftUntilDone == 0) — открыть его файл/папку;
    // не готовый — свойства (в образце файл ещё не скачан, запускать нечего).
    const bool finished = t->leftUntilDone.has_value() && t->leftUntilDone.value() <= 0.0
                          && (t->sizeWhenDone.value_or(0.0) > 0.0);
    if (finished)
        openTorrentObject(m_currentId, /*containingFolder=*/false);
    else
        showProperties(m_currentId, 0);
}

/** Открывает файл/папку торрента, вычисляя путь из списка файлов и downloadDir. */
void MainWindow::openTorrentObject(int id, bool containingFolder)
{
    const tgui::Torrent* torrent = torrentById(id);
    if (!torrent)
        return;

    // Без соединения (например, в тестах) — путь по имени торрента.
    const auto openFromName = [this, torrent, containingFolder] {
        const QString dir = torrent->downloadDir.value_or(QString());
        if (dir.isEmpty())
            return;
        const QString name = torrent->name.value_or(QString());
        if (name.isEmpty())
            openLocalPath(dir, /*selectFile=*/false);
        else
            openLocalPath(dir + QStringLiteral("/") + name, containingFolder);
    };
    if (!m_rpc || !m_connected) {
        openFromName();
        return;
    }

    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const auto profile = m_connectionProfile;
    rpc->getTorrentDetails(id, [this, rpc, id, containingFolder, profile]
                           (const std::optional<tgui::TorrentDetails>& details) {
        if (!rpc || rpc != m_rpc.get() || !m_connected || profile != m_connectionProfile)
            return;
        const tgui::Torrent* current = torrentById(id);
        if (!current)
            return;
        if (!details) {
            if (!containingFolder)
                showProperties(id, 0);
            return;
        }
        bool selectFile = false;
        QString rel = tgui::torrentOpenRelativePath(details->files, containingFolder, &selectFile);
        if (rel.isEmpty())
            rel = current->name.value_or(QString());
        const QString dir = details->downloadDir.isEmpty()
                                ? current->downloadDir.value_or(QString())
                                : details->downloadDir;
        if (dir.isEmpty())
            return;
        const QString path = rel.isEmpty() ? dir : dir + QStringLiteral("/") + rel;
        // Как в образце: двойной клик открывает только существующий объект,
        // иначе показывает свойства.
        if (!containingFolder && !QFileInfo::exists(mappedLocalPath(path))) {
            showProperties(id, 0);
            return;
        }
        openLocalPath(path, selectFile);
    });
}

const tgui::Torrent* MainWindow::torrentById(int id) const
{
    for (const auto& torrent : m_model.torrents())
        if (torrent.id == id)
            return &torrent;
    return nullptr;
}

QString MainWindow::mappedLocalPath(const QString& remotePath) const
{
    return tgui::mapRemoteToLocal(remotePath, ProfilePreferences(m_connectionProfile).mappings());
}

/** Сверяет downloadDir выбранных торрентов с целью переноса после ответа демона. */
void MainWindow::checkPendingMove()
{
    if (m_movePendingIds.isEmpty())
        return;
    const auto normalize = [](const QString& path) {
        QString value = QDir::fromNativeSeparators(path.trimmed());
        while (value.size() > 1 && value.endsWith(QLatin1Char('/')))
            value.chop(1);
#ifdef Q_OS_WIN
        return value.toLower();
#else
        return value;
#endif
    };
    const QString target = normalize(m_movePendingTarget);
    bool confirmed = true;
    for (const int id : m_movePendingIds) {
        const tgui::Torrent* torrent = torrentById(id);
        if (!torrent || normalize(torrent->downloadDir.value_or(QString())) != target) {
            confirmed = false;
            break;
        }
    }
    if (confirmed) {
        statusBar()->showMessage(tr("Move completed: %1").arg(m_movePendingTarget), 5000);
        m_movePendingIds.clear();
        m_movePendingTarget.clear();
        m_moveVerifyAttempts = 0;
        return;
    }
    // Даём демону несколько циклов на обновление пути (образец ждёт до 20 с).
    if (++m_moveVerifyAttempts >= 20) {
        statusBar()->showMessage(
            tr("Move finished, but the daemon still reports another folder."), 8000);
        m_movePendingIds.clear();
        m_movePendingTarget.clear();
        m_moveVerifyAttempts = 0;
        return;
    }
    if (m_rpc && m_connected && !m_moveVerifyTimer.isActive())
        m_moveVerifyTimer.start();
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
    const QString local = mappedLocalPath(path);

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
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    auto* box = new QMessageBox(this);
    box->setWindowTitle(tr("Remove with data"));
    box->setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Warning).pixmap(QSize(32, 32), devicePixelRatioF()));
    box->setText(tr("Remove the selected torrent(s) AND delete their downloaded data files?"));
    box->setInformativeText(tr("This cannot be undone."));
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box->setDefaultButton(QMessageBox::No);
    const int r = box->exec();
    box->deleteLater();
    if (rpc && r == QMessageBox::Yes)
        runTorrentAction("torrent-remove", ids, {{"delete-local-data", true}});
}

void MainWindow::onVerifySelected()
{
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty() || !m_rpc || !m_connected)
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    QMessageBox box(QMessageBox::Question, tr("Verify"),
        ids.size() == 1
            ? tr("Torrent verification may take a long time.\nAre you sure to start verification?")
            : tr("Torrent verification may take a long time.\nAre you sure to start verification of %1 torrents?")
                  .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Question).pixmap(QSize(32, 32), devicePixelRatioF()));
    if (box.exec() == QMessageBox::Yes && rpc)
        runTorrentAction("torrent-verify", ids);
}

void MainWindow::onReannounceSelected()
{
    runTorrentAction("torrent-reannounce", selectedTorrentIds());
}

void MainWindow::onQueueTop()
{
    runTorrentAction("queue-move-top", selectedTorrentIds());
}

void MainWindow::onQueueUp()
{
    runTorrentAction("queue-move-up", selectedTorrentIds());
}

void MainWindow::onQueueDown()
{
    runTorrentAction("queue-move-down", selectedTorrentIds());
}

void MainWindow::onQueueBottom()
{
    runTorrentAction("queue-move-bottom", selectedTorrentIds());
}

void MainWindow::onStartForceSelected()
{
    runTorrentAction("torrent-start-now", selectedTorrentIds());
}

void MainWindow::onAddFromLink()
{
    if (!m_rpc || !m_connected)
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    AddLinkDialog dlg(QSettings().value("lastAddLink").toString(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!rpc || rpc != m_rpc.get() || !m_connected)
        return;
    const QString link = dlg.link();
    if (link.isEmpty())
        return;
    QSettings().setValue("lastAddLink", link);
    tgui::RpcClient::AddTorrent at;
    at.source = tgui::RpcClient::AddTorrent::Source::Url;
    at.url = link;
    at.start = dlg.startWhenAdded();
    at.downloadDir = rpc->sessionInfo().downloadDir;
    startAdd(at);
}

void MainWindow::onUpdateBlocklist()
{
    if (m_rpc && m_connected)
        m_rpc->updateBlocklist();
}

bool MainWindow::ensureFlagImages(bool allowDownload)
{
    const QString appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString flagsDir = appDir + QStringLiteral("/flags");
    if (!QDir(flagsDir).entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty())
        return true;

    const QString zipPath = appDir + QStringLiteral("/flags.zip");
    if (!QFileInfo::exists(zipPath)) {
        if (!allowDownload || !QDir().mkpath(appDir))
            return false;
        static const char* kFlagsUrl =
            "https://raw.githubusercontent.com/transmission-remote-gui/transgui/master/flags.zip";
        QNetworkRequest request{QUrl{kFlagsUrl}};
        request.setTransferTimeout(15000);
        QEventLoop loop;
        auto* reply = m_nam.get(request);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const bool downloaded = reply->error() == QNetworkReply::NoError;
        const QByteArray archive = downloaded ? reply->readAll() : QByteArray();
        reply->deleteLater();
        if (!downloaded || archive.isEmpty())
            return false;
        QFile file(zipPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(archive) != archive.size())
            return false;
        file.close();
    }

    if (!QDir().mkpath(flagsDir))
        return false;
    auto quote = [](QString path) { return path.replace(QChar(0x27), QStringLiteral("''")); };
    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
         QStringLiteral("-Command"),
         QStringLiteral("Expand-Archive -LiteralPath '") + quote(zipPath) +
         QStringLiteral("' -DestinationPath '") + quote(flagsDir) +
         QStringLiteral("' -Force")});
    if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0)
        return false;
    return !QDir(flagsDir).entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty();
}

void MainWindow::onUpdateGeoIp()
{
    static const char *kGeoIpUrl = "https://dl.miyuru.lk/geoip/maxmind/country/maxmind4.dat.gz";

    QMessageBox box(QMessageBox::Question, tr("Update GeoIP database"),
        tr("GeoIP database is needed to resolve country by IP address.\n"
           "Download this database now?"),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    box.setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Question).pixmap(QSize(32, 32), devicePixelRatioF()));
    if (box.exec() != QMessageBox::Yes)
        return;

    // Прогресс-бар (как в оригинале).
    auto* progress = new QProgressDialog(tr("Downloading GeoIP database…"), tr("Cancel"),
                                         0, 100, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    const QUrl url{kGeoIpUrl};
    auto* reply = m_nam.get(QNetworkRequest{url});

    // Обновляем прогресс.
    QObject::connect(reply, &QNetworkReply::downloadProgress, this,
        [progress](qint64 received, qint64 total) {
            if (total > 0)
                progress->setValue(static_cast<int>(received * 100 / total));
        });

    // Отмена.
    QObject::connect(progress, &QProgressDialog::canceled, this, [reply]() {
        reply->abort();
    });

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, progress]() {
        reply->deleteLater();
        progress->setValue(100);

        if (reply->error() != QNetworkReply::NoError) {
            progress->close();
            progress->deleteLater();
            QMessageBox::warning(this, tr("Update GeoIP database"),
                tr("Download failed: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray raw = reply->readAll();

        // Определяем, gzip или raw .dat
        const bool isGzip = raw.size() > 2 &&
                            static_cast<quint8>(raw[0]) == 0x1f &&
                            static_cast<quint8>(raw[1]) == 0x8b;

        const QString appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(appDir);
        const QString datPath = appDir + QStringLiteral("/GeoIP.dat");

        if (isGzip) {
            QByteArray dat;
            if (!gunzipData(raw, dat)) {
                progress->close();
                progress->deleteLater();
                QMessageBox::warning(this, tr("Update GeoIP database"),
                    tr("Decompression failed."));
                return;
            }
            QFile f(datPath);
            if (f.open(QIODevice::WriteOnly))
                f.write(dat);
        } else {
            QFile f(datPath);
            if (f.open(QIODevice::WriteOnly))
                f.write(raw);
        }

        if (!QFileInfo::exists(datPath)) {
            progress->close();
            progress->deleteLater();
            QMessageBox::warning(this, tr("Update GeoIP database"),
                tr("Save failed."));
            return;
        }

        if (!m_geoip.load(datPath.toStdString())) {
            progress->close();
            progress->deleteLater();
            QMessageBox::warning(this, tr("Update GeoIP database"),
                tr("Database is invalid."));
            return;
        }

        progress->close();
        progress->deleteLater();

        if (QSettings().value("peers/showFlag", false).toBool())
            ensureFlagImages(true);

        // Обновить модель пиров.
        const auto& currentPeers = m_peerModel.peers();
        if (!currentPeers.isEmpty())
            m_peerModel.setPeers(currentPeers);
        statusBar()->showMessage(tr("GeoIP database loaded."), 5000);
    });
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
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const QString currentName = found->name.value();
    if (currentName.isEmpty())
        return;
    bool ok = false;
    const QString newName = QInputDialog::getText(
        this, tr("Rename"), tr("New name:"),
        QLineEdit::Normal, currentName, &ok);
    const QString trimmed = newName.trimmed();
    if (!ok || !rpc || trimmed.isEmpty() || trimmed == currentName)
        return;
    rpc->renamePath(id, currentName, trimmed,
                    afterTorrentChange("torrent-rename-path", RefreshAfter::Properties, id));
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
    if (m_checkingUpdate)
        return;
    m_checkingUpdate = true;
    statusBar()->showMessage(tr("Checking for updates…"));
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/don10077/transmissionGUI/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "transmissionGUI");
    request.setTransferTimeout(10000);
    auto* reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_checkingUpdate = false;
        statusBar()->clearMessage();
        reply->deleteLater();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto title = tr("Check for updates");
        if (http == 404) {
            QMessageBox::information(this, title, tr("No published release was found."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, title, tr("Could not check for updates: %1").arg(reply->errorString()));
            return;
        }
        const auto release = QJsonDocument::fromJson(reply->readAll()).object();
        const auto tag = release.value("tag_name").toString();
        const auto number = tag.startsWith('v', Qt::CaseInsensitive) ? tag.mid(1) : tag;
        qsizetype suffix = 0;
        const auto latest = QVersionNumber::fromString(number, &suffix);
        if (latest.isNull() || suffix != number.size()) {
            QMessageBox::warning(this, title, tr("Could not read the published version."));
            return;
        }
        const auto current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        if (latest.normalized() <= current.normalized()) {
            QMessageBox::information(this, title, tr("You have the latest version."));
            return;
        }
        QMessageBox box(QMessageBox::Information, title, tr("Version %1 is available.").arg(number),
                        QMessageBox::Close, this);
        auto* open = box.addButton(tr("Open release"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == open)
            QDesktopServices::openUrl(QUrl::fromEncoded(
                "https://github.com/don10077/transmissionGUI/releases/tag/" + QUrl::toPercentEncoding(tag)));
    });
}

void MainWindow::onPriorityHigh()
{
    runTorrentAction("torrent-set", selectedTorrentIds(), {{"bandwidthPriority", 1}});
}

void MainWindow::onPriorityNormal()
{
    runTorrentAction("torrent-set", selectedTorrentIds(), {{"bandwidthPriority", 0}});
}

void MainWindow::onPriorityLow()
{
    runTorrentAction("torrent-set", selectedTorrentIds(), {{"bandwidthPriority", -1}});
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
    auto* rpc = m_connected ? m_rpc.get() : nullptr;
    const auto profile = (m_rpc || m_reconnectOptions) ? m_connectionProfile : ProfileStore().currentName();
    OptionsDialog dlg(this, rpc ? rpc->rpcVersion() : 0, rpc, profile);
    const auto result = dlg.exec();

    m_labels = torrentlabels::configured();
    updateFilterPanel(m_model.torrents());

    if (result != QDialog::Accepted)
        return;

    updateWatchFolder();
    // Интервал обновления — применяем сразу (запущенный таймер подхватит новый интервал).
    updateRefreshSchedule();
    m_model.setSmoothRates(QSettings().value("ui/smoothRates", false).toBool());
    updateStatusBarTotals();
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
            active.ignoreSslErrors == opts.ignoreSslErrors && active.proxy == opts.proxy &&
            active.clientCertificatePath == opts.clientCertificatePath &&
            active.clientKeyPath == opts.clientKeyPath) {
            store.setCurrent(name);
            m_connectionProfile = name;
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
    m_connectionProfile = name;
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
    // Отправляем только изменённые поля: не перезаписываем чужие/неподдерживаемые.
    const QJsonObject params = dlg.changedSessionSet();
    if (params.isEmpty()) {
        statusBar()->showMessage(tr("No changes to apply."), 3000);
        return;
    }
    m_rpc->setSession(params); // фидбек — через onRequestCompleted (status bar)
    statusBar()->showMessage(tr("Applying daemon options…"), 3000);
}

void MainWindow::onToggleAltSpeed()
{
}

void MainWindow::onTorrentDetailsReceived(const tgui::TorrentDetails& details)
{
    // Вкладка «Файлы» для выбранного торрента.
    if (details.id != m_currentId)
        return;
    // Прогресс проверки берём из текущего снимка списка торрентов (а не из
    // ответа деталей): так список и вкладка показывают одно и то же число.
    QVector<tgui::FileStat> files = details.files;
    for (const auto& t : m_model.torrents()) {
        if (t.id != details.id)
            continue;
        tgui::applyCheckProgress(files, t.status.value_or(-1),
                                 t.recheckProgress,
                                 t.pieceCount.value_or(0));
        break;
    }
    m_fileModel.setFiles(files);
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
    updateStatusBarTotals();
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
    m_fileModel.setFiles({});
    m_peerModel.setPeers({});
    m_trackerModel.setTrackers({});
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

void MainWindow::onFileWantToggled(int fileId, bool wanted)
{
    setFilesDownload({fileId}, wanted);
}

void MainWindow::onFilesContextMenu(const QPoint& pos)
{
    using tgui::theme::Icon;
    if (!m_rpc || m_currentId < 0)
        return;
    auto* view = qobject_cast<QTreeView*>(sender());
    if (!view || !view->selectionModel())
        return;
    const auto hit = view->indexAt(pos);
    if (hit.isValid() && !view->selectionModel()->isSelected(hit))
        view->selectionModel()->setCurrentIndex(hit, QItemSelectionModel::ClearAndSelect |
                                                     QItemSelectionModel::Rows);
    const auto& files = m_fileModel.files();
    auto selected = view->selectionModel()->selectedRows();
    if (selected.isEmpty() && view->currentIndex().isValid())
        selected.append(view->currentIndex());

    // Активный узел (первая выделенная строка либо текущая) — для Open/Rename/CopyPath.
    // Опора на колонку Want (якорь дерева): там хранится поддерево.
    QPersistentModelIndex active;
    if (!selected.isEmpty())
        active = selected.first().siblingAtColumn(FileTreeModel::Want);

    // Файлы для приоритета/skip: вся выделенная область (папки — все вложенные файлы).
    QVector<int> fileIds;
    for (const QModelIndex& idx : selected)
        for (int row : m_fileTree->sourceRows(idx))
            if (row >= 0 && row < files.size() && !fileIds.contains(files.at(row).index))
                fileIds.append(files.at(row).index);

    const bool canRow = active.isValid();
    const bool canIds = !fileIds.isEmpty();

    // Референс (pmFiles): Open, Open containing folder, Rename, Copy file path,
    // приоритет high/normal/low, Don't download, Setup columns.
    auto* menu = new QMenu(this);
    auto* aOpen = menu->addAction(tr("Open"));
    aOpen->setEnabled(canRow);
    auto* aOpenFolder = menu->addAction(tgui::theme::icon(Icon::Folder), tr("Open containing folder"));
    aOpenFolder->setEnabled(canRow);
    auto* aRename = menu->addAction(tr("Rename"));
    aRename->setEnabled(canRow);
    auto* aCopyPath = menu->addAction(tgui::theme::icon(Icon::Copy), tr("Copy file path to clipboard"));
    aCopyPath->setEnabled(canRow);
    menu->addSeparator();
    auto* aHigh = menu->addAction(tr("High priority"));
    auto* aNormal = menu->addAction(tr("Normal priority"));
    auto* aLow = menu->addAction(tr("Low priority"));
    aHigh->setEnabled(canIds);
    aNormal->setEnabled(canIds);
    aLow->setEnabled(canIds);
    menu->addSeparator();
    auto* aSkip = menu->addAction(tr("Don't download"));
    aSkip->setEnabled(canIds);
    menu->addSeparator();
    auto* expand = menu->addAction(tr("Expand all"));
    auto* collapse = menu->addAction(tr("Collapse all"));
    connect(expand, &QAction::triggered, view, &QTreeView::expandAll);
    connect(collapse, &QAction::triggered, view, &QTreeView::collapseAll);
    menu->addSeparator();
    auto* aCols = menu->addAction(tr("Setup columns..."));

    const QPoint global = view->mapToGlobal(pos);
    const auto isCurrent = [this, rpc = QPointer(m_rpc.get()), id = m_currentId] {
        return rpc && rpc == m_rpc.get() && id == m_currentId;
    };
    const auto connectFileAction = [this, active, isCurrent](QAction* action, auto handler) {
        connect(action, &QAction::triggered, this, [this, active, isCurrent, handler] {
            if (isCurrent() && active.isValid())
                (this->*handler)(active);
        });
    };
    connectFileAction(aOpen, &MainWindow::onFilesOpen);
    connectFileAction(aOpenFolder, &MainWindow::onFilesOpenFolder);
    connectFileAction(aRename, &MainWindow::onFilesRename);
    connectFileAction(aCopyPath, &MainWindow::onFilesCopyPath);
    const auto connectPriority = [this, fileIds, isCurrent](QAction* action, int priority) {
        connect(action, &QAction::triggered, this, [this, fileIds, isCurrent, priority] {
            if (isCurrent())
                onFilePriority(fileIds, priority);
        });
    };
    connectPriority(aHigh, 1);
    connectPriority(aNormal, 0);
    connectPriority(aLow, -1);
    connect(aSkip, &QAction::triggered, this, [this, fileIds, isCurrent] {
        if (isCurrent())
            setFilesDownload(fileIds, false);
    });
    QObject::connect(aCols, &QAction::triggered, this, [this, view] {
        const int count = FileTreeModel::ColumnCount;
        QStringList titles;
        if (auto* model = view->model())
            for (int i = 0; i < count; ++i)
                titles << model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
        const ColumnLayout cur = ColumnLayout::fromSettings(count, QStringLiteral("files/columns"));
        ColSetupDialog dlg(cur, titles, this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const ColumnLayout nl = dlg.layout();
        nl.applyToView(view);
        nl.saveToSettings(QStringLiteral("files/columns"));
        statusBar()->showMessage(tr("Column layout updated."), 3000);
    });
    menu->exec(global);
    menu->deleteLater();
}

void MainWindow::onPeersContextMenu(const QPoint& pos)
{
    auto* view = qobject_cast<QTableView*>(sender());
    if (!view)
        return;

    auto* menu = new QMenu(this);
    QSettings s;

    // Resolve Host (toggle — reverse DNS, persist state)
    auto* aHost = menu->addAction(tr("Resolve Host"));
    aHost->setCheckable(true);
    aHost->setChecked(s.value("peers/resolveHost", false).toBool());
    QObject::connect(aHost, &QAction::triggered, this, [this](bool on) {
        QSettings().setValue("peers/resolveHost", on);
        m_peerModel.setResolveHost(on); // асинхронный reverse DNS + обновление колонки
    });

    // Resolve Country (toggle видимости колонки; при вкл. без БД — скачать)
    auto* aCountry = menu->addAction(tr("Resolve Country"));
    aCountry->setCheckable(true);
    const bool countryOn = !view->isColumnHidden(PeerListModel::ColCountry);
    aCountry->setChecked(countryOn);
    QObject::connect(aCountry, &QAction::triggered, this, [this, view](bool on) {
        view->setColumnHidden(PeerListModel::ColCountry, !on);
        if (on && !m_geoip.isLoaded()) {
            // БД не загружена — предложить скачать.
            onUpdateGeoIp();
        }
    });

    // Show Country Flag
    auto* aFlag = menu->addAction(tr("Show Country Flag"));
    aFlag->setCheckable(true);
    aFlag->setChecked(s.value("peers/showFlag", false).toBool());
    aFlag->setEnabled(m_geoip.isLoaded()); // активна только с загруженной БД
    QObject::connect(aFlag, &QAction::triggered, this, [this](bool on) {
        if (on && !ensureFlagImages(true)) {
            QMessageBox::warning(this, tr("Show Country Flag"),
                                 tr("Unable to load country flag images."));
            return;
        }
        QSettings().setValue("peers/showFlag", on);
        m_peerModel.setShowFlags(on);
    });

    // Setup Columns
    menu->addSeparator();
    auto* aCols = menu->addAction(tr("Setup Columns"));
    QObject::connect(aCols, &QAction::triggered, this, [this, view] {
        const int count = PeerListModel::ColumnCount;
        QStringList titles;
        for (int i = 0; i < count; ++i)
            titles << view->model()->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
        const ColumnLayout cur = ColumnLayout::fromView(view, count);
        ColSetupDialog dlg(cur, titles, this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const ColumnLayout nl = dlg.layout();
        nl.applyToView(view);
        nl.saveToSettings(QStringLiteral("peers/columns"));
        statusBar()->showMessage(tr("Column layout updated."), 3000);
    });

    menu->exec(view->mapToGlobal(pos));
    menu->deleteLater();
}

void MainWindow::onFilePriority(const QVector<int>& fileIds, int priority)
{
    if (!m_rpc || m_currentId < 0 || fileIds.isEmpty())
        return;
    m_rpc->setFilePriority(m_currentId, fileIds, priority,
                           afterTorrentChange("torrent-set", RefreshAfter::Files, m_currentId));
}

/** Изменяет выбор указанных файлов и обновляет вкладку после ответа демона. */
void MainWindow::setFilesDownload(const QVector<int>& fileIds, bool wanted)
{
    if (!m_rpc || m_currentId < 0 || fileIds.isEmpty())
        return;
    auto completed = afterTorrentChange("torrent-set", RefreshAfter::Files, m_currentId,
                                        wanted ? tr("Marked for download") : tr("Marked to skip"));
    if (wanted)
        m_rpc->setFilePriority(m_currentId, fileIds, 0, std::move(completed));
    else
        m_rpc->setFilesWanted(m_currentId, {}, fileIds, std::move(completed));
}

// Чекбокс Want (файл/папка) → files-wanted / files-unwanted для затронутого поддерева.
void MainWindow::onFilesWantToggled(const QVector<int>& rows, bool wanted)
{
    if (!m_rpc || m_currentId < 0 || rows.isEmpty())
        return;
    const auto& files = m_fileModel.files();
    QVector<int> ids;
    for (int r : rows)
        if (r >= 0 && r < files.size() && files.at(r).wanted != wanted)
            ids.append(files.at(r).index);
    setFilesDownload(ids, wanted);
}

// downloadDir текущего торрента из основной модели (пусто, если не найден).
QString MainWindow::currentDownloadDir() const
{
    for (const auto& x : m_model.torrents())
        if (x.id == m_currentId)
            return x.downloadDir.value_or(QString());
    return QString();
}

// «Открыть» (референс acOpenFile): папка — открыть; файл — запустить, если готов.
void MainWindow::onFilesOpen(const QModelIndex& index)
{
    if (m_currentId < 0 || !index.isValid())
        return;
    if (m_fileTree->isFolder(index)) {
        const QString dir = currentDownloadDir();
        if (!dir.isEmpty())
            openLocalPath(dir + QStringLiteral("/") +
                          index.data(FileTreeModel::PathRole).toString(), /*selectFile=*/false);
        return;
    }
    const auto rows = m_fileTree->sourceRows(index);
    if (!rows.isEmpty())
        onFilesDoubleClicked(rows.first());
}

// «Открыть содержащую папку» (референс acOpenContainingFolder).
void MainWindow::onFilesOpenFolder(const QModelIndex& index)
{
    if (m_currentId < 0 || !index.isValid())
        return;
    const QString dir = currentDownloadDir();
    if (dir.isEmpty())
        return;
    const QString rel = index.data(FileTreeModel::PathRole).toString();
    // Файл — открыть его каталог (selectFile=true → родитель); папка — открыть её.
    openLocalPath(dir + QStringLiteral("/") + rel,
                  /*selectFile=*/!m_fileTree->isFolder(index));
}

// «Скопировать путь файла» (референс acCopyPath): «"полный путь"».
void MainWindow::onFilesCopyPath(const QModelIndex& index)
{
    if (m_currentId < 0 || !index.isValid())
        return;
    const QString dir = currentDownloadDir();
    if (dir.isEmpty())
        return;
    const QString full = dir + QStringLiteral("/") +
                         index.data(FileTreeModel::PathRole).toString();
    QApplication::clipboard()->setText(QStringLiteral("\"%1\"").arg(full));
    statusBar()->showMessage(tr("File path copied"), 3000);
}

// «Переименовать» (референс acRename): новый базовый имя файла (каталог не меняется).
void MainWindow::onFilesRename(const QModelIndex& index)
{
    if (!m_rpc || m_currentId < 0 || !index.isValid())
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const int id = m_currentId;
    const QString path = index.data(FileTreeModel::PathRole).toString();
    if (path.isEmpty())
        return;
    const QString base = path.section(QLatin1Char('/'), -1);
    bool ok = false;
    const auto newName = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, base, &ok).trimmed();
    if (!ok || !rpc || newName.isEmpty() || newName == base)
        return;
    if (newName == "." || newName == ".." || newName.contains('/') || newName.contains('\\')) {
        QMessageBox::warning(this, tr("Rename"), tr("Specify a valid file or folder name."));
        return;
    }
    rpc->renamePath(id, path, newName, afterTorrentChange("torrent-rename-path", RefreshAfter::Files, id));
}

void MainWindow::onTrackersContextMenu(const QPoint& pos)
{
    if (!m_rpc || m_currentId < 0)
        return;
    auto* view = qobject_cast<QTableView*>(sender());
    if (!view || !view->selectionModel())
        return;
    const auto hit = view->indexAt(pos);
    if (hit.isValid() && !view->selectionModel()->isSelected(hit))
        view->selectionModel()->setCurrentIndex(hit, QItemSelectionModel::ClearAndSelect |
                                                     QItemSelectionModel::Rows);
    const auto selected = view->selectionModel()->selectedRows();
    const auto& trackers = m_trackerModel.trackers();
    bool hasSel = false;
    int trackerId = -1;
    QString announce;
    if (!selected.isEmpty()) {
        const int row = selected.first().row();
        if (row >= 0 && row < trackers.size()) {
            trackerId = trackers.at(row).id;
            hasSel = trackerId >= 0;
            announce = trackers.at(row).announce;
        }
    }

    auto* menu = new QMenu(this);
    auto* aAdd = menu->addAction(tr("Add tracker…"));
    auto* aEdit = menu->addAction(tr("Edit tracker…"));
    auto* aRemove = menu->addAction(tr("Remove tracker…"));
    auto* aAdvanced = menu->addAction(tr("Edit tracker list…"));
    menu->addSeparator();
    auto* aColumns = menu->addAction(tr("Setup columns…"));
    aEdit->setEnabled(hasSel);
    aRemove->setEnabled(hasSel);

    const QPoint global = view->mapToGlobal(pos);
    const auto isCurrent = [this, rpc = QPointer(m_rpc.get()), id = m_currentId] {
        return rpc && rpc == m_rpc.get() && id == m_currentId;
    };
    connect(aAdd, &QAction::triggered, this, [this, isCurrent] {
        if (isCurrent())
            onAddTracker();
    });
    connect(aEdit, &QAction::triggered, this, [this, trackerId, announce, isCurrent] {
        if (isCurrent())
            onEditTracker(trackerId, announce);
    });
    connect(aRemove, &QAction::triggered, this, [this, trackerId, announce, isCurrent] {
        if (isCurrent())
            onRemoveTracker(trackerId, announce);
    });
    connect(aAdvanced, &QAction::triggered, this, [this, isCurrent, id = m_currentId] {
        if (isCurrent())
            showProperties(id, 1);
    });
    connect(aColumns, &QAction::triggered, this, [this, view] {
        const int count = TrackerListModel::ColumnCount;
        QStringList titles;
        for (int i = 0; i < count; ++i)
            titles << view->model()->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
        const ColumnLayout cur = ColumnLayout::fromView(view, count);
        ColSetupDialog dlg(cur, titles, this);
        if (dlg.exec() != QDialog::Accepted)
            return;
        const ColumnLayout nl = dlg.layout();
        nl.applyToView(view);
        nl.saveToSettings(QStringLiteral("trackers/columns"));
        statusBar()->showMessage(tr("Column layout updated."), 3000);
    });
    menu->exec(global);
    menu->deleteLater();
}

/** Обрабатывает Delete в таблице трекеров до глобальной команды удаления торрента. */
bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_statusStrip && event->type() == QEvent::Resize)
        updateStatusBarPresentation();
    if (watched == m_tabs && event->type() == QEvent::Resize)
        updateInfoTitle();
    if (watched == m_trackersView &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Delete) {
            event->accept();
            if (event->type() == QEvent::KeyPress && key->modifiers() == Qt::NoModifier &&
                !key->isAutoRepeat()) {
                const auto index = m_trackersView->currentIndex();
                const auto& trackers = m_trackerModel.trackers();
                if (index.isValid() && index.row() < trackers.size()) {
                    const auto tracker = trackers.at(index.row());
                    onRemoveTracker(tracker.id, tracker.announce);
                }
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onAddTracker()
{
    if (!m_rpc || m_currentId < 0)
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const int id = m_currentId;
    bool ok = false;
    const QString url = QInputDialog::getText(
                           this, tr("Add tracker"), tr("Tracker URL:"),
                           QLineEdit::Normal, QString(), &ok)
                           .trimmed();
    if (!ok || !rpc)
        return;
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("Add tracker"), tr("No tracker URL was specified."));
        return;
    }
    rpc->addTracker(id, url, afterTorrentChange("torrent-set", RefreshAfter::Trackers, id));
}

void MainWindow::onEditTracker(int trackerId, const QString& announce)
{
    if (!m_rpc || m_currentId < 0 || trackerId < 0)
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const int id = m_currentId;
    bool ok = false;
    const QString url = QInputDialog::getText(
                           this, tr("Edit tracker"), tr("New tracker URL:"),
                           QLineEdit::Normal, announce, &ok)
                           .trimmed();
    if (!ok || !rpc)
        return;
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("Edit tracker"), tr("No tracker URL was specified."));
        return;
    }
    rpc->replaceTracker(id, trackerId, url, afterTorrentChange("torrent-set", RefreshAfter::Trackers, id));
}

void MainWindow::onRemoveTracker(int trackerId, const QString& announce)
{
    if (!m_rpc || m_currentId < 0 || trackerId < 0)
        return;
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    const int id = m_currentId;
    const auto ret = QMessageBox::question(
        this, tr("Remove tracker"), tr("Remove tracker “%1”?").arg(announce));
    if (ret != QMessageBox::Yes || !rpc)
        return;
    rpc->removeTracker(id, trackerId, afterTorrentChange("torrent-set", RefreshAfter::Trackers, id));
}

/** Завершает элемент очереди и перечитывает список на том же подключении. */
void MainWindow::finishAdd(const QPointer<tgui::RpcClient>& rpc, int selectedId)
{
    if (!rpc || rpc != m_rpc.get())
        return;
    m_addingTorrent = false;
    if (!m_connected)
        return;
    if (selectedId >= 0)
        m_addSelectionId = selectedId;
    rpc->getTorrents();
    addNextTorrent();
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
            startAdd(p);
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
                startAdd(p);
            }
        }
    }
    event->acceptProposedAction();
}

/** Наполняет меню подтверждёнными лимитами текущего демона и значениями его профиля. */
void MainWindow::addSpeedLimitMenu(QMenu* parent, bool download)
{
    parent->addMenu(createSpeedLimitMenu(parent, download));
}

QMenu* MainWindow::createSpeedLimitMenu(QWidget* parent, bool download)
{
    auto* menu = new QMenu(download ? tr("Max download speed") : tr("Max upload speed"), parent);
    menu->setObjectName(download ? QStringLiteral("quickDownloadSpeed") : QStringLiteral("quickUploadSpeed"));
    menu->setEnabled(m_connected);
    m_speedLimitMenus.append(menu);
    connect(menu, &QMenu::aboutToShow, this, [this, menu, download] {
        menu->clear();
        if (!m_rpc || !m_connected)
            return;
        const QPointer<tgui::RpcClient> rpc(m_rpc.get());
        const auto profile = m_connectionProfile;
        const auto& session = rpc->sessionInfo();
        const bool enabled = download ? session.speedLimitDownEnabled : session.speedLimitUpEnabled;
        const int current = enabled ? (download ? session.speedLimitDown : session.speedLimitUp) : -1;
        auto speeds = ProfilePreferences(profile).speedPresets(download);
        if (current >= 0 && !speeds.contains(current)) {
            speeds.append(current);
            std::sort(speeds.begin(), speeds.end());
        }
        const auto setLimit = [this, rpc, profile, download](int speed) {
            if (!rpc || rpc != m_rpc.get() || !m_connected || profile != m_connectionProfile)
                return;
            const auto key = download ? QStringLiteral("speed-limit-down") : QStringLiteral("speed-limit-up");
            QJsonObject params{{key + "-enabled", speed >= 0}};
            if (speed >= 0)
                params.insert(key, speed);
            if (rpc->sessionInfo().altSpeedEnabled)
                params.insert("alt-speed-enabled", false);
            rpc->setSession(params);
        };
        const auto add = [this, menu, setLimit, current, alternate = session.altSpeedEnabled](
                             const QString& caption, int speed) {
            auto* action = menu->addAction(caption);
            action->setCheckable(true);
            action->setChecked(!alternate && speed == current);
            action->setData(speed);
            connect(action, &QAction::triggered, this, [setLimit, speed] { setLimit(speed); });
        };
        add(tr("Unlimited"), -1);
        if (!speeds.isEmpty())
            menu->addSeparator();
        for (auto it = speeds.crbegin(); it != speeds.crend(); ++it)
            add(tr("%1 KB/s").arg(*it), *it);
        menu->addSeparator();
        auto* custom = menu->addAction(tr("Other…"));
        connect(custom, &QAction::triggered, this, [this, rpc, profile, setLimit, download, current] {
            if (!rpc || rpc != m_rpc.get() || !m_connected || profile != m_connectionProfile)
                return;
            bool accepted = false;
            const int speed = QInputDialog::getInt(this, download ? tr("Max download speed") : tr("Max upload speed"),
                                                   tr("Limit (KB/s):"), qMax(0, current), 0,
                                                   std::numeric_limits<int>::max(), 1, &accepted);
            if (accepted)
                setLimit(speed);
        });
    });
    return menu;
}

void MainWindow::createTray()
{
    using tgui::theme::Icon;
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    // W07: не создавать иконку, если в настройках выключен «always show tray icon».
    if (!QSettings().value("ui/trayIcon", true).toBool())
        return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(tgui::theme::icon(tgui::theme::Icon::App));
    m_tray->setToolTip(QStringLiteral("transmissionGUI"));
    connect(this, &MainWindow::downloadNotification, m_tray, [this](const QString& message) {
        if (m_tray->isVisible())
            m_tray->showMessage(tr("Download completed"), message, QSystemTrayIcon::Information);
    });

    auto* menu = new QMenu(this);
    auto* actShow = menu->addAction(tr("Show"));
    auto* actStartAll = menu->addAction(tgui::theme::icon(Icon::Start), tr("Start all"));
    auto* actStopAll = menu->addAction(tgui::theme::icon(Icon::Pause), tr("Pause all"));
    addSpeedLimitMenu(menu, true);
    addSpeedLimitMenu(menu, false);
    menu->addSeparator();
    auto* actRefresh = menu->addAction(tgui::theme::icon(Icon::Refresh), tr("Refresh"));
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
    saveWindowLayout();
    // W07: «close to tray» из настроек; иначе — выход.
    const bool closeToTray = QSettings().value("ui/closeToTray", true).toBool();
    if (closeToTray && m_tray && m_tray->isVisible()) {
        event->ignore();
        hide();
        return;
    }
    QMainWindow::closeEvent(event);
}

/** Сохраняет геометрию окна и пропорции основной панели. */
void MainWindow::saveWindowLayout()
{
    if (m_settingsImported)
        return;
    QSettings settings;
    settings.setValue("ui/windowGeometry", saveGeometry());
    settings.setValue("ui/mainSplitter", m_mainSplitter->saveState());
}

bool MainWindow::event(QEvent* event)
{
    const bool handled = QMainWindow::event(event);
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
        event->type() == QEvent::WindowStateChange)
        updateRefreshSchedule();
    return handled;
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
