#include "connectioncontroller.h"

#include <QCoreApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>

#include <algorithm>
#include <utility>

#include "connectdialog.h"
#include "connoptionsdialog.h"
#include "daemonprofile.h"

ConnectionController::ConnectionController(QWidget& window, const tgui::RpcClient::Options& startupOptions)
    : m_window(window), m_autoConnectOpts(startupOptions)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_reconnectOptions)
            startConnect(*std::exchange(m_reconnectOptions, std::nullopt), true);
    });
}

void ConnectionController::shutdown()
{
    m_reconnectTimer.stop();
    m_connected = false;
    if (m_rpc) {
        m_rpc->disconnect();
        m_rpc.reset();
    }
}

bool ConnectionController::finishReconnect()
{
    return m_connected && std::exchange(m_reconnecting, false);
}

void ConnectionController::onConnect()
{
    if (m_reconnectOptions) {
        startConnect(*std::exchange(m_reconnectOptions, std::nullopt), true);
        return;
    }
    if (m_rpc && !m_connected)
        return;
    // Единая модель (по OLD): «Connect» подключает к активному серверу (профилю).
    // Серверов нет — открыть управление, чтобы добавить (сначала задают в
    // «Manage connections…», затем переключаются из подменю «Connect to Transmission»).
    ProfileStore store;
    if (store.hasCurrent()) {
        switchToProfile(store.currentName());
        return;
    }
    onConnectionOptions();
}

void ConnectionController::startConnect(const tgui::RpcClient::Options& opts, bool retry)
{
    if (!retry && (m_rpc || m_reconnectOptions))
        clearConnection();
    m_reconnectTimer.stop();
    m_reconnectOptions.reset();
    m_reconnecting = retry;
    if (!retry)
        m_reconnectDelayMs = 1000;
    m_rpc = std::make_unique<tgui::RpcClient>(opts);
    connect(m_rpc.get(), &tgui::RpcClient::connected, this, [this](const tgui::SessionInfo& info) {
        m_reconnectDelayMs = 1000;
        m_connected = true;
        emit connected(info);
    });
    connect(m_rpc.get(), &tgui::RpcClient::connectionFailed, this,
            &ConnectionController::onConnectionFailed);
    connect(m_rpc.get(), &tgui::RpcClient::connectionInterrupted, this,
            [this](const QString& error) { handleConnectionError(error, true); });
    connect(m_rpc.get(), &tgui::RpcClient::authenticationFailed, this,
            &ConnectionController::onAuthenticationFailed);
    connect(m_rpc.get(), &tgui::RpcClient::clientTlsPassphraseRequired, this,
            &ConnectionController::onClientTlsPassphraseRequired);
    emit clientChanged(m_rpc.get());
    emit connecting(opts.url);
    m_rpc->connectToDaemon(retry);
}

void ConnectionController::setAutoConnect(const tgui::RpcClient::Options& opts)
{
    if (opts.url.isEmpty())
        return;
    m_autoConnectOpts = opts;
    // Вызывается после конструктора — подключаемся сразу.
    if (!m_rpc && !m_connected) {
        m_connectionProfile.clear();
        startConnect(opts);
    }
}

void ConnectionController::maybeAutoConnect()
{
    // W08: приоритет — параметры из командной строки; иначе — текущий профиль (ConnOptions).
    tgui::RpcClient::Options opts;
    if (!m_autoConnectOpts.url.isEmpty()) {
        opts = m_autoConnectOpts;
    } else {
        ProfileStore store;
        if (store.hasCurrent()) {
            m_connectionProfile = store.currentName();
            opts = store.profile(m_connectionProfile).toOptions();
        }
    }
    if (opts.url.isEmpty())
        return;
    startConnect(opts);
}

void ConnectionController::onDisconnect()
{
    m_connectionProfile.clear();
    m_reconnectTimer.stop();
    m_reconnectOptions.reset();
    m_reconnectDelayMs = 1000;
    clearConnection();
    emit manuallyDisconnected();
}

void ConnectionController::suspendConnection()
{
    m_connected = false;
    emit suspended();
}

void ConnectionController::clearConnection()
{
    suspendConnection();
    emit clearing();
    m_reconnecting = false;
    resetClient();
    emit cleared();
}

void ConnectionController::resetClient()
{
    m_rpc.reset();
    emit clientChanged(nullptr);
}

void ConnectionController::onConnectionFailed(const QString& error)
{
    handleConnectionError(error, false);
}

/** Планирует новую сессию после временного сбоя или показывает постоянную ошибку. */
void ConnectionController::handleConnectionError(const QString& error, bool retry)
{
    emit interrupted();
    suspendConnection();
    const QPointer<tgui::RpcClient> failedClient(m_rpc.get());
    QTimer::singleShot(0, this, [this, failedClient, error, retry] {
        if (!failedClient || failedClient != m_rpc.get())
            return;
        const auto opts = failedClient->options();
        bool reconnect = retry;
        if (reconnect && !m_connectionProfile.isEmpty())
            reconnect = ProfileStore().profile(m_connectionProfile).alwaysReconnect;
        if (reconnect) {
            resetClient();
            m_reconnectOptions = opts;
            const int delay = m_reconnectDelayMs;
            m_reconnectDelayMs = std::min(delay * 2, 60000);
            m_reconnectTimer.start(delay);
            emit retryScheduled(delay, error);
            return;
        }
        clearConnection();
        m_reconnectDelayMs = 1000;
        emit failed();
        QMessageBox::critical(&m_window, QCoreApplication::translate("MainWindow", "Connection failed"),
                              QCoreApplication::translate("MainWindow", "Address: %1\n\nReason: %2").arg(opts.url.toDisplayString(QUrl::RemoveUserInfo), error));
    });
}

void ConnectionController::onAuthenticationFailed(const QString& error)
{
    Q_UNUSED(error);
    suspendConnection();
    emit authenticationRequired();
    // Откладываем: даём развернуться стеку сигнала, чтобы безопасно показать
    // диалог повторной авторизации и переподключиться (старый клиент уничтожится).
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    QTimer::singleShot(0, this, [this, rpc] {
        if (!rpc || rpc != m_rpc.get())
            return;
        auto* dialog = new ConnectDialog(rpc->options(), &m_window, m_connectionProfile);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(rpc.data(), &QObject::destroyed, dialog, &QDialog::reject);
        connect(dialog, &QDialog::finished, this, [this, rpc, dialog](int result) {
            if (!rpc || rpc != m_rpc.get())
                return;
            if (result != QDialog::Accepted || dialog->options().url.isEmpty()) {
                onDisconnect();
                return;
            }
            const auto opts = dialog->options();
            const bool remember = dialog->rememberPassword();
            const auto profileName = m_connectionProfile;
            const auto& previous = rpc->options();
            startConnect(opts, m_reconnecting && opts.url == previous.url && opts.rpcPath == previous.rpcPath);
            if (!profileName.isEmpty() && m_rpc) {
                connect(m_rpc.get(), &tgui::RpcClient::connected, this,
                        [this, profileName, remember] {
                    ProfileStore store;
                    if (!m_rpc || profileName != m_connectionProfile || !store.exists(profileName))
                        return;
                    auto profile = store.profile(profileName);
                    const auto& active = m_rpc->options();
                    const auto saved = profile.toOptions();
                    if (saved.url != active.url || saved.rpcPath != active.rpcPath)
                        return;
                    profile.username = active.username;
                    profile.password = remember ? active.password : QString{};
                    if (!store.save(profile))
                        emit readError(QCoreApplication::translate("MainWindow", "Connect to Transmission"), QCoreApplication::translate("MainWindow", "Could not save the connection profiles."));
                }, Qt::SingleShotConnection);
            }
        });
        dialog->open();
    });
}

/** Запрашивает пароль файла ключа и повторяет подключение с тем же профилем. */
void ConnectionController::onClientTlsPassphraseRequired()
{
    suspendConnection();
    emit authenticationRequired();
    const QPointer<tgui::RpcClient> rpc(m_rpc.get());
    QTimer::singleShot(0, this, [this, rpc] {
        if (!rpc || rpc != m_rpc.get())
            return;
        auto* dialog = new QInputDialog(&m_window);
        dialog->setWindowTitle(QCoreApplication::translate("MainWindow", "Client TLS passphrase"));
        dialog->setLabelText(QCoreApplication::translate("MainWindow", "Passphrase for the client certificate or key:"));
        dialog->setTextEchoMode(QLineEdit::Password);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(rpc.data(), &QObject::destroyed, dialog, &QDialog::reject);
        connect(dialog, &QDialog::finished, this, [this, rpc, dialog](int result) {
            if (!rpc || rpc != m_rpc.get())
                return;
            if (result != QDialog::Accepted) {
                onDisconnect();
                return;
            }
            auto opts = rpc->options();
            opts.clientKeyPassphrase = dialog->textValue().toUtf8();
            startConnect(opts, m_reconnecting);
        });
        dialog->open();
    });
}

void ConnectionController::onConnectionOptions()
{
    // W08: диалог параметров соединений (несколько демонов, TLS, RPC-путь, прокси).
    ConnOptionsDialog dlg(&m_window);
    if (dlg.exec() != QDialog::Accepted)
        return;
    ProfileStore store;
    if (store.hasCurrent())
        switchToProfile(store.currentName(), dlg.current().toOptions());
}

void ConnectionController::switchToProfile(const QString& name,
                                 std::optional<tgui::RpcClient::Options> sessionOptions)
{
    if (name.isEmpty())
        return;
    ProfileStore store;
    if (!store.exists(name))
        return;
    // Уже подключены к этому серверу — не переключаем (OLD: DoConnectToHost
    // выходит, если FCurConn == выбранное имя).
    const auto opts = sessionOptions ? std::move(*sessionOptions) : store.profile(name).toOptions();
    if (m_connected && m_rpc) {
        const auto& active = m_rpc->options();
        if (active == opts) {
            store.setCurrent(name);
            m_connectionProfile = name;
            emit profileRenamed();
            return;
        }
    }
    onDisconnect();
    store.setCurrent(name);
    if (opts.url.isEmpty()) {
        emit message(QCoreApplication::translate("MainWindow", "Server '%1' has no host set.").arg(name), 4000);
        return;
    }
    m_connectionProfile = name;
    startConnect(opts);
}
