#include "torrentlistcontroller.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTreeView>

#include <algorithm>
#include <utility>

#include "colsetup.h"
#include "connectioncontroller.h"
#include "delegates.h"
#include "daemonprofile.h"
#include "settingsbackup.h"
#include "externalopen.h"
#include "filterpanel.h"
#include "openpath.h"
#include "pathmap.h"
#include "statusbarcontroller.h"
#include "theme.h"
#include "torrentactions.h"
#include "torrentlabels.h"
#include "tgui/util.h"

TorrentListController::TorrentListController(QMainWindow& window, const ConnectionController& connection,
                                             const QStringList& labels)
    : m_window(window), m_connection(connection), m_labels(labels)
{
    m_proxy.setSourceModel(&m_model);
}

void TorrentListController::setServices(TorrentActions& actions, StatusBarController& status,
                                        QAction& chooseLabel, QAction& refresh, std::function<int()> currentId,
                                        std::function<void(int)> selectionChanged,
                                        std::function<void()> filterChanged, std::function<void()> labelsChanged,
                                        std::function<void(const QString&)> downloadCompleted)
{
    m_actions = &actions;
    m_status = &status;
    m_actChooseLabel = &chooseLabel;
    m_actRefresh = &refresh;
    m_currentId = std::move(currentId);
    m_selectionChanged = std::move(selectionChanged);
    m_filterChanged = std::move(filterChanged);
    m_labelsChanged = std::move(labelsChanged);
    m_downloadCompleted = std::move(downloadCompleted);
}

void TorrentListController::bindSearch(QLineEdit& search)
{
    m_searchEdit = &search;
    connect(&search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy.setSearchText(text.trimmed());
        m_filterChanged();
    });
}

QWidget* TorrentListController::createPanel()
{
    m_view = new QTreeView(&m_window);
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
    connect(m_view, &QWidget::customContextMenuRequested, this, &TorrentListController::onContextMenu);
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
    m_columnWidthTimer.setSingleShot(true);
    m_columnWidthTimer.setInterval(500);
    connect(&m_columnWidthTimer, &QTimer::timeout, this, &TorrentListController::saveColumnWidths);
    connect(m_view->header(), &QHeaderView::sectionResized, this,
            [this](int logical, int, int newWidth) {
                m_pendingColumnWidths.insert(logical, newWidth);
                m_columnWidthTimer.start();
            });
    // Единый делегат: выделение строки, статус, прогресс и метки.
    m_view->setItemDelegate(new TorrentDelegate(m_view));

    // Левая панель (референс — lvFilter + edSearch): группы статусов,
    // группы папок/трекеров, поиск.
    m_filterPanel = new FilterPanel(&m_window);
    connect(m_filterPanel, &FilterPanel::filterChanged, this, &TorrentListController::onFilterChanged);
    const bool folderGrouping = QSettings().value("ui/folderGrouping", true).toBool();
    const bool trackerGrouping = QSettings().value("ui/trackerGrouping", true).toBool();
    m_filterPanel->setFolderGroupingEnabled(folderGrouping);
    m_filterPanel->setTrackerGroupingEnabled(trackerGrouping);
    m_filterPanel->setEnabledUi(false);

    // Разделитель между фильтрами и списком торрентов.
    auto* topPanel = new QSplitter(Qt::Horizontal, &m_window);
    topPanel->setHandleWidth(4);
    topPanel->setChildrenCollapsible(false);
    topPanel->addWidget(m_filterPanel);
    topPanel->addWidget(m_view);
    topPanel->setStretchFactor(1, 1);
    topPanel->setSizes({215, m_window.width() - 215 - topPanel->handleWidth()});

    if (m_view->selectionModel()) {
        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &TorrentListController::onSelectionChanged);
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

    return topPanel;
}

void TorrentListController::clear()
{
    m_addSelectionId = -1;
    m_model.setTorrents({});
    updateFilterPanel(m_model.torrents());
}

void TorrentListController::suspendLayoutSave()
{
    m_saveSuspended = true;
    m_columnWidthTimer.stop();
}

TorrentListController::Selection TorrentListController::applySnapshot(const QVector<tgui::Torrent>& torrents, int prevId)
{
    if (QSettings().value("ui/trayNotify", false).toBool()) {
        QStringList completed;
        for (const auto& torrent : torrents) {
            const auto* old = m_model.torrentById(torrent.id);
            if (old && old->status.value_or(-1) == 4 && old->leftUntilDone.value_or(0) > 0 &&
                torrent.leftUntilDone && *torrent.leftUntilDone == 0 &&
                torrent.sizeWhenDone.value_or(0) > 0 && torrent.status.value_or(-1) != 2) {
                completed.append(torrent.name.value_or(QString::number(torrent.id)));
            }
        }
        if (!completed.isEmpty())
            m_downloadCompleted(completed.size() == 1 ?
                tr("Download completed: %1").arg(completed.first()) :
                tr("%n downloads completed").arg(completed.size())); // плюрал (I18N-03)
    }

    const bool firstLoad = m_model.rowCount() == 0;
    m_refreshing = true;
    m_model.setTorrents(torrents);
    updateFilterPanel(m_model.torrents());

    auto* selection = m_view->selectionModel();
    QModelIndex added;
    if (m_addSelectionId >= 0) {
        const auto source = m_model.index(m_model.rowForId(m_addSelectionId), TorrentModel::ColName);
        if (source.isValid()) {
            added = m_proxy.mapFromSource(source);
            if (!added.isValid()) {
                m_filterPanel->selectAllTorrents();
                m_searchEdit->clear();
                added = m_proxy.mapFromSource(source);
            }
            if (added.isValid()) {
                m_addSelectionId = -1;
                selection->setCurrentIndex(added, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                m_view->scrollTo(added);
            }
        }
    }

    if (!added.isValid() && firstLoad && m_proxy.rowCount() > 0 && !m_view->currentIndex().isValid()) {
        selection->setCurrentIndex(m_proxy.index(0, TorrentModel::ColName),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    } else if (!added.isValid() && prevId >= 0 && m_model.rowForId(prevId) < 0 && !selection->hasSelection()) {
        selection->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
    }

    const auto ids = selectedTorrentIds();
    const auto current = m_proxy.mapToSource(selection->currentIndex());
    const int nextId = current.isValid() && selection->isSelected(selection->currentIndex())
        ? m_model.torrentAt(current.row()).id : (ids.isEmpty() ? -1 : ids.first());
    m_refreshing = false;
    return {nextId, ids.size()};
}

void TorrentListController::onFilterChanged(int group, const QString& folder,
                                 const QString& tracker, const QString& label, bool withoutLabels)
{
    m_proxy.setStatusGroup(group);
    m_proxy.setFolderFilter(folder);
    m_proxy.setLabelFilter(label, withoutLabels);
    m_proxy.setTrackerFilter(tracker);
    if (m_filterChanged)
        m_filterChanged();
}

void TorrentListController::updateFilterPanel(const QVector<tgui::Torrent>& torrents)
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
    m_labelsChanged();
    m_filterPanel->setCounts(static_cast<int>(torrents.size()), downloading, completed,
                             active, inactive, stopped, error, waiting,
                             toPairs(folders), labelRows, toPairs(trackers));
}

QVector<int> TorrentListController::selectedTorrentIds() const
{
    QVector<int> ids;
    if (!m_view || !m_view->selectionModel())
        return ids;
    // selectedRows() уже отдаёт по одному индексу на строку (без дублей по
    // колонкам). mapToSource() — ровно один раз: раньше индекс маппился
    // повторно, и результат всегда был пустым (все действия над торрентами
    // оказывались недоступны).
    for (const QModelIndex& vi : m_view->selectionModel()->selectedRows()) {
        const QModelIndex src = m_proxy.mapToSource(vi);
        if (!src.isValid())
            continue;
        ids.push_back(m_model.torrentAt(src.row()).id);
    }
    if (ids.isEmpty()) {
        const auto current = m_proxy.mapToSource(m_view->currentIndex());
        if (current.isValid())
            ids.append(m_model.torrentAt(current.row()).id);
    }
    return ids;
}

void TorrentListController::onColumns()
{
    const int count = TorrentModel::ColumnCount;
    QStringList titles;
    if (auto* model = m_view->model())
        for (int i = 0; i < count; ++i)
            titles << model->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString();
    saveColumnWidths();
    ColumnLayout cur = ColumnLayout::fromSettings(count);
    ColSetupDialog dlg(cur, titles, &m_window);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const ColumnLayout nl = dlg.layout();
    nl.applyToView(m_view);
    nl.saveToSettings();
    m_status->showMessage(tr("Column layout updated."), 3000);
}

void TorrentListController::onContextMenu(const QPoint& pos)
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

    auto* menu = new QMenu(&m_window);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    const auto selectedIds = selectedTorrentIds();
    const bool hasSel = m_connection.isConnected() && !selectedIds.isEmpty();
    const QSet<int> selectedSet(selectedIds.cbegin(), selectedIds.cend());
    const auto connectSelectionAction = [this, rpc = QPointer(m_connection.client()), selectedSet]
                                       (QAction* action, auto handler) {
        connect(action, &QAction::triggered, this, [this, rpc, selectedSet, handler] {
            const auto current = selectedTorrentIds();
            if (rpc && rpc == m_connection.client() &&
                QSet<int>(current.cbegin(), current.cend()) == selectedSet)
                handler();
        });
    };
    using tgui::theme::Icon;

    // Открыть (папку, содержащую объект) — как в образце.
    auto* actOpen = menu->addAction(tgui::theme::icon(Icon::Folder), tr("Open folder containing object"));
    actOpen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    actOpen->setEnabled(hasSel);
    connectSelectionAction(actOpen, [this] { onOpenFolder(); });
    menu->addSeparator();

    auto* actStart = menu->addAction(tgui::theme::icon(Icon::Start), tr("Start"));
    actStart->setShortcut(QKeySequence(Qt::Key_F3));
    actStart->setEnabled(hasSel);
    connectSelectionAction(actStart, [this] { m_actions->onStartSelected(); });
    auto* actForce = menu->addAction(tgui::theme::icon(Icon::ForceStart), tr("Force start"));
    actForce->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    actForce->setEnabled(hasSel);
    connectSelectionAction(actForce, [this] { m_actions->onStartForceSelected(); });
    auto* actStop = menu->addAction(tgui::theme::icon(Icon::Pause), tr("Pause"));
    actStop->setShortcut(QKeySequence(Qt::Key_F4));
    actStop->setEnabled(hasSel);
    connectSelectionAction(actStop, [this] { m_actions->onStopSelected(); });
    menu->addSeparator();

    auto* actRemove = menu->addAction(tgui::theme::icon(Icon::Remove), tr("Remove…"));
    actRemove->setShortcut(QKeySequence(Qt::Key_Delete));
    actRemove->setEnabled(hasSel);
    connectSelectionAction(actRemove, [this] { m_actions->onRemoveSelected(); });
    auto* actRemoveData = menu->addAction(tr("Remove with data…"));
    actRemoveData->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    actRemoveData->setEnabled(hasSel);
    connectSelectionAction(actRemoveData, [this] { m_actions->onRemoveWithDataSelected(); });
    menu->addSeparator();

    auto* mPriority = menu->addMenu(tr("Priority"));
    mPriority->setEnabled(hasSel);
    auto* priHigh = mPriority->addAction(tr("High"));
    auto* priNormal = mPriority->addAction(tr("Normal"));
    auto* priLow = mPriority->addAction(tr("Low"));
    connectSelectionAction(priHigh, [this] { m_actions->onPriorityHigh(); });
    connectSelectionAction(priNormal, [this] { m_actions->onPriorityNormal(); });
    connectSelectionAction(priLow, [this] { m_actions->onPriorityLow(); });

    auto* mQueue = menu->addMenu(tr("Queue"));
    mQueue->setEnabled(hasSel);
    auto* qTop = mQueue->addAction(tr("Move to top"));
    auto* qUp = mQueue->addAction(tgui::theme::icon(Icon::QueueUp), tr("Move up"));
    auto* qDown = mQueue->addAction(tgui::theme::icon(Icon::QueueDown), tr("Move down"));
    auto* qBottom = mQueue->addAction(tr("Move to bottom"));
    connectSelectionAction(qTop, [this] { m_actions->onQueueTop(); });
    connectSelectionAction(qUp, [this] { m_actions->onQueueUp(); });
    connectSelectionAction(qDown, [this] { m_actions->onQueueDown(); });
    connectSelectionAction(qBottom, [this] { m_actions->onQueueBottom(); });
    menu->addSeparator();

    auto* actAnnounce = menu->addAction(tr("Announce (get more peers)"));
    actAnnounce->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    actAnnounce->setEnabled(hasSel);
    connectSelectionAction(actAnnounce, [this] { m_actions->onReannounceSelected(); });
    auto* actVerify = menu->addAction(tr("Verify…"));
    actVerify->setEnabled(hasSel);
    connectSelectionAction(actVerify, [this] { m_actions->onVerifySelected(); });
    menu->addSeparator();

    auto* actCopyMagnet = menu->addAction(tgui::theme::icon(Icon::Copy), tr("Copy magnet link"));
    actCopyMagnet->setEnabled(hasSel);
    connectSelectionAction(actCopyMagnet, [this] { onCopyMagnet(); });
    auto* actMove = menu->addAction(tgui::theme::icon(Icon::Move), tr("Set location…"));
    actMove->setShortcut(QKeySequence(Qt::Key_F6));
    actMove->setEnabled(hasSel);
    connectSelectionAction(actMove, [this] { m_actions->onMoveSelected(); });
    auto* actRename = menu->addAction(tr("Rename…"));
    actRename->setShortcut(QKeySequence(Qt::Key_F2));
    actRename->setEnabled(hasSel);
    connectSelectionAction(actRename, [this] { m_actions->onRenameSelected(); });
    menu->addSeparator();

    auto* actProperties = menu->addAction(tgui::theme::icon(Icon::Properties), tr("Properties…"));
    actProperties->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    actProperties->setEnabled(hasSel);
    connectSelectionAction(actProperties, [this] { m_actions->onShowProperties(); });
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
        connectSelectionAction(labelAction, [this] { m_actions->onSetLabels(); });
        labelAction->setEnabled(m_actChooseLabel->isEnabled());
    }
    auto* actSetupColumns = menu->addAction(tr("Setup columns…"));
    QObject::connect(actSetupColumns, &QAction::triggered, this, &TorrentListController::onColumns);
    menu->addSeparator();

    auto* actRefresh = menu->addAction(tgui::theme::icon(Icon::Refresh), tr("Refresh"));
    actRefresh->setEnabled(m_connection.isConnected());
    QObject::connect(actRefresh, &QAction::triggered, m_actRefresh, &QAction::trigger);
    auto* actSelectAll = menu->addAction(tr("Select all"));
    actSelectAll->setEnabled(m_connection.isConnected());
    QObject::connect(actSelectAll, &QAction::triggered, this, &TorrentListController::onSelectAll);

    menu->exec(m_view->mapToGlobal(pos));
}

void TorrentListController::onSelectAll()
{
    m_view->selectAll();
    onSelectionChanged();
}

void TorrentListController::onCopyPath()
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
            m_status->showToast(tr("Path copied"));
            return;
        }
    }
}

void TorrentListController::onOpenFolder()
{
    const auto ids = selectedTorrentIds();
    if (ids.size() != 1)
        return;
    openTorrentObject(ids.first(), /*containingFolder=*/true);
}

void TorrentListController::onTorrentsDoubleClicked()
{
    if (m_currentId() < 0)
        return;
    const tgui::Torrent* t = m_model.torrentById(m_currentId());
    if (!t)
        return;
    // Готовый торрент (leftUntilDone == 0) — открыть его файл/папку;
    // не готовый — свойства (в образце файл ещё не скачан, запускать нечего).
    const bool finished = t->leftUntilDone.has_value() && t->leftUntilDone.value() <= 0.0
                          && (t->sizeWhenDone.value_or(0.0) > 0.0);
    if (finished)
        openTorrentObject(m_currentId(), /*containingFolder=*/false);
    else
        m_actions->showProperties(m_currentId(), 0);
}

/** Открывает файл/папку торрента, вычисляя путь из списка файлов и downloadDir. */
void TorrentListController::openTorrentObject(int id, bool containingFolder)
{
    const tgui::Torrent* torrent = m_model.torrentById(id);
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
    if (!m_connection.client() || !m_connection.isConnected()) {
        openFromName();
        return;
    }

    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const auto profile = m_connection.profile();
    rpc->getTorrentDetails(id, [this, rpc, id, containingFolder, profile]
                           (const std::optional<tgui::TorrentDetails>& details) {
        if (!rpc || rpc != m_connection.client() || !m_connection.isConnected() || profile != m_connection.profile())
            return;
        const tgui::Torrent* current = m_model.torrentById(id);
        if (!current)
            return;
        if (!details) {
            if (!containingFolder)
                m_actions->showProperties(id, 0);
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
            m_actions->showProperties(id, 0);
            return;
        }
        openLocalPath(path, selectFile);
    });
}

QString TorrentListController::mappedLocalPath(const QString& remotePath) const
{
    return tgui::mapRemoteToLocal(remotePath, ProfilePreferences(m_connection.profile()).mappings()).value_or(QString{});
}

void TorrentListController::openLocalPath(const QString& path, bool selectFile)
{
    if (path.isEmpty())
        return;
    // Маппинг remote→local: путь демона (на сервере) → локальный.
    // Правила задаются в Application options → Paths (строки "remote=local").
    const QString local = mappedLocalPath(path);
    if (local.isEmpty())
        return;

    const QFileInfo fi(local);
    if (fi.exists() && !selectFile && tgui::isPotentiallyExecutable(fi)) {
        QMessageBox confirm(QMessageBox::Warning, tr("Open downloaded file"),
            tr("This file may run a program: %1").arg(fi.fileName()), QMessageBox::Cancel, &m_window);
        confirm.setTextFormat(Qt::PlainText);
        auto* folder = confirm.addButton(tr("Open folder"), QMessageBox::ActionRole);
        auto* run = confirm.addButton(tr("Run"), QMessageBox::AcceptRole);
        confirm.setDefaultButton(folder);
        confirm.exec();
        if (confirm.clickedButton() == run)
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.filePath()));
        else if (confirm.clickedButton() == folder)
            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
        return;
    }
    if (fi.exists() && (fi.isDir() || !selectFile)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.filePath()));
        return;
    }
    const QDir d = fi.dir();
    if (d.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(d.absolutePath()));
        return;
    }
    m_status->showMessage(tr("Path not found: %1").arg(local), 4000);
}

void TorrentListController::onCopyMagnet()
{
    const auto ids = selectedTorrentIds();
    if (ids.size() != 1)
        return;
    const auto* torrent = m_model.torrentById(ids.first());
    if (!torrent)
        return;
    const auto copy = [this](QString magnet, const std::optional<QString>& hash) {
        if (magnet.isEmpty() && hash)
            magnet = QStringLiteral("magnet:?xt=urn:btih:") + *hash;
        if (magnet.isEmpty())
            return;
        QApplication::clipboard()->setText(magnet);
        m_status->showToast(tr("Magnet link copied"));
    };
    if (!m_connection.client() || !m_connection.isConnected() || torrent->magnetLink) {
        copy(torrent->magnetLink.value_or(QString{}), torrent->hashString);
        return;
    }
    const QPointer rpc(m_connection.client());
    m_connection.client()->getTorrentProperties(torrent->id, [this, rpc, copy, hash = torrent->hashString](const auto& detail) {
        if (!rpc || rpc != m_connection.client() || !m_connection.isConnected())
            return;
        if (detail && hash && detail->hashString && hash != detail->hashString)
            return;
        copy(detail ? detail->magnetLink.value_or(QString{}) : QString{}, hash);
    }, tgui::RpcClient::TorrentProperties::Magnet);
}

void TorrentListController::onSelectionChanged()
{
    // modelReset при автообновлении временно «сбрасывает» выделение —
    // это обрабатывается onTorrentsReceived (восстановление по prevId).
    if (m_refreshing)
        return;
    int id = -1;
    if (const auto* selection = m_view->selectionModel()) {
        auto index = selection->currentIndex();
        if (!selection->isSelected(index)) {
            const auto rows = selection->selectedRows();
            if (!rows.isEmpty())
                index = rows.first();
        }
        const auto source = m_proxy.mapToSource(index);
        if (source.isValid())
            id = m_model.torrentAt(source.row()).id;
    }
    if (m_selectionChanged)
        m_selectionChanged(id);
}

void TorrentListController::saveColumnWidths()
{
    if (m_saveSuspended || m_pendingColumnWidths.isEmpty())
        return;
    m_columnWidthTimer.stop();
    QSettings settings;
    for (auto it = m_pendingColumnWidths.cbegin(); it != m_pendingColumnWidths.cend(); ++it)
        settings.setValue(QStringLiteral("columns/width_%1").arg(it.key()), it.value());
    m_pendingColumnWidths.clear();
}

/** Экспортирует весь подтверждённый список в переносимый файл magnet-ссылок. */
void TorrentListController::onExportTorrentList()
{
    if (m_connection.client() && m_connection.isConnected()) {
        const QPointer rpc(m_connection.client());
        m_connection.client()->getTorrentMagnets([this, rpc](const auto& torrents) {
            if (rpc && rpc == m_connection.client() && m_connection.isConnected())
                exportTorrentLinks(torrents.isEmpty() ? m_model.torrents() : torrents);
        });
        return;
    }
    exportTorrentLinks(m_model.torrents());
}

/** Сохраняет подтверждённые magnet-ссылки из @p torrents. */
void TorrentListController::exportTorrentLinks(const QVector<tgui::Torrent>& torrents)
{
    QStringList links;
    QSet<QString> identifiers;
    for (const auto& torrent : torrents) {
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
        QMessageBox::information(&m_window, tr("Export torrent list"), tr("There are no torrent links to export."));
        return;
    }
    const auto path = QFileDialog::getSaveFileName(&m_window, tr("Export torrent list"), "torrents.txt",
                                                  tr("Magnet lists (*.txt)"));
    if (!path.isEmpty() && !settingsbackup::writeFile(path, (links.join('\n') + '\n').toUtf8()))
        QMessageBox::warning(&m_window, tr("Export torrent list"), tr("Could not write the torrent list."));
}
