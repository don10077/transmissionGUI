#pragma once

#include <QMainWindow>
#include "watchfolderservice.h"
#include "updatechecker.h"
#include "clipboardmonitor.h"
#include "flagimageloader.h"
#include "geoipupdater.h"
#include "statusbarcontroller.h"
#include "connectioncontroller.h"
#include "addtorrentcoordinator.h"
#include "torrentactions.h"
#include "detailspanel.h"
#include "torrentlistcontroller.h"
#include "traycontroller.h"
#include <QTimer>
#include <memory>

#include "tgui/rpcclient.h"

class QLineEdit;
class QTreeView;
class QAction;
class QMenu;
class QSplitter;
class QToolBar;
class ConnectButton;
class QDragEnterEvent;
class QDropEvent;
class QCloseEvent;
class QColor;
class PropsForm;
class StatsForm;
class FilterPanel;

/** Собирает интерфейс и связывает контроллеры приложения. */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr, const tgui::RpcClient::Options& startupOptions = {});
    ~MainWindow() override;

    // Быстрый запуск из командной строки: --url/--user/--pass/--path.
    void setAutoConnect(const tgui::RpcClient::Options& opts);
    void openExternalTorrents(const QStringList& inputs);

    // Чтение-только для E2E-тестов (offscreen): состояние и вкладки деталей.
    bool isConnectedNow() const { return m_connection.isConnected(); }
    int currentId() const { return m_details.currentId(); }
    const TorrentModel& torrentModel() const { return m_list.model(); }
    // Не-const — для тестов (загрузка данных в модель).
    TorrentModel& torrentModel() { return m_list.model(); }
    const FileListModel& fileModel() const { return m_details.fileModel(); }
    const PeerListModel& peerModel() const { return m_details.peerModel(); }
    const TrackerListModel& trackerModel() const { return m_details.trackerModel(); }
    QTreeView* torrentView() const { return m_list.view(); }
    // Двухуровневое окно: грид сверху, вкладки снизу (как в Transmission).
    // «General» = содержимое диалога «Свойства».
    PropsForm* generalForm() const { return m_details.generalForm(); }
    StatsForm* statsForm() const { return m_details.statsForm(); }
    FilterPanel* filterPanel() const { return m_list.filterPanel(); }
    // Публичный вызов обновления списка (обёртка над onTorrentsReceived) —
    // для регресс-тестов: симуляция сигнала torrentsReceived без живого демона.
    void applyTorrentsUpdate(const QVector<tgui::Torrent>& torrents)
    {
        onTorrentsReceived(torrents);
    }

signals:
    void downloadNotification(const QString& message);

private slots:
    void onConnect();
    void onDisconnect();

    void onRefresh();
    void onConnected(const tgui::SessionInfo& info);
    void onTorrentsReceived(const QVector<tgui::Torrent>& torrents);
    void onTrackersReceived(int id, const QVector<tgui::TrackerStat>& trackers);
    void onMoveSelected();
    void onAbout();
    void onRemoveWithDataSelected();
    void onQueueUp();
    void onQueueDown();
    void onExit();
    void onHome();
    void onDevSite();
    void onCheckUpdate();
    void onToggleAltSpeed();
    void onDaemonOptions();
    void onOptions();
    void onConnectionOptions();
    void onExportSettings();
    void onImportSettings();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    void createUi();
    void createActions();
    void applyToolbarStyle(int value);
    void createMenus();
    void addSpeedLimitMenu(QMenu* parent, bool download);
    void setConnectedUi(bool connected);
    void setConnectionState(const QString& text, const QColor& color);
    void updateConnectionButton();
    void updateRefreshSchedule();
    void saveWindowLayout();
    void updateSelectionActions(qsizetype count = -1);
    void updateLabelUi();
    void applyViewVisibility();
    void connectConnectionController();
    void onClientChanged(tgui::RpcClient* client);
    void suspendConnectionUi();
    void onConnectionClearing();
    void onConnectionCleared();

    QTimer m_refreshTimer;
    QSplitter* m_mainSplitter = nullptr;
    bool m_settingsImported = false;
    tgui::SessionInfo m_sessionInfo; // session-get для лимитов и вкладки «General»
    QStringList m_labels;
    ConnectionController m_connection;
    WatchFolderService m_watchService;
    TorrentListController m_list;
    DetailsPanel m_details;
    StatusBarController m_status;
    FlagImageLoader m_flags;
    GeoIpUpdater m_geoIp;
    UpdateChecker m_updateChecker;
    QToolBar* m_toolBar = nullptr;
    // Кнопка подключения-дропдаун (аналог OLD tbConnect, tbsDropDown).
    ConnectButton* m_connectButton = nullptr;
    QLineEdit* m_searchEdit = nullptr;

    QAction* m_actConnect = nullptr;
    QAction* m_actDisconnect = nullptr;
    QAction* m_actAdd = nullptr;
    QAction* m_actAddLink = nullptr;
    QAction* m_actQueue = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actAutoRefresh = nullptr;
    QAction* m_actAltSpeed = nullptr;
    QAction* m_actDaemonOptions = nullptr;
    QAction* m_actOptions = nullptr;
    QAction* m_actAbout = nullptr;
    // View (visibility/grouping) — нужны для применения сохранённого состояния
    // при старте и для горячих клавиш (F11/F12/Shift+F11/Shift+F12).
    QAction* m_actFilterPanel = nullptr;
    QAction* m_actInfoPanel = nullptr;
    QAction* m_actToolbar = nullptr;
    QAction* m_actMenu = nullptr;
    QAction* m_actStatusBar = nullptr;
    QAction* m_actFolderGrouping = nullptr;
    QAction* m_actLabelGrouping = nullptr;
    QAction* m_actChooseLabel = nullptr;
    QAction* m_actTrackerGrouping = nullptr;

    qsizetype m_discardedAdds = 0;
    TorrentActions m_actions;
    AddTorrentCoordinator m_add;
    ClipboardMonitor m_clipboard;
    std::unique_ptr<TrayController> m_tray;
};
