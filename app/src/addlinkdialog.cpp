#include "addlinkdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

AddLinkDialog::AddLinkDialog(const QString& initialLink, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add from link"));

    m_link = new QLineEdit(initialLink, this);
    m_link->setObjectName(QStringLiteral("linkEdit"));
    m_link->setPlaceholderText(tr("magnet:?xt=… or http(s)://… .torrent"));
    m_start = new QCheckBox(tr("Start when added"), this);
    m_start->setChecked(true);
    m_status = new QLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("statusLabel"));

    auto* form = new QFormLayout(this);
    form->addRow(tr("Link"), m_link);
    form->addRow(QString(), m_start);
    form->addRow(QString(), m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddLinkDialog::onOk);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_link->setFocus();
}

QString AddLinkDialog::link() const
{
    return m_link->text().trimmed();
}

bool AddLinkDialog::startWhenAdded() const
{
    return m_start->isChecked();
}

void AddLinkDialog::onOk()
{
    const QString text = m_link->text().trimmed();
    if (text.isEmpty()) {
        m_link->setFocus();
        m_status->setText(tr("No link was specified."));
        return; // остаёмся в диалоге
    }
    accept();
}