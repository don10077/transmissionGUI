#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QEvent>
#include <memory>

#include "tgui/rpcclient.h"
#include "detailmodels.h"
#include "groupedproxy.h"
#include "torrentmodel.h"

class QComboBox;
class QLineEdit;
class QLabel;
class QTabWidget;
class QTableView;
class QTreeView;
class QAction;
class QNetworkAccessManager;
class QDragEnterEvent;
class QDropEvent;
class QCloseEvent;
class QSystemTrayIcon;
class QPixmap;
class QColor;
class PropsForm;
class StatsForm;
class FilterPanel;
class OptionsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Быстрый запуск из командной строки: --url/--user/--pass/--path.
    void setAutoConnect(const tgui::RpcClient::Options& opts);

    // Чтение-только для E2E-тестов (offscreen): состояние и вкладки деталей.
    bool isConnectedNow() const { return m_connected; }
    int currentId() const { return m_currentId; }
    const TorrentModel& torrentModel() const { return m_model; }
    // Не-const — для тестов (загрузка данных в модель).
    TorrentModel& torrentModel() { return m_model; }
    const FileListModel& fileModel() const { return m_fileModel; }
    const PeerListModel& peerModel() const { return m_peerModel; }
    const TrackerListModel& trackerModel() const { return m_trackerModel; }
    QTreeView* torrentView() const { return m_view; }
    // Двухуровневое окно: грид сверху, вкладки снизу (раскладка — по референсу,
    // см. WORKFLOW.md). «General» = содержимое диалога «Свойства».
    PropsForm* generalForm() const { return m_general; }
    StatsForm* statsForm() const { return m_statsForm; }
    FilterPanel* filterPanel() const { return m_filterPanel; }
    // Публичный вызов обновления списка (обёртка над onTorrentsReceived) —
    // для регресс-тестов: симуляция сигнала torrentsReceived без живого демона.
    void applyTorrentsUpdate(const QVector<tgui::Torrent>& torrents)
    {
        onTorrentsReceived(torrents);
    }

private slots:
    void onConnect();
    void onDisconnect();
    void onAddTorrent();
    void onRefresh();
    void onConnected(const tgui::SessionInfo& info);
    void onConnectionFailed(const QString& error);
    void onAuthenticationFailed(const QString& error);
    void onTorrentsReceived(const QVector<tgui::Torrent>& torrents);
    void onTorrentDetailsReceived(const tgui::TorrentDetails& details);
    void onPeersReceived(int id, const QVector<tgui::Peer>& peers);
    void onTrackersReceived(int id, const QVector<tgui::TrackerStat>& trackers);
    void onReadError(const QString& what, const QString& error);
    void onRequestCompleted(const QString& method, bool ok, const QString& error,
                            const QJsonObject& arguments);
    void onStartSelected();
    void onStopSelected();
    void onStartAll();
    void onStopAll();
    void onRemoveSelected();
    void onShowProperties();
    void onMoveSelected();
    void onSelectionChanged();
    void onFileWantToggled(int fileIndex, bool wanted);
    void onFilesContextMenu(const QPoint& pos);
    void onFilePriority(const QVector<int>& fileIds, int priority);
    void onFileWantSelected(const QVector<int>& fileIds);
    void onFileSkipSelected(const QVector<int>& fileIds);
    void onTrackersContextMenu(const QPoint& pos);
    void onAddTracker();
    void onEditTracker(int trackerId, const QString& announce);
    void onRemoveTracker(int trackerId, const QString& announce);
    void onAbout();
    void onColumns();
    void onContextMenu(const QPoint& pos);
    void onFilterChanged(int group, const QString& folder, const QString& tracker);
    void onSelectAll();
    void onCopyPath();
    void onOpenFolder();
    void onCopyMagnet();
    void onRemoveWithDataSelected();
    void onVerifySelected();
    void onReannounceSelected();
    void onQueueTop();
    void onQueueUp();
    void onQueueDown();
    void onQueueBottom();
    void onStartForceSelected();
    void onToggleSequential();
    void onAddFromLink();
    void onUpdateBlocklist();
    void onExit();
    void onRenameSelected();
    void onHome();
    void onDevSite();
    void onCheckUpdate();
    void onPriorityHigh();
    void onPriorityNormal();
    void onPriorityLow();
    void onToggleAltSpeed();
    void onPortTest();
    void onDaemonOptions();
    void onOptions();
    void onConnectionOptions();
    void onSetLabels();
    void onExportSettings();
    void onImportSettings();
    void downloadAndAdd(const tgui::RpcClient::AddTorrent& params, const QString& dst);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void createUi();
    void createActions();
    void createMenus();
    void createTray();
    static QPixmap makeTrayIcon();
    void startConnect(const tgui::RpcClient::Options& opts);
    void maybeAutoConnect();
    void switchToProfile(const QString& name);
    // Двойной клик (референс: lvFilesDblClick / gTorrentsDblClick): файл → запустить,
    // папка → открыть; по строке списка торрентов → открыть его файл/папку.
    void onFilesDoubleClicked(int row);
    void onTorrentsDoubleClicked();
    void openLocalPath(const QString& path, bool selectFile);
    void setConnectedUi(bool connected);
    void setConnectionState(const QString& text, const QColor& color);
    void updateStatusBarTotals();
    void updateFilterPanel(const QVector<tgui::Torrent>& torrents);
    void startAdd(const tgui::RpcClient::AddTorrent& params, bool doSave,
                  const QString& savePath);
    void finishAdd();
    QVector<int> selectedTorrentIds() const;
    static void loadSettings(tgui::RpcClient::Options& opts);
    static void saveSettings(const tgui::RpcClient::Options& opts);

    std::unique_ptr<tgui::RpcClient> m_rpc;
    bool m_connected = false;

    // Параметры автоподключения (из командной строки).
    tgui::RpcClient::Options m_autoConnectOpts;

    // Состояние добавления торрента.
    QNetworkAccessManager m_nam;
    tgui::RpcClient::AddTorrent m_pendingAdd;
    int m_pendingAddId = -1;
    bool m_pendingStart = false;

    TorrentModel m_model;
    TorrentFilterProxy m_proxy;
    FileListModel m_fileModel;
    PeerListModel m_peerModel;
    TrackerListModel m_trackerModel;
    QTimer m_refreshTimer;

    QTabWidget* m_tabs = nullptr;
    PropsForm* m_general = nullptr;
    StatsForm* m_statsForm = nullptr;
    FilterPanel* m_filterPanel = nullptr;
    QTreeView* m_view = nullptr;
    QTableView* m_filesView = nullptr;
    // ID торрента, для которого загружаются вкладки Файлы/Пиры/Трекеры.
    int m_currentId = -1;
    // true только в момент modelReset при автообновлении: onSelectionChanged
    // в этот момент не должен сбрасывать m_currentId и детали.
    bool m_refreshing = false;
    tgui::SessionInfo m_sessionInfo; // session-get для лимитов в «General»
    QLabel* m_daemonLabel = nullptr;
    QLabel* m_speedLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_statusText = nullptr;
    QLabel* m_statusDot = nullptr;

    QAction* m_actConnect = nullptr;
    QAction* m_actDisconnect = nullptr;
    QAction* m_actAdd = nullptr;
    QAction* m_actStart = nullptr;
    QAction* m_actStop = nullptr;
    QAction* m_actStartAll = nullptr;
    QAction* m_actStopAll = nullptr;
    QAction* m_actRemove = nullptr;
    QAction* m_actProperties = nullptr;
    QAction* m_actMove = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actAutoRefresh = nullptr;
    QAction* m_actAltSpeed = nullptr;
    QAction* m_actAbout = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
};
