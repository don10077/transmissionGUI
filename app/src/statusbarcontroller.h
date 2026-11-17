#pragma once

#include <QCoreApplication>
#include <QTimer>

#include <functional>

#include "tgui/rpcclient.h"

class QLabel;
class QMainWindow;
class QMenu;
class QToolButton;
class QTreeView;
class TorrentModel;

/** Управляет сегментами строки состояния, меню лимитов и уведомлениями. */
class StatusBarController final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    using Client = std::function<tgui::RpcClient*()>;
    using Profile = std::function<QString()>;

    /** Использует общую модель торрентов и данные текущей сессии. */
    StatusBarController(QMainWindow& window, const TorrentModel& model,
                        const tgui::SessionInfo& session, Client client, Profile profile);
    /** Создаёт строку состояния после построения основного интерфейса. */
    void initialize(QTreeView& view);
    /** Отражает доступность команд демона. */
    void setConnected(bool on);
    /** Показывает состояние подключения. */
    void setConnectionState(const QString& text, const QColor& color);
    const QString& connectionStateText() const { return m_connectionStateText; }
    /** Пересчитывает счётчики и суммарные скорости. */
    void updateStatusBarTotals();
    /** Показывает краткое уведомление. */
    void showToast(const QString& message);
    /** Показывает сообщение в строке состояния на указанный срок. */
    void showMessage(const QString& message, int durationMs);
    /** Создаёт меню управления лимитом скорости. */
    QMenu* createSpeedLimitMenu(QWidget* parent, bool download);

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void positionToast();
    void updateStatusBarPresentation();

    QMainWindow& m_window;
    const TorrentModel& m_model;
    const tgui::SessionInfo& m_sessionInfo;
    Client m_client;
    Profile m_profile;
    QTreeView* m_view = nullptr;
    QList<QMenu*> m_speedLimitMenus;
    bool m_connected = false;
    QWidget* m_statusStrip = nullptr;
    QWidget* m_statusConnection = nullptr;
    QToolButton* m_statusDownload = nullptr;
    QToolButton* m_statusUpload = nullptr;
    QWidget* m_statusAlternative = nullptr;
    QLabel* m_statusFree = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_statusText = nullptr;
    QLabel* m_statusDot = nullptr;
    QLabel* m_toast = nullptr;
    QTimer m_toastTimer;
    QString m_connectionStateText;
    double m_statusDownRate = 0;
    double m_statusUpRate = 0;
};
