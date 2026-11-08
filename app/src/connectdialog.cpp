#include "connectdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>

ConnectDialog::ConnectDialog(const tgui::RpcClient::Options& initial, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Подключение к демону"));

    m_url = new QLineEdit(initial.url.toString(QUrl::PrettyDecoded), this);
    m_url->setPlaceholderText(tr("127.0.0.1:9091 или http://хост:порт"));
    m_path = new QLineEdit(initial.rpcPath, this);
    m_path->setPlaceholderText(tr("/transmission/rpc"));
    m_user = new QLineEdit(initial.username, this);
    m_pass = new QLineEdit(initial.password, this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_ignoreSsl = new QCheckBox(tr("Игнорировать ошибки сертификата"), this);
    m_ignoreSsl->setChecked(initial.ignoreSslErrors);

    auto* form = new QFormLayout(this);
    form->addRow(tr("Адрес"), m_url);
    form->addRow(tr("RPC-путь"), m_path);
    form->addRow(tr("Пользователь"), m_user);
    form->addRow(tr("Пароль"), m_pass);
    form->addRow(QString(), m_ignoreSsl);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    m_url->setFocus();
}

tgui::RpcClient::Options ConnectDialog::options() const
{
    tgui::RpcClient::Options opts;
    QString url = m_url->text().trimmed();
    if (!url.isEmpty() && !url.startsWith("http://") && !url.startsWith("https://"))
        url = QStringLiteral("http://") + url;
    if (!url.isEmpty())
        opts.url = QUrl(url);
    opts.rpcPath = m_path->text().trimmed();
    opts.username = m_user->text().trimmed();
    opts.password = m_pass->text();
    opts.ignoreSslErrors = m_ignoreSsl->isChecked();
    return opts;
}