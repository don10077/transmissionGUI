#include "propsdialog.h"

#include <QDialogButtonBox>
#include "propsform.h"
#include <QPushButton>
#include <QVBoxLayout>

PropsDialog::PropsDialog(const tgui::Torrent& t, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Properties — %1").arg(t.name.value_or(tr("(no name)"))));
    resize(520, 420);

    // Содержимое — общая с вкладкой «General» панель (см. WORKFLOW.md).
    auto* form = new PropsForm(this);
    form->setTorrent(t);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    if (auto* close = buttons->button(QDialogButtonBox::Close))
        close->setDefault(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(form);
    layout->addWidget(buttons);
}