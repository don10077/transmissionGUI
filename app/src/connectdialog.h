#pragma once

#include <QDialog>

#include "tgui/rpcclient.h"

class QLineEdit;
class QCheckBox;

// Диалог повторной авторизации (design/mockup.html, DLG.pass):
// пояснение с профилем и адресом, пользователь, пароль, «Запомнить пароль».
class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectDialog(const tgui::RpcClient::Options& initial, QWidget* parent = nullptr,
                           const QString& profile = {});

    tgui::RpcClient::Options options() const;
    bool rememberPassword() const;

private:
    tgui::RpcClient::Options m_initial;
    QLineEdit* m_user;
    QLineEdit* m_pass;
    QCheckBox* m_remember;
};
