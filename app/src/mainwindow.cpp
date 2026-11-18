#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>

#include "aboutdialog.h"
#include "connectbutton.h"
#include "daemonoptionsdialog.h"
#include "daemonprofile.h"
#include "externalopen.h"
#include "filterpanel.h"
#include "optionsdialog.h"
#include "propsform.h"
#include "settingsbackup.h"
#include "statsform.h"
#include "theme.h"
#include "torrentlabels.h"

namespace {
constexpr int kRefreshIntervalMs = 2000;
constexpr int kDetailsPanelHeight = 280; // высота панели деталей
constexpr int kMainTopMinHeight = 60;    // минимальная высота области списка

} // namespace

MainWindow::MainWindow(QWidget* parent, const tgui::RpcClient::Options& startupOptions)
    : QMainWindow(parent)
    , m_labels(torrentlabels::configured())
    , m_connection(*this, startupOptions)
    , m_list(*this, m_connection, m_labels)
    , m_details(this, m_connection, m_list.model(), m_sessionInfo,
                [this](const QString& path) { return m_list.mappedLocalPath(path); },
                [this](const QString& path, bool select) { m_list.openLocalPath(path, select); },
                [this] { return m_list.selectedTorrentIds().size(); })
    , m_status(*this, m_list.model(), m_sessionInfo, [this] { return m_connection.client(); },
               [this] { return m_connection.profile(); })
    , m_flags(m_details.peerModel(), [this](const QString& message) { m_status.showToast(message); })
    , m_geoIp(*this, m_details.peerModel(), m_flags)
    , m_updateChecker(*this, [this](const QString& title, const QString& message) {
        if (!m_tray->notify(title, message))
            m_status.showToast(message);
    })
    , m_actions(*this, m_connection, m_list.model(), m_status, m_labels,
                [this] { return m_list.selectedTorrentIds(); },
                [this] { return currentId(); },
                [this](TorrentActions::RefreshAfter refresh, bool ok) {
                    if (refresh == TorrentActions::RefreshAfter::Files)
                        m_details.refreshFilesAfterAction(ok);
                    else if (refresh == TorrentActions::RefreshAfter::Properties)
                        m_details.refreshFilesDetails();
                },
                [this] { m_list.updateFilterPanel(m_list.model().torrents()); },
                [this] {
                    m_list.refreshDisplayNames();
                    m_details.applySnapshot(currentId(), m_connection.isReconnecting());
                    m_status.updateStatusBarTotals();
                })
    , m_add(*this, m_connection, m_list.model(), m_watchService, [this] { return currentId(); })
    , m_clipboard(*this, m_list.model(), [this] { return m_connection.isConnected() ? m_connection.client() : nullptr; },
                  [this](const auto& params) { m_add.startAdd(params); })
{
    setWindowTitle(QStringLiteral(TGUI_DISPLAY_NAME));
    setWindowIcon(tgui::theme::icon(tgui::theme::Icon::App));
    resize(1200, 780);
    setAcceptDrops(true);
    QSettings().remove(QStringLiteral("connection"));
    ProfileStore().migratePasswords();

    createUi();
    createActions();
    createMenus();
    m_list.setServices(m_actions, m_status, *m_actChooseLabel, *m_actRefresh,
                       [this] { return currentId(); },
                       [this](int id) {
                           m_status.updateStatusBarTotals();
                           m_details.setCurrentTorrent(id);
                           updateSelectionActions();
                       },
                       [this] { m_status.updateStatusBarTotals(); },
                       [this] { updateLabelUi(); },
                       [this](const QString& message) { emit downloadNotification(message); });
    const auto refreshDisplayNames = [this] {
        m_list.refreshDisplayNames();
        m_details.applySnapshot(currentId(), m_connection.isReconnecting());
        m_status.updateStatusBarTotals();
    };
    connect(&m_add, &AddTorrentCoordinator::displayNamesChanged, this, refreshDisplayNames);
    m_list.updateFilterPanel(m_list.model().torrents());
    m_tray = std::make_unique<TrayController>(*this, m_status, *m_actions.startAllAction(), *m_actions.stopAllAction(), *m_actRefresh);
    connect(this, &MainWindow::downloadNotification, m_tray.get(), &TrayController::downloadCompleted);
    m_tray->updateVisibility();

    m_details.setServices(m_actions, m_geoIp, m_flags, m_status);
    m_geoIp.initialize();

    // W07: интервал обновления из настроек (секунды), по умолчанию 2 (ранее 2000 ms).
    // ARCH-02: таймер — простой член без parent (единая схема владения таймерами).
    m_refreshTimer.setObjectName(QStringLiteral("refreshTimer"));
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        if (m_connection.client() && m_connection.isConnected()) {
            m_connection.client()->pollTorrents();
            m_connection.client()->getSessionStats();
            m_details.refreshFilesDetails();
        }
    });
    connectConnectionController();
    m_watchService.setCanAccept([this] { return m_add.canAcceptWatchedFile(); });
    connect(&m_watchService, &WatchFolderService::fileReady, &m_add, &AddTorrentCoordinator::addWatchedFile);
    connect(&m_add, &AddTorrentCoordinator::torrentAdded, &m_list, &TorrentListController::selectWhenAvailable);
    connect(&m_add, &AddTorrentCoordinator::readError, &m_actions, &TorrentActions::onReadError);

    m_updateChecker.updateCheckSchedule();

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
    m_list.model().setSmoothRates(layout.value("ui/smoothRates", false).toBool());
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::saveWindowLayout);
    statusBar()->showMessage(tr("Disconnected"), 5000);
    m_connection.maybeAutoConnect();
}

MainWindow::~MainWindow()
{
    // ARCH-02: сначала останавливаем все таймеры и завершаем RPC-клиент,
    // затем деструкторы членов (модели, контроллеры) — клиент не должен
    // существовать (и тем более слать сигналы) после их уничтожения.
    // Владелец таймеров — сами члены-контроллеры, parent не нужен.
    m_refreshTimer.stop();
    m_watchService.stop();
    m_actions.suspend();
    m_connection.shutdown();
}

void MainWindow::createUi()
{
    auto* topPanel = m_list.createPanel();
    auto* splitter = new QSplitter(Qt::Vertical, this);
    m_mainSplitter = splitter;
    splitter->setHandleWidth(4);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(topPanel);
    splitter->addWidget(&m_details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({qMax(kMainTopMinHeight, height() - kDetailsPanelHeight - splitter->handleWidth()),
                        kDetailsPanelHeight});
    setCentralWidget(splitter);

    m_status.initialize(*m_list.view());

}

/** Обновляет подпись профиля и адрес или состояние подключения. */
void MainWindow::setConnectionState(const QString& text, const QColor& color)
{
    m_status.setConnectionState(text, color);
    updateConnectionButton();
}

void MainWindow::updateConnectionButton()
{
    if (!m_connectButton)
        return;

    if (!m_connection.isConnected()) {
        m_connectButton->setConnection(tr("Connect"), m_status.connectionStateText(), false);
        return;
    }

    const QUrl url = m_connection.client() ? m_connection.client()->options().url : QUrl{};
    const auto& profile = m_connection.profile();
    const QString title = profile.isEmpty() ? url.host() : profile;
    const QString address = url.adjusted(QUrl::RemoveUserInfo).authority();
    m_connectButton->setConnection(title.isEmpty() ? tr("Connected") : title,
                                   address.isEmpty() ? tr("Connected") : address, true);
}

void MainWindow::setConnectedUi(bool on)
{
    m_list.filterPanel()->setEnabledUi(on);
    m_actConnect->setEnabled(!on && !m_connection.client());
    updateConnectionButton();
    m_actDisconnect->setEnabled(m_connection.client() || m_connection.hasScheduledRetry());
    m_actAdd->setEnabled(on);
    m_actAddLink->setEnabled(on);
    m_actRefresh->setEnabled(on);
    m_actAutoRefresh->setEnabled(on);
    m_actAltSpeed->setEnabled(on);
    m_actDaemonOptions->setEnabled(on);
    m_actions.setConnected(on);
    m_status.setConnected(on);
    m_status.updateStatusBarTotals();
    updateSelectionActions();
}

// Действия над торрентами активны только при непустом выделении (как в
// transgui: acStartTorrent/acStopTorrent/… Enabled по SelectedCount).
void MainWindow::updateSelectionActions(qsizetype count)
{
    if (count < 0)
        count = m_list.selectedTorrentIds().size();
    const bool hasSel = m_connection.isConnected() && count != 0;
    m_details.updateInfoTitleWithCount(count);
    m_actions.updateSelection(count);
    if (m_actQueue) m_actQueue->setEnabled(hasSel);
    m_actChooseLabel->setEnabled(hasSel && m_connection.client() && m_connection.client()->supports(tgui::kRpcLabels));
}

/** Синхронизирует пункты меню и правую панель со списком меток. */
void MainWindow::updateLabelUi()
{
    const bool configured = !m_labels.isEmpty();
    const bool hasLabelFilters = configured || m_list.hasUnlabelledTorrents();
    m_actChooseLabel->setVisible(configured);
    m_actLabelGrouping->setVisible(hasLabelFilters);
    m_list.filterPanel()->setLabelGroupingEnabled(hasLabelFilters && m_actLabelGrouping->isChecked());
}

void MainWindow::connectConnectionController()
{
    connect(&m_connection, &ConnectionController::clientChanged, this, &MainWindow::onClientChanged);
    connect(&m_connection, &ConnectionController::connecting, this, [this](const QUrl& address) {
        m_watchService.stop(true);
        statusBar()->showMessage(tr("Connecting to %1…").arg(address.toDisplayString(QUrl::RemoveUserInfo)), 5000);
        setConnectedUi(false);
        setConnectionState(tr("Connecting…"), tgui::theme::color::accent);
    });
    connect(&m_connection, &ConnectionController::connected, this, &MainWindow::onConnected);
    connect(&m_connection, &ConnectionController::interrupted, this, [this] {
        m_add.interrupt();
    });
    connect(&m_connection, &ConnectionController::suspended, this, &MainWindow::suspendConnectionUi);
    connect(&m_connection, &ConnectionController::clearing, this, &MainWindow::onConnectionClearing);
    connect(&m_connection, &ConnectionController::cleared, this, &MainWindow::onConnectionCleared);
    connect(&m_connection, &ConnectionController::manuallyDisconnected, this, [this] {
        const auto discarded = m_discardedAdds;
        setConnectionState(tr("Disconnected"), tgui::theme::palette().statusOfflineDot);
        if (discarded > 0) {
            const auto notice = tr("Add queue cleared. Torrents not added: %1.").arg(discarded);
            statusBar()->showMessage(notice, 8000);
            m_status.showToast(notice);
        } else {
            statusBar()->showMessage(tr("Disconnected"), 5000);
        }
    });
    connect(&m_connection, &ConnectionController::retryScheduled, this, [this](int delay, const QString& error) {
        m_add.prepareRetry();
        setConnectedUi(false);
        setConnectionState(tr("Reconnecting…"), tgui::theme::color::accent);
        statusBar()->showMessage(tr("Retrying in %1 s: %2").arg(delay / 1000).arg(error), delay);
    });
    connect(&m_connection, &ConnectionController::failed, this, [this] {
        setConnectionState(tr("Connection failed"), tgui::theme::color::red);
        statusBar()->showMessage(tr("Disconnected"), 5000);
    });
    connect(&m_connection, &ConnectionController::authenticationRequired, this, [this] {
        setConnectionState(tr("Authentication required"), tgui::theme::color::red);
    });
    connect(&m_connection, &ConnectionController::profileRenamed, this, [this] { setConnectedUi(true); });
    connect(&m_connection, &ConnectionController::readError, &m_actions, &TorrentActions::onReadError);
    connect(&m_connection, &ConnectionController::message, statusBar(),
            static_cast<void (QStatusBar::*)(const QString&, int)>(&QStatusBar::showMessage));
}

void MainWindow::onClientChanged(tgui::RpcClient* client)
{
    if (!client)
        return;
    connect(client, &tgui::RpcClient::sessionInfoReceived, this,
            [this](const tgui::SessionInfo& info) {
        const auto free = info.downloadDir == m_sessionInfo.downloadDir
            ? m_sessionInfo.downloadDirFreeSpace : std::nullopt;
        m_sessionInfo = info;
        if (!m_sessionInfo.downloadDirFreeSpace)
            m_sessionInfo.downloadDirFreeSpace = free;
        const QSignalBlocker blocker(m_actAltSpeed);
        m_actAltSpeed->setChecked(info.altSpeedEnabled);
        if (m_details.generalForm()->torrent()) m_details.generalForm()->setSession(info);
        m_status.updateStatusBarTotals();
    });
    connect(client, &tgui::RpcClient::torrentsReceived, this,
            [this](const QVector<tgui::Torrent>& torrents) {
        if (m_connection.isConnected())
            onTorrentsReceived(torrents);
    });
    connect(client, &tgui::RpcClient::torrentDetailsReceived, &m_details, &DetailsPanel::onTorrentDetailsReceived);
    connect(client, &tgui::RpcClient::peersReceived, &m_details, &DetailsPanel::onPeersReceived);
    connect(client, &tgui::RpcClient::trackersReceived, this,
            &MainWindow::onTrackersReceived);
    connect(client, &tgui::RpcClient::sessionStatsReceived, this,
            [this](const tgui::SessionStats& stats) { m_details.statsForm()->setStats(stats); });
    connect(client, &tgui::RpcClient::readError, &m_actions, &TorrentActions::onReadError);
    connect(client, &tgui::RpcClient::requestCompleted, &m_actions,
            &TorrentActions::onRequestCompleted);

}

/** Блокирует действия на старом соединении до завершения его обработчиков. */
void MainWindow::suspendConnectionUi()
{
    m_refreshTimer.stop();
    m_watchService.stop();
    m_actions.suspend();
    m_details.suspend();
    setConnectedUi(false);
}

/** Сбрасывает ожидающие операции перед удалением клиента. */
void MainWindow::onConnectionClearing()
{
    m_watchService.stop(true);
    m_actions.clearPendingMove();
    m_discardedAdds = m_add.clear();
}

void MainWindow::onConnectionCleared()
{
    m_list.clear();
    m_details.clear();
    setConnectedUi(false);
}

void MainWindow::onConnect()
{
    m_connection.onConnect();
}

void MainWindow::onDisconnect()
{
    m_connection.onDisconnect();
}

void MainWindow::onConnectionOptions()
{
    m_connection.onConnectionOptions();
}

void MainWindow::setAutoConnect(const tgui::RpcClient::Options& opts)
{
    m_connection.setAutoConnect(opts);
}

void MainWindow::onConnected(const tgui::SessionInfo& info)
{
    m_sessionInfo = info;
    {
        const QSignalBlocker blocker(m_actAltSpeed);
        m_actAltSpeed->setChecked(info.altSpeedEnabled);
    }
    if (m_details.generalForm()->torrent())
        m_details.generalForm()->setSession(info);
    setConnectionState(tr("Connected"), tgui::theme::color::green);
    setConnectedUi(true);
    updateRefreshSchedule();
    m_connection.client()->getTorrents();
    m_connection.client()->getSessionStats();
    m_watchService.updateWatchFolder(m_connection.isConnected());
    m_actions.checkPendingMove();
    statusBar()->showMessage(tr("Connected"), 3000);
    m_add.openPendingExternalTorrents();
}

void MainWindow::onRefresh()
{
    if (m_connection.client() && m_connection.isConnected()) {
        m_connection.client()->getTorrents();
        m_connection.client()->getSessionStats();
        m_details.refreshFilesDetails();
    }
}

/** Выбирает интервал по состоянию окна, сохраняя пользовательский режим автообновления. */
void MainWindow::updateRefreshSchedule()
{
    if (!m_actAutoRefresh || !m_connection.isConnected() || !m_actAutoRefresh->isChecked()) {
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

void MainWindow::onTorrentsReceived(const QVector<tgui::Torrent>& torrents)
{
    const auto selection = m_list.applySnapshot(torrents, currentId());
    m_details.applySnapshot(selection.currentId, m_connection.isReconnecting());

    m_status.updateStatusBarTotals();
    updateSelectionActions(selection.count);
    if (m_connection.finishReconnect())
        m_add.resumeAddQueue(torrents);
}

void MainWindow::onAbout()
{
    AboutDialog dlg(this);
    dlg.exec();
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
    const auto backup = settingsbackup::read(path);
    if (!backup) {
        QMessageBox::warning(this, tr("Import settings"), tr("The file is not a supported TransmissionGUI settings backup."));
        return;
    }
    if (QMessageBox::question(this, tr("Import settings"),
            tr("Import settings from file? Current settings will be overwritten.\n"
               "Restart the application to apply the changes."))
            != QMessageBox::Yes)
        return;
    QSettings cur;
    cur.setFallbacksEnabled(false);
    const auto journal = settingsbackup::recoveryPath();
    if (journal.isEmpty() || !settingsbackup::replace(cur, *backup, journal)) {
        QMessageBox::warning(this, tr("Import settings"), tr("Could not apply the settings backup."));
        return;
    }
    ProfileStore().migratePasswords();
    m_settingsImported = true;
    m_list.suspendLayoutSave();
    m_updateChecker.suspend();
    m_flags.suspend();
    onDisconnect();
    statusBar()->showMessage(tr("Settings imported. Restart to apply."), 4000);
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
    m_updateChecker.checkUpdate(true);
}

void MainWindow::onOptions()
{
    // W07: диалог параметров (интерфейс: язык/масштаб/интервал; трей).
    auto* rpc = m_connection.isConnected() ? m_connection.client() : nullptr;
    const auto profile = (m_connection.client() || m_connection.hasScheduledRetry()) ? m_connection.profile() : ProfileStore().currentName();
    OptionsDialog dlg(this, rpc ? rpc->rpcVersion() : 0, rpc, profile);
    const auto result = dlg.exec();

    m_labels = torrentlabels::configured();
    m_list.updateFilterPanel(m_list.model().torrents());

    if (result != QDialog::Accepted)
        return;

    m_watchService.updateWatchFolder(m_connection.isConnected());
    m_updateChecker.updateCheckSchedule();
    // Интервал обновления — применяем сразу (запущенный таймер подхватит новый интервал).
    updateRefreshSchedule();
    m_list.model().setSmoothRates(QSettings().value("ui/smoothRates", false).toBool());
    m_status.updateStatusBarTotals();
    // Стиль кнопок панели инструментов — применяем сразу (вкладка «Интерфейс»).
    applyToolbarStyle(QSettings().value("ui/toolbarButtonStyle", 2).toInt());
    // Иконка трея — применяем сразу (показать/скрыть существующую).
    m_tray->updateVisibility();

    // Язык/масштаб — применяются при следующем запуске (см. main.cpp).
    if (dlg.restartRequired())
        QMessageBox::information(this, tr("Options"),
                                 tr("Language or interface scale changed. Restart the application to apply the new value."));
}

void MainWindow::onDaemonOptions()
{
    // W09: редактируемые настройки демона (session-set), применяются к живому демону.
    if (!m_connection.client() || !m_connection.isConnected()) {
        QMessageBox::information(this, tr("Daemon options"), tr("Not connected to the daemon."));
        return;
    }
    DaemonOptionsDialog dlg(m_connection.client()->sessionInfo(), this, m_connection.client());
    if (dlg.exec() != QDialog::Accepted)
        return;
    // Отправляем только изменённые поля: не перезаписываем чужие/неподдерживаемые.
    const QJsonObject params = dlg.changedSessionSet();
    if (params.isEmpty()) {
        statusBar()->showMessage(tr("No changes to apply."), 3000);
        return;
    }
    m_connection.client()->setSession(params); // фидбек — через onRequestCompleted (status bar)
    statusBar()->showMessage(tr("Applying daemon options…"), 3000);
}

/** Возвращает только локальные .torrent-файлы и поддерживаемые ссылки из @p mime. */
static QStringList droppedTorrentInputs(const QMimeData& mime)
{
    QStringList raw;
    if (mime.hasUrls()) {
        for (const auto& url : mime.urls())
            raw.append(url.toString(QUrl::FullyEncoded));
    } else if (mime.hasText()) {
        raw = mime.text().split(QLatin1Char('\n'));
    }

    QStringList accepted;
    for (const auto& line : raw) {
        const auto input = line.trimmed();
        const QUrl url(input, QUrl::StrictMode);
        if (url.isValid() && !url.host().isEmpty() && (url.scheme() == "http" || url.scheme() == "https")) {
            accepted.append(url.toString(QUrl::FullyEncoded));
            continue;
        }
        const auto torrent = externalTorrentInput(input);
        if (!torrent.isEmpty() && (torrent.startsWith("magnet:") || QFileInfo(torrent).isFile()))
            accepted.append(torrent);
    }
    return accepted;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_connection.isConnected() && !droppedTorrentInputs(*event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!m_connection.client() || !m_connection.isConnected())
        return;
    const auto items = droppedTorrentInputs(*event->mimeData());
    if (items.isEmpty())
        return;
    const auto& s = m_connection.client()->sessionInfo();
    for (const auto& item : items) {
        tgui::RpcClient::AddTorrent p;
        if (item.startsWith("magnet:") || item.startsWith("http://") || item.startsWith("https://")) {
            p.source = tgui::RpcClient::AddTorrent::Source::Url;
            p.url = item;
        } else {
            p.file = item;
        }
        p.downloadDir = s.downloadDir;
        p.start = s.startAddedTorrents;
        m_add.startAdd(p);
    }
    event->acceptProposedAction();
}

/** Наполняет меню подтверждёнными лимитами текущего демона и значениями его профиля. */
void MainWindow::addSpeedLimitMenu(QMenu* parent, bool download)
{
    parent->addMenu(m_status.createSpeedLimitMenu(parent, download));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveWindowLayout();
    if (m_tray && m_tray->handleClose(event))
        return;
    QMainWindow::closeEvent(event);
}

/** Сохраняет геометрию окна и пропорции основной панели. */
void MainWindow::saveWindowLayout()
{
    if (m_settingsImported)
        return;
    QSettings settings;
    m_list.saveColumnWidths();
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
    if (m_tray)
        m_tray->handleStateChange(event);
    QMainWindow::changeEvent(event);
}

void MainWindow::onExit()
{
    m_tray->onExit();
}

void MainWindow::openExternalTorrents(const QStringList& inputs)
{
    m_add.openExternalTorrents(inputs);
}

void MainWindow::onQueueUp()
{
    m_actions.onQueueUp();
}

void MainWindow::onQueueDown()
{
    m_actions.onQueueDown();
}

void MainWindow::onMoveSelected()
{
    m_actions.onMoveSelected();
}

void MainWindow::onRemoveWithDataSelected()
{
    m_actions.onRemoveWithDataSelected();
}

void MainWindow::onTrackersReceived(int id, const QVector<tgui::TrackerStat>& trackers)
{
    m_details.onTrackersReceived(id, trackers);
}
