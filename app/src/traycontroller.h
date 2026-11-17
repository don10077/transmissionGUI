#pragma once

#include <QCoreApplication>

class QAction;
class QCloseEvent;
class QSystemTrayIcon;
class StatusBarController;
class QWidget;

/** Управляет значком, уведомлениями и сворачиванием в системный трей. */
class TrayController final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    /** Подключает команды трея к действиям главного окна. */
    TrayController(QWidget& window, StatusBarController& status, QAction& startAll,
                   QAction& stopAll, QAction& refresh);
    /** Применяет настройку видимости значка. */
    void updateVisibility();
    /** Показывает уведомление, если трей доступен и виден. */
    bool notify(const QString& title, const QString& message);
    /** Уведомляет о завершении загрузки. */
    void downloadCompleted(const QString& message);
    /** Закрывает окно и завершает приложение. */
    void onExit();
    /** Скрывает окно, если включено закрытие в трей. */
    bool handleClose(QCloseEvent* event);
    /** Обрабатывает сворачивание окна в системный трей. */
    void handleStateChange(QEvent* event);

private:
    void createTray();

    QWidget& m_window;
    StatusBarController& m_status;
    QAction& m_startAll;
    QAction& m_stopAll;
    QAction& m_refresh;
    QSystemTrayIcon* m_tray = nullptr;
    bool m_quitRequested = false;
};
