#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QStringList>

#include <functional>

#include "tgui/rpcclient.h"

class TorrentModel;
class QWidget;

/** Проверяет ссылки из буфера обмена и отсекает недавние повторы. */
class ClipboardMonitor final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    using Client = std::function<tgui::RpcClient*()>;
    using Add = std::function<void(const tgui::RpcClient::AddTorrent&)>;

    /** Подключает наблюдение к буферу обмена приложения. */
    ClipboardMonitor(QWidget& window, const TorrentModel& model, Client client, Add add);

private:
    void onClipboardChanged();

    QWidget& m_window;
    const TorrentModel& m_model;
    Client m_client;
    Add m_add;
    QStringList m_clipboardLinksSeen;
};
