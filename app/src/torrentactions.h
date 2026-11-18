#pragma once

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QTimer>

#include <functional>

#include "tgui/rpcclient.h"

class QAction;
class ConnectionController;
class QMainWindow;
class StatusBarController;
class TorrentModel;

/** Выполняет команды над торрентами и обрабатывает ответы демона. */
class TorrentActions final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    using Selection = std::function<QVector<int>()>;
    using CurrentTorrent = std::function<int()>;
    enum class RefreshAfter { List, Files, Trackers, Properties };
    using RefreshDetails = std::function<void(RefreshAfter, bool)>;

    /** Подключает команды к текущему выбору, профилю и данным окна. */
    TorrentActions(QMainWindow& window, const ConnectionController& connection,
                   const TorrentModel& model, StatusBarController& status, QStringList& labels,
                   Selection selection, CurrentTorrent current, RefreshDetails refresh,
                   std::function<void()> labelsChanged, std::function<void()> displayNamesChanged = {});

    /** Создаёт общие действия главного меню и панели инструментов. */
    void createActions();
    /** Применяет доступность команд для состояния соединения. */
    void setConnected(bool connected);
    /** Обновляет доступность команд над текущим выделением. */
    void updateSelection(qsizetype count);

    QAction* startAction() const { return m_actStart; }
    QAction* stopAction() const { return m_actStop; }
    QAction* startAllAction() const { return m_actStartAll; }
    QAction* stopAllAction() const { return m_actStopAll; }
    QAction* removeAction() const { return m_actRemove; }
    QAction* propertiesAction() const { return m_actProperties; }
    QAction* moveAction() const { return m_actMove; }

    /** Отображает ошибку чтения данных демона. */
    void onReadError(const QString& what, const QString& error);
    /** Обрабатывает ответ на команду и при необходимости обновляет сессию. */
    void onRequestCompleted(const QString& method, bool ok, const QString& error,
                            const QJsonObject& arguments);
    /** Запускает проверку нового пути при переносе данных. */
    void checkPendingMove();
    /** Приостанавливает проверку до восстановления подключения. */
    void suspend();
    /** Сбрасывает проверку при смене сессии. */
    void clearPendingMove();

    // Команды, привязанные к действиям меню и панели инструментов.
    void onStartSelected();
    void onStopSelected();
    void onStartAll();
    void onStopAll();
    void onRemoveSelected();
    void onShowProperties();
    void onMoveSelected();
    void onSetLabels();
    void onRemoveWithDataSelected();
    void onVerifySelected();
    void onReannounceSelected();
    void onQueueTop();
    void onQueueUp();
    void onQueueDown();
    void onQueueBottom();
    void onStartForceSelected();
    void onUpdateBlocklist();
    void onRenameSelected();
    void onRenameDisplayNameSelected();
    void onPriorityHigh();
    void onPriorityNormal();
    void onPriorityLow();

    tgui::RpcClient::RequestCallback afterTorrentChange(const QString& method,
                                                        RefreshAfter refresh, int id = -1,
                                                        const QString& successMessage = {});
    void runTorrentAction(const QString& method, const QVector<int>& ids,
                          const QJsonObject& params = {},
                          std::function<void()> onSuccess = {});
    /** Открывает свойства торрента на указанной вкладке. */
    void showProperties(int id, int page);

private:

    QAction* m_actStart = nullptr;
    QAction* m_actStop = nullptr;
    QAction* m_actStartAll = nullptr;
    QAction* m_actStopAll = nullptr;
    QAction* m_actRemove = nullptr;
    QAction* m_actProperties = nullptr;
    QAction* m_actMove = nullptr;

    QMainWindow& m_window;
    const ConnectionController& m_connection;
    const TorrentModel& m_model;
    StatusBarController& m_status;
    QStringList& m_labels;
    Selection m_selection;
    CurrentTorrent m_currentId;
    RefreshDetails m_refreshDetails;
    std::function<void()> m_labelsChanged;
    std::function<void()> m_displayNamesChanged;
    // Ожидание подтверждённого нового downloadDir после переноса.
    QVector<int> m_movePendingIds;
    QString m_movePendingTarget;
    QDeadlineTimer m_moveDeadline;
    quint64 m_moveGeneration = 0;
    QTimer m_moveVerifyTimer;
};
