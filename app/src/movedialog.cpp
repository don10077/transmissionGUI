#include "movedialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include "pathmap.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kMoveHistoryLimit = 10;
const char kMoveHistoryKey[] = "paths/moveDirs";
}  // namespace

MoveDialog::MoveDialog(const QString& currentDir, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Move torrent"));
    resize(560, 120);

    auto* layout = new QVBoxLayout(this);

    auto* row = new QHBoxLayout();
    auto* browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &MoveDialog::onBrowse);
    m_path = new QComboBox(this);
    m_path->setEditable(true);
    m_path->setInsertPolicy(QComboBox::NoInsert);
    m_path->addItems(QSettings().value(QLatin1String(kMoveHistoryKey)).toStringList());
    m_path->setCurrentText(currentDir);
    if (m_path->lineEdit())
        m_path->lineEdit()->setPlaceholderText(tr("Folder for the torrent files"));
    row->addWidget(m_path, 1);
    row->addWidget(browse);
    layout->addLayout(row);

    m_rename = new QCheckBox(tr("Move existing data"), this);
    m_rename->setChecked(true);
    layout->addWidget(m_rename);

    auto* info = new QLabel(
        tr("The destination is a folder on the daemon. Uncheck Move existing data to locate files that have already been moved."),
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
        if (m_path->currentText().trimmed().isEmpty()) {
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
    return m_path->currentText().trimmed();
}

bool MoveDialog::moveData() const
{
    return m_rename->isChecked();
}

void MoveDialog::rememberDirectory(const QString& dir)
{
    const QString target = dir.trimmed();
    if (target.isEmpty())
        return;
    QSettings settings;
    QStringList history = settings.value(QLatin1String(kMoveHistoryKey)).toStringList();
    history.removeAll(target);
    history.prepend(target);
    while (history.size() > kMoveHistoryLimit)
        history.removeLast();
    settings.setValue(QLatin1String(kMoveHistoryKey), history);
    settings.sync();
}

void MoveDialog::onBrowse()
{
    const auto mappings = QSettings().value("paths/mappings").toStringList();
    const QString local = QDir::fromNativeSeparators(tgui::mapRemoteToLocal(targetDir(), mappings));
    if (!QFileInfo(local).isDir()) {
        QMessageBox::warning(this, tr("Move"),
            tr("Folder is not accessible locally: %1. Check path mappings or enter the daemon path manually.").arg(local));
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Folder for the torrent files"), local);
    if (!dir.isEmpty()) m_path->setCurrentText(tgui::mapLocalToRemote(dir, mappings));
}
