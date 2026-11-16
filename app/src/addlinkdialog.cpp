#include "addlinkdialog.h"
#include "externalopen.h"

#include "theme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

AddLinkDialog::AddLinkDialog(const QString& initialLink, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add torrent link"));
    resize(520, 0);

    m_link = new QLineEdit(initialLink, this);
    m_link->setObjectName(QStringLiteral("linkEdit"));
    m_link->setPlaceholderText(tr("magnet:?xt=… or https://… .torrent"));
    m_start = new QCheckBox(tr("Start after adding"), this);
    m_start->setChecked(true);
    m_status = new QLabel(QString(), this);
    m_status->setObjectName(QStringLiteral("statusLabel"));

    auto* note = new QLabel(tr("A link from the clipboard is inserted automatically if enabled in the options."), this);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);

    auto* form = tgui::theme::formGrid(this);
    tgui::theme::addFormRow(form, tr("Link to .torrent or magnet:"), m_link);
    tgui::theme::addFormRow(form, QString(), m_start);
    tgui::theme::addFullRow(form, note);
    tgui::theme::addFormRow(form, QString(), m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    tgui::theme::addFullRow(form, buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddLinkDialog::onOk);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const auto validate = [this, buttons] {
        const bool valid = !link().isEmpty();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
        m_status->setText(!valid && !m_link->text().trimmed().isEmpty()
            ? tr("Enter a valid magnet or HTTP(S) link.") : QString{});
    };
    connect(m_link, &QLineEdit::textChanged, this, validate);
    validate();

    m_link->setFocus();
    tgui::theme::decorateDialog(this);
}

QString AddLinkDialog::link() const
{
    return torrentLinkInput(m_link->text());
}

bool AddLinkDialog::startWhenAdded() const
{
    return m_start->isChecked();
}

void AddLinkDialog::onOk()
{
    if (link().isEmpty()) {
        m_link->setFocus();
        return;
    }
    accept();
}
