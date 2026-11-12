#pragma once

#include <QDialog>

#include "daemonprofile.h"

class QListWidget;
class QLineEdit;
class QSpinBox;
class QComboBox;
class QCheckBox;

// W08: диалог параметров соединений (несколько демонов, TLS, RPC-путь, прокси, проверка).
// Образец: transgui/connoptions.pas. Профили — ProfileStore (QSettings).
class ConnOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnOptionsDialog(QWidget* parent = nullptr);

    DaemonProfile current() const;        // профиль, отображённый в форме
    void setProfile(const DaemonProfile& p); // заполнить форму
    void saveCurrent();                   // записать форму в ProfileStore

private slots:
    void onProfileSelected();
    void onNew();
    void onDelete();
    void onRename();
    void onTest();

private:
    void refreshList();
    void selectProfile(const QString& name);

    ProfileStore m_store;
    QListWidget* m_list = nullptr;
    QLineEdit* m_host = nullptr;
    QSpinBox* m_port = nullptr;
    QComboBox* m_protocol = nullptr;
    QLineEdit* m_rpcPath = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_pass = nullptr;
    QCheckBox* m_ignoreSsl = nullptr;
    QLineEdit* m_clientCertificatePath = nullptr;
    QLineEdit* m_clientKeyPath = nullptr;
    QCheckBox* m_useProxy = nullptr;
    QLineEdit* m_proxyHost = nullptr;
    QSpinBox* m_proxyPort = nullptr;
    QLineEdit* m_proxyUser = nullptr;
    QLineEdit* m_proxyPass = nullptr;
    QCheckBox* m_useSocks = nullptr;
};
