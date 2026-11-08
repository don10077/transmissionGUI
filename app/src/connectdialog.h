#pragma once

#include <QDialog>

#include "tgui/rpcclient.h"

class QLineEdit;
class QCheckBox;

// Диалог подключения к демону (адрес, RPC-путь, пользователь, пароль, SSL).
class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectDialog(const tgui::RpcClient::Options& initial, QWidget* parent = nullptr);

    tgui::RpcClient::Options options() const;

private:
    QLineEdit* m_url;
    QLineEdit* m_path;
    QLineEdit* m_user;
    QLineEdit* m_pass;
    QCheckBox* m_ignoreSsl;
};
