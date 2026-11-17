#pragma once

#include <QTimer>

#include <memory>
#include <optional>

#include "tgui/rpcclient.h"

class QWidget;

// Экспоненциальная задержка повторного подключения: 1s → 2s → … → 60s.
inline constexpr int kReconnectBaseDelayMs = 1000;
inline constexpr int kReconnectMaxDelayMs = 60000;

/** Управляет клиентом RPC, выбором профиля и восстановлением подключения. */
class ConnectionController final : public QObject {
    Q_OBJECT

public:
    /** Готовит контроллер с параметрами запуска. */
    ConnectionController(QWidget& window, const tgui::RpcClient::Options& startupOptions);

    tgui::RpcClient* client() const { return m_rpc.get(); }
    const QString& profile() const { return m_connectionProfile; }
    bool isConnected() const { return m_connected; }
    bool isReconnecting() const { return m_reconnecting; }
    bool hasScheduledRetry() const { return m_reconnectOptions.has_value(); }

    /** Завершает повторное подключение после первого свежего списка торрентов. */
    bool finishReconnect();
    /** Отменяет запросы при уничтожении главного окна. */
    void shutdown();
    /** Подключает к текущему профилю или открывает управление профилями. */
    void onConnect();
    /** Завершает текущий сеанс и сбрасывает запланированное подключение. */
    void onDisconnect();
    /** Вызывает подключение из командной строки или из сохранённого профиля. */
    void maybeAutoConnect();
    /** Применяет параметры автоподключения, переданные после создания окна. */
    void setAutoConnect(const tgui::RpcClient::Options& opts);
    /** Открывает параметры профилей и применяет выбранный профиль. */
    void onConnectionOptions();
    /** Переключает сеанс на заданный профиль. */
    void switchToProfile(const QString& name,
                         std::optional<tgui::RpcClient::Options> sessionOptions = std::nullopt);

signals:
    void clientChanged(tgui::RpcClient* client);
    void connecting(const QUrl& address);
    void connected(const tgui::SessionInfo& info);
    void suspended();
    void interrupted();
    void clearing();
    void cleared();
    void manuallyDisconnected();
    void retryScheduled(int delayMs, const QString& error);
    void failed();
    void authenticationRequired();
    void profileRenamed();
    void readError(const QString& what, const QString& error);
    void message(const QString& text, int durationMs);

private:
    void startConnect(const tgui::RpcClient::Options& opts, bool retry = false);
    void suspendConnection();
    void clearConnection();
    void resetClient();
    void handleConnectionError(const QString& error, bool retry);
    void onConnectionFailed(const QString& error);
    void onAuthenticationFailed(const QString& error);
    void onClientTlsPassphraseRequired();

    QWidget& m_window;
    QString m_connectionProfile;
    bool m_connected = false;
    QTimer m_reconnectTimer;
    std::optional<tgui::RpcClient::Options> m_reconnectOptions;
    int m_reconnectDelayMs = kReconnectBaseDelayMs;
    bool m_reconnecting = false;
    tgui::RpcClient::Options m_autoConnectOpts;
    std::unique_ptr<tgui::RpcClient> m_rpc;
};
