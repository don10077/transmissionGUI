#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTabWidget>

#include <functional>
#include <optional>

#include "detailmodels.h"
#include "tgui/models.h"

class ConnectionController;
class FlagImageLoader;
class GeoIpUpdater;
class QLabel;
class PropsForm;
class QTableView;
class QTreeView;
class StatsForm;
class StatusBarController;
class TorrentActions;
class TorrentModel;

/** Отображает вкладки текущего торрента и загружает детали видимой вкладки. */
class DetailsPanel final : public QTabWidget {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    using MapPath = std::function<QString(const QString&)>;
    using OpenPath = std::function<void(const QString&, bool)>;
    using SelectionCount = std::function<qsizetype()>;

    /** Создаёт вкладки с данными активной сессии. */
    DetailsPanel(QWidget* parent, const ConnectionController& connection, const TorrentModel& model,
                 const tgui::SessionInfo& session, MapPath map, OpenPath open, SelectionCount count);
    /** Подключает обслуживающие команды после построения контроллеров. */
    void setServices(TorrentActions& actions, GeoIpUpdater& geoIp, FlagImageLoader& flags,
                     StatusBarController& status);

    int currentId() const { return m_currentId; }
    const FileListModel& fileModel() const { return m_fileModel; }
    PeerListModel& peerModel() { return m_peerModel; }
    const PeerListModel& peerModel() const { return m_peerModel; }
    const TrackerListModel& trackerModel() const { return m_trackerModel; }
    PropsForm* generalForm() const { return m_general; }
    StatsForm* statsForm() const { return m_statsForm; }

    /** Сбрасывает детали и запрашивает вкладку при смене текущего торрента. */
    void setCurrentTorrent(int id);
    /** Обновляет детали из свежего снимка списка. */
    void applySnapshot(int nextId, bool reconnecting);
    /** Отменяет применение устаревших RPC-ответов. */
    void suspend();
    /** Очищает вкладки при смене сессии. */
    void clear();
    /** Запрашивает файлы после команды или восстанавливает отображение при ошибке. */
    void refreshFilesAfterAction(bool ok);

    void updateInfoTitleWithCount(qsizetype count);
    void refreshFilesDetails();
    void onTorrentDetailsReceived(const tgui::TorrentDetails& details);
    void onPeersReceived(int id, const QVector<tgui::Peer>& peers);
    void onTrackersReceived(int id, const QVector<tgui::TrackerStat>& trackers);

protected:
    /** Подгоняет заголовок под ширину вкладок. */
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateInfoTitle();
    void refreshGeneralDetails();
    void updateGeneralTorrent(const tgui::Torrent& torrent);
    void resetFiles();
    void requestFiles(bool forceFull = false);
    void onFilesDoubleClicked(int row);
    void onFileWantToggled(int fileIndex, bool wanted);
    void onFilesContextMenu(const QPoint& pos);
    void onPeersContextMenu(const QPoint& pos);
    void onFilePriority(const QVector<int>& fileIds, int priority);
    void setFilesDownload(const QVector<int>& fileIds, bool wanted);
    void onFilesWantToggled(const QVector<int>& rows, bool wanted);
    QString currentDownloadDir() const;
    void onFilesOpen(const QModelIndex& index);
    void onFilesOpenFolder(const QModelIndex& index);
    void onFilesCopyPath(const QModelIndex& index);
    void onFilesRename(const QModelIndex& index);
    void onTrackersContextMenu(const QPoint& pos);
    bool eventFilter(QObject* watched, QEvent* event) override;
    void onAddTracker();
    void onEditTracker(int trackerId, const QString& announce);
    void onRemoveTracker(int trackerId, const QString& announce);

    const ConnectionController& m_connection;
    const TorrentModel& m_model;
    const tgui::SessionInfo& m_sessionInfo;
    MapPath mappedLocalPath;
    OpenPath openLocalPath;
    SelectionCount m_selectionCount;
    TorrentActions* m_actions = nullptr;
    GeoIpUpdater* m_geoIp = nullptr;
    FlagImageLoader* m_flags = nullptr;
    StatusBarController* m_status = nullptr;
    FileListModel m_fileModel;
    PeerListModel m_peerModel;
    TrackerListModel m_trackerModel;
    QLabel* m_infoTitle = nullptr;
    QLabel* m_peerEmpty = nullptr;
    PropsForm* m_general = nullptr;
    StatsForm* m_statsForm = nullptr;
    QTreeView* m_filesView = nullptr;
    QTableView* m_peersView = nullptr;
    QTableView* m_trackersView = nullptr;
    FileTreeModel* m_fileTree = nullptr;
    int m_currentId = -1;
    int m_filePieceCount = 0;
    QElapsedTimer m_filesSnapshot;
    quint64 m_filesRequest = 0;
    bool m_filesLoading = false;
    quint64 m_generalRequest = 0;
    bool m_generalLoading = false;
    std::optional<tgui::Torrent> m_generalDetails;
};
