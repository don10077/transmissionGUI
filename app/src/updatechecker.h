#pragma once

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QTimer>

#include <functional>

class QMainWindow;

/** Проверяет опубликованные выпуски вручную и по расписанию. */
class UpdateChecker final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    using Notify = std::function<void(const QString& title, const QString& message)>;

    /** Привязывает диалоги к окну и задаёт способ фонового уведомления. */
    UpdateChecker(QMainWindow& window, Notify notify);
    /** Планирует фоновую проверку по настройкам. */
    void updateCheckSchedule();
    /** Запрашивает последний выпуск. */
    void checkUpdate(bool requestedManually);
    /** Останавливает фоновую проверку после импорта настроек. */
    void suspend();

private:
    QMainWindow& m_window;
    Notify m_notify;
    QNetworkAccessManager m_nam;
    QTimer m_updateCheckTimer;
    bool m_settingsImported = false;
    bool m_checkingUpdate = false;
    bool m_manualUpdateCheck = false;
    bool m_updateTimerIsWake = false;
};
