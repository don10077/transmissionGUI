#pragma once

#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>

#include <functional>
#include <optional>

#include "tgui/rpcclient.h"

class ConnectionController;
class QMainWindow;
class TorrentModel;
class WatchFolderService;

/** Последовательно добавляет файлы и ссылки в активную сессию демона. */
class AddTorrentCoordinator final : public QObject {
    Q_OBJECT

public:
    using CurrentTorrent = std::function<int()>;
    enum class AddOrigin { Interactive, WatchFolder, External };

    /** Подключает очередь к сеансу, списку торрентов и наблюдаемой папке. */
    AddTorrentCoordinator(QMainWindow& window, const ConnectionController& connection,
                          const TorrentModel& model, WatchFolderService& watch,
                          CurrentTorrent currentTorrent);

    /** Принимает файлы и ссылки командной строки, Проводника и перетаскивания. */
    void openExternalTorrents(const QStringList& inputs);
    /** Добавляет ожидавшие подключения файлы и ссылки. */
    void openPendingExternalTorrents();
    /** Открывает выбор файлов .torrent. */
    void onAddTorrent();
    /** Открывает диалог добавления ссылки. */
    void onAddFromLink();
    /** Импортирует список magnet-ссылок. */
    void onImportTorrentList();
    /** Ставит команду добавления в очередь текущего профиля. */
    void startAdd(const tgui::RpcClient::AddTorrent& params, AddOrigin origin = AddOrigin::Interactive);
    /** Принимает готовый файл из наблюдаемой папки. */
    void addWatchedFile(const QString& path);
    /** Проверяет, можно ли принимать фоновый файл. */
    bool canAcceptWatchedFile() const;
    /** Отмечает активное добавление как прерванное обрывом связи. */
    void interrupt();
    /** Снимает блокировку очереди перед повторным подключением. */
    void prepareRetry();
    /** Сбрасывает команды завершённой сессии и возвращает число отброшенных. */
    qsizetype clear();
    /** Проверяет результат оборванного добавления и возобновляет очередь. */
    void resumeAddQueue(const QVector<tgui::Torrent>& torrents);

signals:
    void torrentAdded(int id);
    void readError(const QString& what, const QString& error);
    /** Локальные отображаемые имена изменились. */
    void displayNamesChanged();

private:
    struct PendingAdd {
        tgui::RpcClient::AddTorrent params;
        AddOrigin origin;
        QString profile;
    };
    struct AddChoices {
        tgui::RpcClient::AddTorrent options;
        QString saveAs;
        QVector<int> unwanted;
        QVector<tgui::FileStat> files;
        QString displayName;
        bool displayNameEdited = false;
    };
    void addNextTorrent();
    void submitTorrent(PendingAdd pending, QByteArray sourceBytes = {},
                       std::optional<AddChoices> choices = {});
    void offerDuplicateUpdate(const tgui::RpcClient::AddTorrent& source,
                              const QJsonObject& duplicate, const QByteArray& metainfo,
                              const QString& displayName = {});
    void finishAdd(const QPointer<tgui::RpcClient>& rpc, int selectedId = -1);

    QMainWindow& m_window;
    const ConnectionController& m_connection;
    const TorrentModel& m_model;
    WatchFolderService& m_watchService;
    CurrentTorrent m_currentId;
    QNetworkAccessManager m_nam;
    QQueue<PendingAdd> m_addQueue;
    QStringList m_pendingExternalTorrents;
    bool m_addingTorrent = false;
    bool m_addInterrupted = false;
    int m_unconfiguredTorrentId = -1;
    QString m_unconfiguredTorrentHash;
};
