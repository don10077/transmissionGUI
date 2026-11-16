#include "connectdialog.h"

#include "theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

ConnectDialog::ConnectDialog(const tgui::RpcClient::Options& initial, QWidget* parent,
                             const QString& profile)
    : QDialog(parent)
    , m_initial(initial)
{
    // design/mockup.html, DLG.pass: короткий диалог только с учётными данными.
    setWindowTitle(tr("Connect to Transmission"));
    resize(420, 0);

    const QString host = initial.url.host();
    QString address = host;
    if (initial.url.port() > 0)
        address += QLatin1Char(':') + QString::number(initial.url.port());
    if (address.isEmpty())
        address = initial.url.toDisplayString(QUrl::RemoveUserInfo);

    auto* note = new QLabel(
        profile.isEmpty()
            ? tr("Enter the password for the connection (%1).").arg(address)
            : tr("Enter the password for the connection \"%1\" (%2).").arg(profile, address),
        this);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);

    m_user = new QLineEdit(initial.username, this);
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_remember = new QCheckBox(tr("Remember password"), this);
    m_remember->setChecked(QSettings().value("connection/rememberPassword", false).toBool());

    auto* form = tgui::theme::formGrid(this);
    tgui::theme::addFullRow(form, note);
    tgui::theme::addFormRow(form, tr("User:"), m_user);
    tgui::theme::addFormRow(form, tr("Password:"), m_pass);
    tgui::theme::addFormRow(form, QString(), m_remember);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* connectButton = buttons->addButton(tr("Connect"), QDialogButtonBox::AcceptRole);
    connectButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    tgui::theme::addFullRow(form, buttons);

    m_pass->setFocus();
    tgui::theme::decorateDialog(this);
}

tgui::RpcClient::Options ConnectDialog::options() const
{
    auto opts = m_initial;
    opts.username = m_user->text().trimmed();
    opts.password = m_pass->text();
    return opts;
}

bool ConnectDialog::rememberPassword() const
{
    return m_remember->isChecked();
}
