#pragma once

#include <QDialog>

#include "daemonprofile.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QToolButton;
class QWidget;

// W08: параметры подключений (несколько демонов, TLS, RPC-путь, прокси, проверка).
// design/mockup.html, DLG.conn: профиль в ComboBox сверху, параметры — во вкладках.
class ConnOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnOptionsDialog(QWidget* parent = nullptr);

    DaemonProfile current() const;        // профиль, отображённый в форме
    void setProfile(const DaemonProfile& p); // заполнить форму

public slots:
    void accept() override;

private slots:
    void onProfileSelected();
    void onNew();
    void onDelete();
    void onRename();
    void onTest();

private:
    void refreshList();
    void selectProfile(const QString& name);
    void saveDraft();
    bool canUseName(const QString& name, const QString& title);

    ProfileStore m_store;
    QList<ProfileStore::Draft> m_drafts;
    QString m_editingName;
    QComboBox* m_profiles = nullptr;
    QLineEdit* m_host = nullptr;
    QSpinBox* m_port = nullptr;
    QComboBox* m_protocol = nullptr;
    QLineEdit* m_rpcPath = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_pass = nullptr;
    QCheckBox* m_requireAuth = nullptr;
    QCheckBox* m_alwaysReconnect = nullptr;
    QCheckBox* m_ignoreSsl = nullptr;
    QLabel* m_testStatus = nullptr;
    QToolButton* m_certToggle = nullptr;
    QWidget* m_certPanel = nullptr;
    QLineEdit* m_clientCertificatePath = nullptr;
    QLineEdit* m_clientKeyPath = nullptr;
    QCheckBox* m_useProxy = nullptr;
    QLineEdit* m_proxyHost = nullptr;
    QSpinBox* m_proxyPort = nullptr;
    QCheckBox* m_proxyAuth = nullptr;
    QLineEdit* m_proxyUser = nullptr;
    QLineEdit* m_proxyPass = nullptr;
    QCheckBox* m_useSocks = nullptr;
};
