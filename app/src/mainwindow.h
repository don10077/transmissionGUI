#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QEvent>
#include <memory>
#include <optional>

#include "tgui/rpcclient.h"
#include "tgui/geoip.h"
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
class QMenu;
class QSplitter;
class QToolBar;
class QToolButton;
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
    explicit MainWindow(QWidget* parent = nullptr, const tgui::RpcClient::Options& startupOptions = {});

    // Быстрый запуск из командной строки: --url/--user/--pass/--path.
    void setAutoConnect(const tgui::RpcClient::Options& opts);
    void openExternalTorrents(const QStringList& inputs);

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

signals:
    void downloadNotification(const QString& message);

private slots:
    void onConnect();
    void onDisconnect();
    void onAddTorrent();
    void scanWatchFolder();
    void onRefresh();
    void onConnected(const tgui::SessionInfo& info);
    void onConnectionFailed(const QString& error);
    void onAuthenticationFailed(const QString& error);
    void onClientTlsPassphraseRequired();
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
    void onPeersContextMenu(const QPoint& pos);
    void onFilePriority(const QVector<int>& fileIds, int priority);
    void setFilesDownload(const QVector<int>& fileIds, bool wanted);
    // Чекбокс Want (сигнал FileTreeModel::wantToggled) → RPC + перечитка.
    void onFilesWantToggled(const QVector<int>& rows, bool wanted);
    // Контекстное меню вкладки «Файлы» (референс — pmFiles).
    // index — активный узел дерева (файл или папка).
    void onFilesOpen(const QModelIndex& index);
    void onFilesOpenFolder(const QModelIndex& index);
    void onFilesCopyPath(const QModelIndex& index);
    void onFilesRename(const QModelIndex& index);
    // downloadDir текущего торрента (пусто, если не выбран / не найден).
    QString currentDownloadDir() const;
    void onTrackersContextMenu(const QPoint& pos);
    void onAddTracker();
    void onEditTracker(int trackerId, const QString& announce);
    void onRemoveTracker(int trackerId, const QString& announce);
    void onAbout();
    void onColumns();
    void onContextMenu(const QPoint& pos);
    void onFilterChanged(int group, const QString& folder, const QString& tracker,
                         const QString& label);
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
    void onAddFromLink();
    void onUpdateBlocklist();
    void onUpdateGeoIp();
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
    void onExportTorrentList();
    void onImportTorrentList();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void createUi();
    void createActions();
    void createMenus();
    void createTray();
    void addSpeedLimitMenu(QMenu* parent, bool download);
    bool ensureFlagImages(bool allowDownload);
    static QPixmap makeTrayIcon();
    void startConnect(const tgui::RpcClient::Options& opts, bool retry = false);
    void suspendConnection();
    void clearConnection();
    void handleConnectionError(const QString& error, bool retry);
    void maybeAutoConnect();
    void switchToProfile(const QString& name);
    // Двойной клик (референс: lvFilesDblClick / gTorrentsDblClick): файл → запустить,
    // папка → открыть; по строке списка торрентов → открыть его файл/папку.
    void onFilesDoubleClicked(int row);
    void onTorrentsDoubleClicked();
    // Открыть объект торрента по списку файлов (аналог OpenCurrentTorrent).
    // containingFolder — открыть каталог, содержащий файл/папку торрента.
    void openTorrentObject(int id, bool containingFolder);
    void openLocalPath(const QString& path, bool selectFile);
    QString mappedLocalPath(const QString& remotePath) const;
    const tgui::Torrent* torrentById(int id) const;
    // Проверка подтверждённого torrent-set-location: демон должен сообщить новый путь.
    void checkPendingMove();
    void setConnectedUi(bool connected);
    void setConnectionState(const QString& text, const QColor& color);
    void updateStatusBarTotals();
    void updateRefreshSchedule();
    void saveWindowLayout();
    void updateFilterPanel(const QVector<tgui::Torrent>& torrents);
    void updateSelectionActions();
    void updateLabelUi();
    // Обновить видимую вкладку данных выбранного торрента («Файлы»/«Пиры»):
    // проценты файлов и набор пиров меняются во время проверки и загрузки —
    // одного запроса при выборе мало (в старом transgui детали
    // перезапрашивались каждый цикл обновления).
    void refreshFilesDetails();
    void applyViewVisibility();
    void restoreTorrentSelection(const QVector<int>& ids, int currentId);
    int proxyRowForId(int id) const;
    enum class AddOrigin { Interactive, WatchFolder, External };
    void startAdd(const tgui::RpcClient::AddTorrent& params, AddOrigin origin = AddOrigin::Interactive);
    void addNextTorrent();
    void onClipboardChanged();
    void updateWatchFolder();
    void offerDuplicateUpdate(const tgui::RpcClient::AddTorrent& source,
                              const QJsonObject& duplicate, const QByteArray& metainfo);
    void finishAdd(const QPointer<tgui::RpcClient>& rpc, int selectedId = -1);
    QVector<int> selectedTorrentIds() const;
    enum class RefreshAfter { List, Files, Trackers, Properties };
    tgui::RpcClient::RequestCallback afterTorrentChange(const QString& method,
                                                        RefreshAfter refresh, int id = -1,
                                                        const QString& successMessage = {});
    void runTorrentAction(const QString& method, const QVector<int>& ids,
                          const QJsonObject& params = {});
    void showProperties(int id, int page);
    static void loadSettings(tgui::RpcClient::Options& opts);
    static void saveSettings(const tgui::RpcClient::Options& opts);

    std::unique_ptr<tgui::RpcClient> m_rpc;
    QString m_connectionProfile;
    QList<QMenu*> m_speedLimitMenus;
    bool m_connected = false;
    QTimer m_reconnectTimer;
    std::optional<tgui::RpcClient::Options> m_reconnectOptions;
    int m_reconnectDelayMs = 1000;

    // Параметры автоподключения (из командной строки).
    tgui::RpcClient::Options m_autoConnectOpts;

    // Состояние добавления торрента.
    QNetworkAccessManager m_nam;
    struct PendingAdd {
        tgui::RpcClient::AddTorrent params;
        AddOrigin origin;
        QString profile;
    };
    QQueue<PendingAdd> m_addQueue;
    QStringList m_pendingExternalTorrents;
    bool m_addingTorrent = false;
    int m_addSelectionId = -1;
    QSet<QString> m_clipboardLinksSeen;
    QString m_watchFolder;
    QHash<QString, QString> m_watchCandidates;
    QTimer m_watchTimer;

    TorrentModel m_model;
    TorrentFilterProxy m_proxy;
    FileListModel m_fileModel;
    PeerListModel m_peerModel;
    TrackerListModel m_trackerModel;
    tgui::GeoIpDb m_geoip; // GeoIP Country Edition (W11b)
    QTimer m_refreshTimer;
    QSplitter* m_mainSplitter = nullptr;
    int m_totalsMode = 0;
    bool m_settingsImported = false;
    bool m_checkingUpdate = false;
    // Ожидание подтверждённого нового downloadDir после переноса.
    QVector<int> m_movePendingIds;
    QString m_movePendingTarget;
    int m_moveVerifyAttempts = 0;
    QTimer m_moveVerifyTimer;

    QTabWidget* m_tabs = nullptr;
    PropsForm* m_general = nullptr;
    StatsForm* m_statsForm = nullptr;
    FilterPanel* m_filterPanel = nullptr;
    QTreeView* m_view = nullptr;
    QTreeView* m_filesView = nullptr;
    QTableView* m_peersView = nullptr;
    QTableView* m_trackersView = nullptr;
    FileTreeModel* m_fileTree = nullptr;
    QToolBar* m_toolBar = nullptr;
    // Кнопка подключения-дропдаун (аналог OLD tbConnect, tbsDropDown).
    QToolButton* m_connectButton = nullptr;
    QLineEdit* m_searchEdit = nullptr;
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
    QAction* m_actAddLink = nullptr;
    QAction* m_actStart = nullptr;
    QAction* m_actStop = nullptr;
    QAction* m_actStartAll = nullptr;
    QAction* m_actStopAll = nullptr;
    QAction* m_actRemove = nullptr;
    QAction* m_actProperties = nullptr;
    QAction* m_actMove = nullptr;
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
    QStringList m_labels;
    QAction* m_actTrackerGrouping = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
};
