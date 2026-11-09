#include "aboutdialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

QString AboutDialog::aboutText()
{
    return QObject::tr(
        "<h3>Transmission Remote GUI (Qt)</h3>"
        "<p>GUI client for the Transmission daemon (C++ / Qt 6).</p>"
        "<p>Author: <a href=\"mailto:don10077@mail.ru\">don10077@mail.ru</a></p>");
}

QString AboutDialog::licenseText()
{
    return QObject::tr(
        "This program is free software under the GNU General Public License, "
        "version 3 (or any later version). It is provided \"as is\", without a "
        "warranty of any kind. See "
        "https://www.gnu.org/licenses/gpl-3.0.html for the full license text.");
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("About"));

    auto* tabs = new QTabWidget(this);

    auto* aboutLabel = new QLabel(this);
    aboutLabel->setWordWrap(true);
    aboutLabel->setOpenExternalLinks(true);
    aboutLabel->setText(aboutText() +
                        tr("<p>Version: %1</p>").arg(QApplication::applicationVersion()));
    tabs->addTab(aboutLabel, tr("About"));

    auto* license = new QPlainTextEdit(this);
    license->setReadOnly(true);
    license->setPlainText(licenseText());
    tabs->addTab(license, tr("License"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    auto* root = new QVBoxLayout(this);
    root->addWidget(tabs, 1);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
}