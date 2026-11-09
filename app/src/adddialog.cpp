#include "adddialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "tgui/util.h"

AddTorrentDialog::AddTorrentDialog(const QString& initialFile, const QString& initialDir,
                                   int rpcVersion, bool defaultStart, tgui::RpcClient* rpc,
                                   QWidget* parent)
    : QDialog(parent)
    , m_rpc(rpc)
{
    setWindowTitle(tr("Add torrent"));

    m_rbFile = new QRadioButton(tr(".torrent file:"), this);
    m_rbUrl = new QRadioButton(tr("Magnet link or URL:"), this);
    m_rbFile->setChecked(true);

    m_file = new QLineEdit(initialFile, this);
    m_file->setPlaceholderText(tr("Path to a .torrent file"));
    auto* btnFile = new QPushButton(tr("Browse…"), this);
    connect(btnFile, &QPushButton::clicked, this, &AddTorrentDialog::onBrowseFile);
    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(btnFile);

    m_url = new QLineEdit(QString(), this);
    m_url->setPlaceholderText(tr("magnet:?xt=urn:btih:… or http://…"));

    m_dir = new QLineEdit(initialDir, this);
    auto* btnDir = new QPushButton(tr("Browse…"), this);
    connect(btnDir, &QPushButton::clicked, this, &AddTorrentDialog::onBrowseDir);
    auto* dirRow = new QHBoxLayout();
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(btnDir);

    m_freeSpace = new QLabel(this);
    m_freeSpace->setContentsMargins(12, 0, 0, 0);
    connect(m_dir, &QLineEdit::textEdited, this, &AddTorrentDialog::updateFreeSpace);
    if (m_rpc)
        connect(m_rpc, &tgui::RpcClient::freeSpaceReceived, this,
                [this](const QString& path, qint64 bytes) {
                    if (path == m_dir->text().trimmed())
                        m_freeSpace->setText(tr("Free: %1")
                                                 .arg(QString::fromStdString(tgui::formatSize(
                                                     static_cast<double>(bytes)))));
                });

    m_peerLimit = new QSpinBox(this);
    m_peerLimit->setRange(0, 10000);
    m_peerLimit->setValue(0);
    m_peerLimit->setSpecialValueText(tr("Unlimited"));

    m_save = new QCheckBox(tr("Save .torrent file"), this);
    m_savePath = new QLineEdit(QString(), this);
    m_savePath->setPlaceholderText(tr("Destination folder/file name"));
    m_savePath->setEnabled(false);
    connect(m_save, &QCheckBox::toggled, m_savePath, &QLineEdit::setEnabled);

    m_start = new QCheckBox(tr("Start"), this);
    m_start->setChecked(defaultStart);

    m_sequential = new QCheckBox(tr("Sequential download"), this);
    if (rpcVersion < 18) {
        m_sequential->setEnabled(false);
        m_sequential->setToolTip(tr("Requires Transmission 4.1 or newer (RPC 18)"));
    }

    auto* form = new QFormLayout();
    form->addRow(QString(), m_rbFile);
    form->addRow(QString(), fileRow);
    form->addRow(QString(), m_rbUrl);
    form->addRow(QString(), m_url);
    form->addRow(tr("Download to"), dirRow);
    form->addRow(QString(), m_freeSpace);
    form->addRow(tr("Peer limit"), m_peerLimit);
    form->addRow(QString(), m_save);
    form->addRow(QString(), m_savePath);
    form->addRow(QString(), m_start);
    form->addRow(QString(), m_sequential);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_rbFile->isChecked()) {
            const QString f = m_file->text().trimmed();
            if (f.isEmpty() || !QFile::exists(f)) {
                QMessageBox::warning(this, tr("Add torrent"),
                                     tr("Specify an existing .torrent file."));
                return;
            }
        } else if (m_url->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Add torrent"), tr("Specify a magnet link or URL."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);

    m_file->setFocus();
    updateFreeSpace();
}

tgui::RpcClient::AddTorrent AddTorrentDialog::params() const
{
    tgui::RpcClient::AddTorrent p;
    if (m_rbFile->isChecked() && !m_file->text().trimmed().isEmpty()) {
        p.source = tgui::RpcClient::AddTorrent::Source::File;
        p.file = m_file->text().trimmed();
    } else {
        p.source = tgui::RpcClient::AddTorrent::Source::Url;
        p.url = m_url->text().trimmed();
    }
    p.downloadDir = m_dir->text().trimmed();
    p.peerLimit = m_peerLimit->value();
    p.start = m_start->isChecked();
    p.sequential = m_sequential->isChecked();
    return p;
}

bool AddTorrentDialog::saveEnabled() const
{
    return m_save->isChecked() && !m_savePath->text().trimmed().isEmpty();
}

QString AddTorrentDialog::savePath() const
{
    return m_savePath->text().trimmed();
}

void AddTorrentDialog::onBrowseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select a .torrent file"), QString(),
        tr("Torrent files (*.torrent);;All files (*)"));
    if (!path.isEmpty())
        m_file->setText(path);
}

void AddTorrentDialog::onBrowseDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Download to"),
                                                           m_dir->text());
    if (!dir.isEmpty())
        m_dir->setText(dir);
}

void AddTorrentDialog::updateFreeSpace()
{
    m_freeSpace->clear();
    if (!m_rpc || m_rpc->rpcVersion() < 15)
        return;
    if (!m_dir->text().trimmed().isEmpty())
        m_rpc->getFreeSpace(m_dir->text().trimmed());
}
