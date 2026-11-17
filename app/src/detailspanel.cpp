#include "detailspanel.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTableView>
#include <QTreeView>

#include <algorithm>
#include <utility>

#include "colsetup.h"
#include "connectioncontroller.h"
#include "delegates.h"
#include "flagimageloader.h"
#include "geoipupdater.h"
#include "propsform.h"
#include "statsform.h"
#include "theme.h"
#include "statusbarcontroller.h"
#include "torrentactions.h"
#include "torrentmodel.h"
#include "tgui/util.h"

using RefreshAfter = TorrentActions::RefreshAfter;

DetailsPanel::DetailsPanel(QWidget* parent, const ConnectionController& connection, const TorrentModel& model,
                           const tgui::SessionInfo& session, MapPath map, OpenPath open, SelectionCount count)
    : QTabWidget(parent), m_connection(connection), m_model(model), m_sessionInfo(session),
      mappedLocalPath(std::move(map)), openLocalPath(std::move(open)), m_selectionCount(std::move(count))
{
    // Вкладки деталей выбранного торрента: Файлы / Пиры / Трекеры.
    auto makeTable = [this](QAbstractTableModel* tableModel) {
        auto* view = new QTableView(this);
        view->setModel(tableModel);
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
    connect(&m_fileModel, &QAbstractItemModel::dataChanged, this, [this] {
        m_fileTree->setFiles(m_fileModel.files());
    });
    connect(&m_fileModel, &FileListModel::wantChanged, this, &DetailsPanel::onFileWantToggled);
    // Чекбокс Want: модель эмитит wantToggled(rows, wanted) → единый RPC.
    connect(m_fileTree, &FileTreeModel::wantToggled, this, &DetailsPanel::onFilesWantToggled);
    // Контекстное меню вкладки «Файлы» (референс — pmFiles): приоритет high/normal/low
    // и want/skip для выделенных строк.
    filesView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(filesView, &QWidget::customContextMenuRequested, this, &DetailsPanel::onFilesContextMenu);

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
    connect(&m_peerModel, &QAbstractItemModel::modelReset, this, &DetailsPanel::updateInfoTitle);
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
    connect(peersView, &QWidget::customContextMenuRequested, this, &DetailsPanel::onPeersContextMenu);
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
            this, &DetailsPanel::onTrackersContextMenu);
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

    this->setObjectName(QStringLiteral("infoPanel"));
    this->setIconSize(QSize(14, 14));
    this->setUsesScrollButtons(true);
    m_infoTitle = new QLabel(this);
    m_infoTitle->setObjectName(QStringLiteral("infoTorrentTitle"));
    m_infoTitle->setTextFormat(Qt::PlainText);
    this->setCornerWidget(m_infoTitle, Qt::TopRightCorner);
    // Названия вкладок 1:1 с референсом (tabControl: General/Trackers/Peers/
    // Files/Statistics).
    this->addTab(m_general, tgui::theme::icon(tgui::theme::Icon::TabGeneral),
                   tr("General"));
    this->addTab(trackersView, tgui::theme::icon(tgui::theme::Icon::TabTrackers),
                   tr("Trackers"));
    this->addTab(peersView, tgui::theme::icon(tgui::theme::Icon::TabPeers),
                   tr("Peers"));
    this->addTab(filesView, tgui::theme::icon(tgui::theme::Icon::TabFiles),
                   tr("Files"));
    this->addTab(m_statsForm, tgui::theme::icon(tgui::theme::Icon::TabStatistics),
                   tr("Statistics"));
    this->setMinimumHeight(80);
    // При переключении на «Файлы» проценты могли устареть (проверка или
    // загрузка шли, пока открыта другая вкладка) — запрашиваем детали сразу.
    connect(this, &QTabWidget::currentChanged, this,
            [this](int) { refreshFilesDetails(); });
}

void DetailsPanel::setServices(TorrentActions& actions, GeoIpUpdater& geoIp, FlagImageLoader& flags,
                               StatusBarController& status)
{
    m_actions = &actions;
    m_geoIp = &geoIp;
    m_flags = &flags;
    m_status = &status;
}

void DetailsPanel::suspend()
{
    ++m_generalRequest;
    m_generalLoading = false;
    ++m_filesRequest;
    m_filesLoading = false;
    m_filesSnapshot.invalidate();
}

void DetailsPanel::clear()
{
    resetFiles();
    m_peerModel.setPeers({});
    m_trackerModel.setTrackers({});
    m_general->clear();
    ++m_generalRequest;
    m_generalLoading = false;
    m_generalDetails.reset();
    m_statsForm->clear();
    m_currentId = -1;
}

void DetailsPanel::resizeEvent(QResizeEvent* event)
{
    QTabWidget::resizeEvent(event);
    updateInfoTitle();
}

void DetailsPanel::refreshFilesAfterAction(bool ok)
{
    if (ok)
        requestFiles(true);
    else
        m_fileTree->setFiles(m_fileModel.files());
}

void DetailsPanel::applySnapshot(int nextId, bool reconnecting)
{
    const int prevId = std::exchange(m_currentId, nextId);

    if (nextId != prevId) {
        ++m_generalRequest;
        m_generalLoading = false;
        m_generalDetails.reset();
        resetFiles();
        m_peerModel.setPeers({});
        m_trackerModel.setTrackers({});
    }

    if (const auto* torrent = m_model.torrentById(nextId)) {
        const auto& currentFiles = m_fileModel.files();
        const int status = torrent->status.value_or(-1);
        if (!currentFiles.isEmpty() &&
            (status == static_cast<int>(tgui::TorrentStatus::Check) ||
             std::ranges::any_of(currentFiles, [](const tgui::FileStat& file) { return file.checkProgress >= 0.0; }))) {
            auto files = currentFiles;
            tgui::applyCheckProgress(files, status, torrent->recheckProgress,
                                     torrent->pieceCount.value_or(m_filePieceCount));
            m_fileModel.setFiles(files);
        }
        updateGeneralTorrent(*torrent);
    } else {
        m_general->clear();
    }
    if (nextId != prevId || reconnecting)
        refreshFilesDetails();
}

void DetailsPanel::setCurrentTorrent(int id)
{
    if (id == m_currentId)
        return;
    m_currentId = id;
    ++m_generalRequest;
    m_generalLoading = false;
    m_generalDetails.reset();
    resetFiles();
    m_peerModel.setPeers({});
    m_trackerModel.setTrackers({});
    // «General» обновляем из модели сразу — до ответа RPC.
    if (id >= 0) {
        for (const auto& t : m_model.torrents()) {
            if (t.id == id) {
                updateGeneralTorrent(t);
                break;
            }
        }
    } else {
        m_general->clear();
    }
    if (id >= 0 && m_connection.client())
        refreshFilesDetails();
}

/** Обновляет заголовок, используя готовое количество выбранных строк. */
void DetailsPanel::updateInfoTitleWithCount(qsizetype count)
{
    const auto* torrent = m_general ? m_general->torrent() : nullptr;
    if (m_peerEmpty)
        m_peerEmpty->setVisible(m_currentId >= 0 && m_peerModel.rowCount() == 0);
    if (!m_infoTitle)
        return;
    QString title = torrent ? torrent->name.value_or(QString()) : QString();
    if (!title.isEmpty()) {
        if (count > 1)
            title += QLatin1Char(' ') + tr("(%1 more)").arg(count - 1); // без ведущего пробела в строке (I18N-03)
    }
    const int textWidth = qMax(0, qMin(360, this->width() * 45 / 100) - 16);
    m_infoTitle->setText(m_infoTitle->fontMetrics().elidedText(title, Qt::ElideRight, textWidth));
    m_infoTitle->setToolTip(title);
}

void DetailsPanel::refreshFilesDetails()
{
    // Прогресс файлов меняется между опросами (проверка торрента, загрузка),
    // поэтому видимую вкладку «Файлы» обновляем вместе со списком торрентов.
    // Иначе остаются проценты, полученные в момент выбора торрента: при
    // начатой проверке они так и висят на нуле.
    // «Пиры» — то же самое: во время скачивания набор подключённых пиров
    // постоянно меняется, а при выборе торрента пиров ещё могло не быть —
    // без периодического запроса вкладка остаётся пустой до переподключения
    // (старый transgui перезапрашивал детали каждый цикл обновления).
    if (!m_connection.client() || m_currentId < 0 || this->isHidden())
        return;
    if (this->currentWidget() == m_general)
        refreshGeneralDetails();
    if (m_filesView && this->currentWidget() == m_filesView)
        requestFiles();
    if (m_peersView && this->currentWidget() == m_peersView)
        m_connection.client()->getPeers(m_currentId);
    if (m_trackersView && this->currentWidget() == m_trackersView)
        m_connection.client()->getTrackers(m_currentId);
}

void DetailsPanel::onTorrentDetailsReceived(const tgui::TorrentDetails& details)
{
    // Вкладка «Файлы» для выбранного торрента.
    if (details.id != m_currentId)
        return;
    m_filePieceCount = details.pieceCount;
    // Прогресс проверки берём из текущего снимка списка торрентов (а не из
    // ответа деталей): так список и вкладка показывают одно и то же число.
    QVector<tgui::FileStat> files = details.files;
    for (const auto& t : m_model.torrents()) {
        if (t.id != details.id)
            continue;
        tgui::applyCheckProgress(files, t.status.value_or(-1),
                                 t.recheckProgress,
                                 t.pieceCount.value_or(details.pieceCount));
        break;
    }
    m_fileModel.setFiles(files);
}

void DetailsPanel::onPeersReceived(int id, const QVector<tgui::Peer>& peers)
{
    if (id != m_currentId)
        return;
    m_peerModel.setPeers(peers);
}

void DetailsPanel::onTrackersReceived(int id, const QVector<tgui::TrackerStat>& trackers)
{
    if (id != m_currentId)
        return;
    m_trackerModel.setTrackers(trackers);
}

/** Обновляет подпись выбранного торрента справа от вкладок. */
void DetailsPanel::updateInfoTitle()
{
    updateInfoTitleWithCount(m_selectionCount());
}

/** Обновляет тяжёлые поля видимой вкладки «Общие», привязывая ответ к выбору. */
void DetailsPanel::refreshGeneralDetails()
{
    if (m_generalLoading || !m_connection.client() || !m_connection.isConnected() || m_currentId < 0 ||
        this->isHidden() || this->currentWidget() != m_general)
        return;
    m_generalLoading = true;
    const auto request = ++m_generalRequest;
    const QPointer rpc(m_connection.client());
    m_connection.client()->getTorrentProperties(m_currentId, [this, rpc, request](const auto& details) {
        if (!rpc || rpc != m_connection.client() || request != m_generalRequest)
            return;
        m_generalLoading = false;
        if (!details || details->id != m_currentId || !m_connection.isConnected())
            return;
        if (const auto* torrent = m_model.torrentById(details->id)) {
            if (details->hashString && torrent->hashString && details->hashString != torrent->hashString)
                return;
            m_generalDetails = details;
            updateGeneralTorrent(*torrent);
        }
    });
}

/** Показывает свежие поля списка вместе с деталями того же торрента. */
void DetailsPanel::updateGeneralTorrent(const tgui::Torrent& torrent)
{
    auto combined = torrent;
    if (m_generalDetails && m_generalDetails->id == torrent.id &&
        (!m_generalDetails->hashString || !torrent.hashString || m_generalDetails->hashString == torrent.hashString)) {
        const auto& detail = *m_generalDetails;
        combined.comment = detail.comment;
        combined.creator = detail.creator;
        combined.dateCreated = detail.dateCreated;
        combined.pieceCount = detail.pieceCount;
        combined.pieceSize = detail.pieceSize;
        combined.haveValid = detail.haveValid;
        combined.pieces = detail.pieces;
        combined.magnetLink = detail.magnetLink;
        combined.maxConnectedPeers = detail.maxConnectedPeers;
        combined.corruptEver = detail.corruptEver;
        if (!detail.trackerStats.isEmpty())
            combined.trackerStats = detail.trackerStats;
    }
    m_general->setTorrent(combined);
    if (m_sessionInfo.rpcVersion > 0)
        m_general->setSession(m_sessionInfo);
}

void DetailsPanel::resetFiles()
{
    ++m_filesRequest;
    m_filesLoading = false;
    m_filesSnapshot.invalidate();
    m_filePieceCount = 0;
    m_fileModel.setFiles({});
}

void DetailsPanel::requestFiles(bool forceFull)
{
    if (!m_connection.client() || !m_connection.isConnected() || m_currentId < 0 || (m_filesLoading && !forceFull))
        return;
    m_filesLoading = true;
    const auto request = ++m_filesRequest;
    const auto id = m_currentId;
    const QPointer rpc(m_connection.client());
    const auto current = [this, rpc, request, id] {
        return rpc && rpc == m_connection.client() && m_connection.isConnected() && request == m_filesRequest && id == m_currentId;
    };

    // Редкий полный снимок учитывает переименования, сделанные другим клиентом.
    if (forceFull || !m_filesSnapshot.isValid() || m_filesSnapshot.hasExpired(30000)) {
        m_connection.client()->getTorrentDetails(id, [this, current](const auto& details) {
            if (!current())
                return;
            m_filesLoading = false;
            if (!details)
                return;
            m_filesSnapshot.start();
            onTorrentDetailsReceived(*details);
        });
        return;
    }
    m_connection.client()->getFileStats(id, [this, current, id](const auto& stats) {
        if (!current())
            return;
        m_filesLoading = false;
        auto files = m_fileModel.files();
        if (!stats || stats->size() != files.size()) {
            requestFiles(true);
            return;
        }
        for (qsizetype row = 0; row < files.size(); ++row) {
            auto& file = files[row];
            const auto& progress = stats->at(row);
            file.completion = file.size > 0.0 ? progress.bytesCompleted / file.size : 0.0;
            file.priority = progress.priority;
            file.wanted = progress.wanted;
        }
        if (const auto* torrent = m_model.torrentById(id))
            tgui::applyCheckProgress(files, torrent->status.value_or(-1), torrent->recheckProgress,
                                     torrent->pieceCount.value_or(m_filePieceCount));
        m_fileModel.setFiles(files);
    });
}

void DetailsPanel::onFilesDoubleClicked(int row)
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
    const QString path = dir + QStringLiteral("/") + rel;
    // Не скачанный до конца файл не имеет смысла запускать — открываем каталог.
    openLocalPath(path, /*selectFile=*/files.at(row).completion < 1.0);
}

void DetailsPanel::onFileWantToggled(int fileId, bool wanted)
{
    setFilesDownload({fileId}, wanted);
}

void DetailsPanel::onFilesContextMenu(const QPoint& pos)
{
    using tgui::theme::Icon;
    if (!m_connection.client() || m_currentId < 0)
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
    auto* aCols = menu->addAction(tr("Setup columns…")); // единое многоточие (I18N-03)

    const QPoint global = view->mapToGlobal(pos);
    const auto isCurrent = [this, rpc = QPointer(m_connection.client()), id = m_currentId] {
        return rpc && rpc == m_connection.client() && id == m_currentId;
    };
    const auto connectFileAction = [this, active, isCurrent](QAction* action, auto handler) {
        connect(action, &QAction::triggered, this, [this, active, isCurrent, handler] {
            if (isCurrent() && active.isValid())
                (this->*handler)(active);
        });
    };
    connectFileAction(aOpen, &DetailsPanel::onFilesOpen);
    connectFileAction(aOpenFolder, &DetailsPanel::onFilesOpenFolder);
    connectFileAction(aRename, &DetailsPanel::onFilesRename);
    connectFileAction(aCopyPath, &DetailsPanel::onFilesCopyPath);
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
        m_status->showMessage(tr("Column layout updated."), 3000);
    });
    menu->exec(global);
    menu->deleteLater();
}

void DetailsPanel::onPeersContextMenu(const QPoint& pos)
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
        if (on && !m_geoIp->isLoaded()) {
            // БД не загружена — предложить скачать.
            m_geoIp->onUpdateGeoIp();
        }
    });

    // Show Country Flag
    auto* aFlag = menu->addAction(tr("Show Country Flag"));
    aFlag->setCheckable(true);
    aFlag->setChecked(s.value("peers/showFlag", false).toBool());
    aFlag->setEnabled(m_geoIp->isLoaded()); // активна только с загруженной БД
    QObject::connect(aFlag, &QAction::triggered, this, [this](bool on) {
        QSettings().setValue("peers/showFlag", on);
        m_peerModel.setShowFlags(on);
        if (on)
            m_flags->loadFlagImages(true, true);
    });

    // Setup Columns
    menu->addSeparator();
    auto* aCols = menu->addAction(tr("Setup columns…")); // регистр и многоточие как в остальных местах (I18N-03)
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
        m_status->showMessage(tr("Column layout updated."), 3000);
    });

    menu->exec(view->mapToGlobal(pos));
    menu->deleteLater();
}

void DetailsPanel::onFilePriority(const QVector<int>& fileIds, int priority)
{
    if (!m_connection.client() || m_currentId < 0 || fileIds.isEmpty())
        return;
    m_connection.client()->setFilePriority(m_currentId, fileIds, priority,
                           m_actions->afterTorrentChange("torrent-set", TorrentActions::RefreshAfter::Files, m_currentId));
}

/** Изменяет выбор указанных файлов и обновляет вкладку после ответа демона. */
void DetailsPanel::setFilesDownload(const QVector<int>& fileIds, bool wanted)
{
    if (!m_connection.client() || m_currentId < 0 || fileIds.isEmpty())
        return;
    auto completed = m_actions->afterTorrentChange("torrent-set", TorrentActions::RefreshAfter::Files, m_currentId,
                                        wanted ? tr("Marked for download") : tr("Marked to skip"));
    if (wanted)
        m_connection.client()->setFilePriority(m_currentId, fileIds, 0, std::move(completed));
    else
        m_connection.client()->setFilesWanted(m_currentId, {}, fileIds, std::move(completed));
}

void DetailsPanel::onFilesWantToggled(const QVector<int>& rows, bool wanted)
{
    if (!m_connection.client() || m_currentId < 0 || rows.isEmpty())
        return;
    const auto& files = m_fileModel.files();
    QVector<int> ids;
    for (int r : rows)
        if (r >= 0 && r < files.size() && files.at(r).wanted != wanted)
            ids.append(files.at(r).index);
    setFilesDownload(ids, wanted);
}

QString DetailsPanel::currentDownloadDir() const
{
    for (const auto& x : m_model.torrents())
        if (x.id == m_currentId)
            return x.downloadDir.value_or(QString());
    return QString();
}

void DetailsPanel::onFilesOpen(const QModelIndex& index)
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

void DetailsPanel::onFilesOpenFolder(const QModelIndex& index)
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

void DetailsPanel::onFilesCopyPath(const QModelIndex& index)
{
    if (m_currentId < 0 || !index.isValid())
        return;
    const QString dir = currentDownloadDir();
    if (dir.isEmpty())
        return;
    const QString full = dir + QStringLiteral("/") +
                         index.data(FileTreeModel::PathRole).toString();
    QApplication::clipboard()->setText(QStringLiteral("\"%1\"").arg(full));
    m_status->showToast(tr("File path copied"));
}

void DetailsPanel::onFilesRename(const QModelIndex& index)
{
    if (!m_connection.client() || m_currentId < 0 || !index.isValid())
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
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
    rpc->renamePath(id, path, newName, m_actions->afterTorrentChange("torrent-rename-path", TorrentActions::RefreshAfter::Files, id));
}

void DetailsPanel::onTrackersContextMenu(const QPoint& pos)
{
    if (!m_connection.client() || m_currentId < 0)
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
    const auto isCurrent = [this, rpc = QPointer(m_connection.client()), id = m_currentId] {
        return rpc && rpc == m_connection.client() && id == m_currentId;
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
            m_actions->showProperties(id, 1);
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
        m_status->showMessage(tr("Column layout updated."), 3000);
    });
    menu->exec(global);
    menu->deleteLater();
}

bool DetailsPanel::eventFilter(QObject* watched, QEvent* event)
{
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
    return QTabWidget::eventFilter(watched, event);
}

void DetailsPanel::onAddTracker()
{
    if (!m_connection.client() || m_currentId < 0)
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
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
    rpc->addTracker(id, url, m_actions->afterTorrentChange("torrent-set", TorrentActions::RefreshAfter::Trackers, id));
}

void DetailsPanel::onEditTracker(int trackerId, const QString& announce)
{
    if (!m_connection.client() || m_currentId < 0 || trackerId < 0)
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
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
    rpc->replaceTracker(id, trackerId, url, m_actions->afterTorrentChange("torrent-set", TorrentActions::RefreshAfter::Trackers, id));
}

void DetailsPanel::onRemoveTracker(int trackerId, const QString& announce)
{
    if (!m_connection.client() || m_currentId < 0 || trackerId < 0)
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const int id = m_currentId;
    const auto ret = QMessageBox::question(
        this, tr("Remove tracker"), tr("Remove tracker “%1”?").arg(announce));
    if (ret != QMessageBox::Yes || !rpc)
        return;
    rpc->removeTracker(id, trackerId, m_actions->afterTorrentChange("torrent-set", TorrentActions::RefreshAfter::Trackers, id));
}
