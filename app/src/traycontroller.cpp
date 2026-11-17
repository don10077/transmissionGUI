#include "traycontroller.h"

#include <QCloseEvent>
#include <QMenu>
#include <QSettings>
#include <QSystemTrayIcon>

#include "statusbarcontroller.h"
#include "theme.h"

TrayController::TrayController(QWidget& window, StatusBarController& status, QAction& startAll,
                               QAction& stopAll, QAction& refresh)
    : m_window(window), m_status(status), m_startAll(startAll), m_stopAll(stopAll), m_refresh(refresh)
{
}

void TrayController::updateVisibility()
{
    const bool enabled = QSettings().value("ui/trayIcon", true).toBool();
    if (!m_tray && enabled)
        createTray();
    if (m_tray)
        m_tray->setVisible(enabled);
}

bool TrayController::notify(const QString& title, const QString& message)
{
    if (!m_tray || !m_tray->isVisible() || !QSystemTrayIcon::supportsMessages())
        return false;
    m_tray->showMessage(title, message);
    return true;
}

void TrayController::downloadCompleted(const QString& message)
{
    if (m_tray && m_tray->isVisible())
        m_tray->showMessage(tr("Download completed"), message, QSystemTrayIcon::Information);
}

bool TrayController::handleClose(QCloseEvent* event)
{
    const bool closeToTray = QSettings().value("ui/closeToTray", true).toBool();
    if (m_quitRequested || !closeToTray || !m_tray || !m_tray->isVisible())
        return false;
    event->ignore();
    m_window.hide();
    return true;
}

void TrayController::createTray()
{
    using tgui::theme::Icon;
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    // W07: не создавать иконку, если в настройках выключен «always show tray icon».
    if (!QSettings().value("ui/trayIcon", true).toBool())
        return;
    m_tray = new QSystemTrayIcon(&m_window);
    m_tray->setIcon(tgui::theme::icon(tgui::theme::Icon::App));
    m_tray->setToolTip(QStringLiteral(TGUI_DISPLAY_NAME));
    auto* menu = new QMenu(&m_window);
    auto* actShow = menu->addAction(tr("Show"));
    auto* actStartAll = menu->addAction(tgui::theme::icon(Icon::Start), tr("Start all"));
    auto* actStopAll = menu->addAction(tgui::theme::icon(Icon::Pause), tr("Pause all"));
    menu->addMenu(m_status.createSpeedLimitMenu(menu, true));
    menu->addMenu(m_status.createSpeedLimitMenu(menu, false));
    menu->addSeparator();
    auto* actRefresh = menu->addAction(tgui::theme::icon(Icon::Refresh), tr("Refresh"));
    menu->addSeparator();
    auto* actExit = menu->addAction(tr("Exit"));

    QObject::connect(actShow, &QAction::triggered, this, [this] {
        m_window.show();
        m_window.activateWindow();
    });
    QObject::connect(actStartAll, &QAction::triggered, &m_startAll, &QAction::trigger);
    QObject::connect(actStopAll, &QAction::triggered, &m_stopAll, &QAction::trigger);
    QObject::connect(actRefresh, &QAction::triggered, &m_refresh, &QAction::trigger);
    connect(actExit, &QAction::triggered, this, &TrayController::onExit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick ||
                    reason == QSystemTrayIcon::Trigger) {
                    m_window.show();
                    m_window.activateWindow();
                }
            });
    m_tray->show();
}

void TrayController::onExit()
{
    m_quitRequested = true;
    if (m_window.close())
        QCoreApplication::quit();
    else
        m_quitRequested = false;
}

void TrayController::handleStateChange(QEvent* event)
{
    // W07: «minimize to tray» — перехватываем переход окна в минимизированное состояние.
    if (event->type() == QEvent::WindowStateChange) {
        auto* wse = static_cast<QWindowStateChangeEvent*>(event);
        // Новое состояние — windowState() (уже обновлено к моменту события); старое — oldState().
        const bool nowMinimized = (m_window.windowState() & Qt::WindowMinimized) != 0;
        const bool wasMinimized = (wse->oldState() & Qt::WindowMinimized) != 0;
        if (nowMinimized && !wasMinimized &&
            QSettings().value("ui/minimizeToTray", false).toBool() &&
            m_tray && m_tray->isVisible()) {
            m_window.showNormal();
            m_window.hide();
            return;
        }
    }
}
