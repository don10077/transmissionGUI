#include "movedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

MoveDialog::MoveDialog(const QString& currentDir, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Move torrent"));
    resize(560, 120);

    auto* layout = new QVBoxLayout(this);

    auto* row = new QHBoxLayout();
    auto* browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &MoveDialog::onBrowse);
    m_path = new QLineEdit(this);
    m_path->setText(currentDir);
    m_path->setPlaceholderText(tr("Folder for the torrent files"));
    row->addWidget(m_path, 1);
    row->addWidget(browse);
    layout->addLayout(row);

    m_rename = new QCheckBox(tr("Rename files to the torrent name"), this);
    layout->addWidget(m_rename);

    auto* info = new QLabel(
        tr("The daemon will move the files to the specified folder. "
           "Rename creates a subfolder named after the torrent (rename-partially)."),
        this);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Move"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_path->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Move"), tr("Specify a folder."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString MoveDialog::targetDir() const
{
    return m_path->text().trimmed();
}

bool MoveDialog::renameFiles() const
{
    return m_rename->isChecked();
}

void MoveDialog::onBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Folder for the torrent files"), m_path->text());
    if (!dir.isEmpty())
        m_path->setText(dir);
}