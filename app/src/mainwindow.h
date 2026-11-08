#pragma once

#include <QMainWindow>
#include <QTimer>
#include <memory>

#include "tgui/rpcclient.h"
#include "groupedproxy.h"
#include "torrentmodel.h"

class QComboBox;
class QLineEdit;
class QLabel;
class QTreeView;
class QAction;
class QNetworkAccessManager;
class QDragEnterEvent;
class QDropEvent;
class QCloseEvent;
class QSystemTrayIcon;
class QPixmap;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Быстрый запуск из командной строки: --url/--user/--pass/--path.
    void setAutoConnect(const tgui::RpcClient::Options& opts);

private slots:
    void onConnect();
    void onDisconnect();
    void onAddTorrent();
    void onRefresh();
    void onConnected(const tgui::SessionInfo& info);
    void onConnectionFailed(const QString& error);
    void onTorrentsReceived(const QVector<tgui::Torrent>& torrents);
    void onTorrentDetailsReceived(const tgui::TorrentDetails& details);
    void onReadError(const QString& what, const QString& error);
    void onRequestCompleted(const QString& method, bool ok, const QString& error,
                            const QJsonObject& arguments);
    void onStartSelected();
    void onStopSelected();
    void onStartAll();
    void onStopAll();
    void onRemoveSelected();
    void onAbout();
    void onContextMenu(const QPoint& pos);
    void downloadAndAdd(const tgui::RpcClient::AddTorrent& params, const QString& dst);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void createUi();
    void createActions();
    void createTray();
    static QPixmap makeTrayIcon();
    void startConnect(const tgui::RpcClient::Options& opts);
    void maybeAutoConnect();
    void setConnectedUi(bool connected);
    void updateStatusBarTotals();
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
    GroupedTorrentProxy m_groupProxy;
    QTimer m_refreshTimer;

    QTreeView* m_view = nullptr;
    QComboBox* m_statusFilter = nullptr;
    QComboBox* m_grouping = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_daemonLabel = nullptr;
    QLabel* m_speedLabel = nullptr;

    QAction* m_actConnect = nullptr;
    QAction* m_actDisconnect = nullptr;
    QAction* m_actAdd = nullptr;
    QAction* m_actStart = nullptr;
    QAction* m_actStop = nullptr;
    QAction* m_actStartAll = nullptr;
    QAction* m_actStopAll = nullptr;
    QAction* m_actRemove = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actAutoRefresh = nullptr;
    QAction* m_actAbout = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
};
