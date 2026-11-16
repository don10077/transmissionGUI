#include "movedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include "daemonprofile.h"
#include "pathmap.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include "theme.h"

MoveDialog::MoveDialog(const QString& currentDir, QWidget* parent, const QString& profile, bool localDaemon)
    : QDialog(parent), m_mappings(ProfilePreferences(profile).mappings()), m_localDaemon(localDaemon)
{
    setWindowTitle(tr("Move torrent"));
    resize(560, 120);

    auto* form = tgui::theme::formGrid(this);

    auto* row = new QHBoxLayout();
    auto* browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &MoveDialog::onBrowse);
    m_path = new QComboBox(this);
    m_path->setEditable(true);
    m_path->setInsertPolicy(QComboBox::NoInsert);
    m_path->addItems(ProfilePreferences(profile).moveHistory());
    m_path->setCurrentText(currentDir);
    if (m_path->lineEdit())
        m_path->lineEdit()->setPlaceholderText(tr("Folder for the torrent files"));
    row->addWidget(m_path, 1);
    row->addWidget(browse);
    tgui::theme::addFormRow(form, tr("Destination folder:"), row);

    m_rename = new QCheckBox(tr("Move existing data"), this);
    m_rename->setChecked(true);
    tgui::theme::addFormRow(form, QString(), m_rename);

    auto* info = new QLabel(
        tr("The destination is a folder on the daemon. Uncheck Move existing data to locate files that have already been moved."),
        this);
    info->setObjectName(QStringLiteral("hintLabel"));
    info->setWordWrap(true);
    tgui::theme::addFullRow(form, info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Move"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_path->currentText().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Move"), tr("Specify a folder."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    tgui::theme::addFullRow(form, buttons);
    tgui::theme::decorateDialog(this);
}

QString MoveDialog::targetDir() const
{
    return m_path->currentText().trimmed();
}

bool MoveDialog::moveData() const
{
    return m_rename->isChecked();
}

void MoveDialog::onBrowse()
{
    const QString local = tgui::mapRemoteToLocal(targetDir(), m_mappings).value_or(QString{});
    if (!QFileInfo(local).isDir()) {
        QMessageBox::warning(this, tr("Move"),
            tr("Folder is not accessible locally: %1. Check path mappings or enter the daemon path manually.").arg(local));
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Folder for the torrent files"), local);
    if (dir.isEmpty())
        return;
    const auto mapped = tgui::mapLocalToRemote(dir, m_mappings);
    if (!mapped && !m_localDaemon) {
        QMessageBox::warning(this, windowTitle(),
            tr("This folder has no path mapping. Enter the daemon path or configure a mapping."));
        return;
    }
    m_path->setCurrentText(mapped.value_or(dir));
}
