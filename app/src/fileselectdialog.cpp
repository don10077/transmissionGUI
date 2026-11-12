#include "fileselectdialog.h"
#include "torrentlabels.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "tgui/util.h"
#include "pathmap.h"
#include "sequentialpolicy.h"

namespace {

// Размер файла в UserRole+1 нужен для подсчёта выбранного объёма.
constexpr int kFileIndexRole = Qt::UserRole;
constexpr int kFileSizeRole = Qt::UserRole + 1;

/** Compile the filename wildcards saved for a destination folder. */
QList<QRegularExpression> destinationPatterns(const QString& text)
{
    QList<QRegularExpression> patterns;
    const auto masks = text.split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
    for (const auto& mask : masks) {
        patterns.append(QRegularExpression(QRegularExpression::wildcardToRegularExpression(mask),
                                           QRegularExpression::CaseInsensitiveOption));
    }
    return patterns;
}

// Обойти листья дерева (у них есть индекс файла).
void forEachFile(QTreeWidgetItem* item, const std::function<void(QTreeWidgetItem*)>& fn)
{
    const QVariant index = item->data(0, kFileIndexRole);
    if (item->childCount() == 0 && index.isValid())
        fn(item);
    for (int i = 0; i < item->childCount(); ++i)
        forEachFile(item->child(i), fn);
}

} // namespace

FileSelectDialog::FileSelectDialog(const tgui::TorrentDetails& details, bool start,
                                   int rpcVersion, tgui::RpcClient* rpc, QWidget* parent,
                                   bool useDestinationTemplates)
    : QDialog(parent)
{
    setWindowTitle(tr("Add new torrent"));
    resize(620, 460);

    auto* destination = new QGroupBox(this);
    auto* form = new QFormLayout(destination);
    auto* pathRow = new QHBoxLayout;
    m_destination = new QComboBox(this);
    m_destination->setObjectName(QStringLiteral("destinationFolder"));
    m_destination->setEditable(true);
    m_destination->setInsertPolicy(QComboBox::NoInsert);
    m_historyKey = QStringLiteral("downloads/destinations/");
    if (rpc)
        m_historyKey += QString::fromLatin1(QUrl::toPercentEncoding(
            rpc->options().url.toString(QUrl::RemoveUserInfo) + rpc->options().rpcPath));
    m_history = QSettings().value(m_historyKey).toMap();
    m_destination->addItems(m_history.keys());
    if (!details.downloadDir.isEmpty() && m_destination->findText(details.downloadDir) < 0)
        m_destination->addItem(details.downloadDir);
    auto* remove = new QPushButton(tr("Remove"), this);
    connect(remove, &QPushButton::clicked, this, [this] {
        const auto path = m_destination->currentText().trimmed();
        if (path.isEmpty() || !m_history.remove(path))
            return;
        const int index = m_destination->findText(path);
        if (index >= 0)
            m_destination->removeItem(index);
        QSettings().setValue(m_historyKey, m_history);
    });
    auto* browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &FileSelectDialog::browseDestination);
    pathRow->addWidget(m_destination, 1);
    pathRow->addWidget(remove);
    pathRow->addWidget(browse);
    form->addRow(tr("Destination folder:"), pathRow);

    m_saveAs = new QLineEdit(details.name, this);
    m_saveAs->setObjectName(QStringLiteral("saveAs"));
    m_saveAs->setEnabled(rpcVersion >= 15 && !details.files.isEmpty());
    form->addRow(tr("Save as:"), m_saveAs);

    m_template = new QLineEdit(this);
    m_template->setObjectName(QStringLiteral("destinationTemplate"));
    m_template->setPlaceholderText(tr("For example: *.mkv *.mp4"));
    form->insertRow(1, tr("Template:"), m_template);

    m_peerLimit = new QSpinBox(this);
    m_peerLimit->setObjectName(QStringLiteral("peerLimit"));
    m_peerLimit->setRange(1, 65535);
    m_peerLimit->setValue(details.peerLimit);
    form->addRow(tr("Peer limit:"), m_peerLimit);
    const auto labels = torrentlabels::configured();
    if (rpcVersion >= 16 && !labels.isEmpty()) {
        m_label = torrentlabels::createPicker(destination, labels);
        form->addRow(tr("Label"), m_label);
    }

    QString initialPath = details.downloadDir;
    double bestSize = 0.0;
    for (auto it = m_history.cbegin(); useDestinationTemplates && it != m_history.cend(); ++it) {
        const auto patterns = destinationPatterns(it.value().toString());
        double matchedSize = 0.0;
        for (const auto& file : details.files) {
            const auto name = QFileInfo(file.name).fileName();
            if (std::any_of(patterns.cbegin(), patterns.cend(), [&name](const auto& pattern) {
                    return pattern.match(name).hasMatch();
                }))
                matchedSize += file.size;
        }
        if (matchedSize > bestSize) {
            bestSize = matchedSize;
            initialPath = it.key();
        }
    }
    if (rpcVersion < 6) {
        initialPath = details.downloadDir;
        m_destination->setEnabled(false);
        remove->setEnabled(false);
        browse->setEnabled(false);
        m_template->setEnabled(false);
    }
    m_destination->setCurrentText(initialPath);
    m_template->setText(m_history.value(initialPath).toString());
    connect(m_destination, &QComboBox::currentTextChanged, this, [this](const QString& path) {
        m_template->setText(m_history.value(path.trimmed()).toString());
    });

    auto* freeSpace = new QLabel(this);
    freeSpace->setText(tr("Free disk space: %1").arg(QStringLiteral("—")));
    if (rpc && rpcVersion >= 15) {
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(250);
        connect(timer, &QTimer::timeout, this, [this, rpc] {
            const auto path = m_destination->currentText().trimmed();
            if (!path.isEmpty())
                rpc->getFreeSpace(path);
        });
        connect(rpc, &QObject::destroyed, timer, &QTimer::stop);
        connect(rpc, &tgui::RpcClient::freeSpaceReceived, this,
                [this, freeSpace](const QString& path, qint64 bytes) {
            if (path == m_destination->currentText().trimmed()) {
                freeSpace->setText(tr("Free disk space: %1").arg(bytes >= 0
                    ? QString::fromStdString(tgui::formatSize(static_cast<double>(bytes)))
                    : QStringLiteral("—")));
            }
        });
        connect(m_destination, &QComboBox::currentTextChanged, timer, [this, timer, freeSpace] {
            freeSpace->setText(tr("Free disk space: %1").arg(QStringLiteral("—")));
            timer->start();
        });
        timer->start();
    }

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("filesTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("File name"), tr("Size")});
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    // Дерево папок/файлов: полная иерархия, как в списке файлов.
    QHash<QString, QTreeWidgetItem*> nodes;
    QHash<QString, double> folderSizes;
    const QString root = details.name + '/';
    const bool sharedRoot = !details.name.isEmpty() &&
        std::all_of(details.files.cbegin(), details.files.cend(), [&root](const auto& file) {
            return file.name.startsWith(root);
        });
    for (const auto& file : details.files) {
        m_totalSize += file.size;
        QString path;
        QTreeWidgetItem* parentItem = nullptr;
        const auto relative = sharedRoot ? file.name.mid(root.size()) : file.name;
        const auto parts = relative.split('/', Qt::SkipEmptyParts);
        for (int i = 0; i < parts.size(); ++i) {
            if (!path.isEmpty())
                path += '/';
            path += parts.at(i);
            QTreeWidgetItem* item = nodes.value(path);
            if (!item) {
                item = parentItem ? new QTreeWidgetItem(parentItem)
                                  : new QTreeWidgetItem(m_tree);
                item->setText(0, parts.at(i));
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, Qt::Checked);
                nodes.insert(path, item);
            }
            if (i + 1 < parts.size())
                folderSizes[path] += file.size;
            parentItem = item;
        }
        if (parentItem) {
            parentItem->setIcon(0,
                                QApplication::style()->standardIcon(QStyle::SP_FileIcon));
            parentItem->setData(0, kFileIndexRole, file.index);
            parentItem->setData(0, kFileSizeRole, file.size);
            parentItem->setText(1, QString::fromStdString(tgui::formatSize(file.size)));
            parentItem->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    // Папки: значок каталога и суммарный размер.
    for (auto it = folderSizes.cbegin(); it != folderSizes.cend(); ++it) {
        QTreeWidgetItem* item = nodes.value(it.key());
        if (!item)
            continue;
        item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        item->setText(1, QString::fromStdString(tgui::formatSize(it.value())));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    m_tree->collapseAll();

    connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
                if (m_updating || column != 0)
                    return;
                m_updating = true;
                const Qt::CheckState state = item->checkState(0);
                // Отметка папки каскадом переходит на все вложенные файлы.
                if (item->childCount() > 0 && state != Qt::PartiallyChecked)
                    setChildrenChecked(item, state);
                // Состояние папок пересчитывается снизу вверх.
                updateAncestors(item);
                m_updating = false;
                updateSummary();
            });

    auto* btnAll = new QPushButton(tr("Select all"), this);
    auto* btnNone = new QPushButton(tr("Select none"), this);
    connect(btnAll, &QPushButton::clicked, this, [this] { setAll(Qt::Checked); });
    connect(btnNone, &QPushButton::clicked, this, [this] { setAll(Qt::Unchecked); });

    m_summary = new QLabel(this);
    m_summary->setObjectName(QStringLiteral("selectionSummary"));
    auto* selectRow = new QHBoxLayout();
    selectRow->addWidget(btnAll);
    selectRow->addWidget(btnNone);
    selectRow->addStretch(1);
    selectRow->addWidget(m_summary);

    m_start = new QCheckBox(tr("Start torrent"), this);
    m_start->setObjectName(QStringLiteral("startTorrent"));
    m_start->setChecked(start);

    m_sequential = new QCheckBox(tr("Sequential download"), this);
    m_sequential->setObjectName(QStringLiteral("sequentialDownload"));
    m_sequential->setChecked(tgui::automaticSequential(details.files, rpcVersion));
    if (rpcVersion < 18) {
        m_sequential->setChecked(false);
        m_sequential->setEnabled(false);
        m_sequential->setToolTip(tr("Requires Transmission 4.1 or newer (RPC 18)"));
    }

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &FileSelectDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* optionsRow = new QHBoxLayout();
    optionsRow->addWidget(m_start);
    optionsRow->addSpacing(12);
    optionsRow->addWidget(m_sequential);
    optionsRow->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(destination);
    if (!details.files.isEmpty()) {
        auto* contents = new QGroupBox(tr("Torrent contents"), this);
        auto* contentsLayout = new QVBoxLayout(contents);
        contentsLayout->addWidget(m_tree, 1);
        contentsLayout->addWidget(freeSpace);
        contentsLayout->addLayout(selectRow);
        layout->addWidget(contents, 1);
    } else {
        m_tree->hide();
        btnAll->hide();
        btnNone->hide();
        m_summary->hide();
        delete selectRow;
        layout->addWidget(freeSpace);
        layout->setSizeConstraint(QLayout::SetFixedSize);
    }
    layout->addLayout(optionsRow);
    layout->addWidget(m_buttons);
    updateSummary();
}

/** Offer a local copy of the metainfo for file and HTTP(S) sources. */
void FileSelectDialog::offerMetainfoCopy(const tgui::RpcClient::AddTorrent& source)
{
    const QUrl url(source.url);
    const bool local = source.source == tgui::RpcClient::AddTorrent::Source::File;
    if (!local && url.scheme() != "http" && url.scheme() != "https")
        return;

    auto* button = m_buttons->addButton(tr("Save .torrent file…"), QDialogButtonBox::ActionRole);
    connect(button, &QPushButton::clicked, this, [this, source, local, url] {
        const QPointer<FileSelectDialog> dialog(this);
        const QString destination = QFileDialog::getSaveFileName(
            this, tr("Save .torrent file…"), {}, tr("Torrent files (*.torrent)"));
        if (!dialog || destination.isEmpty())
            return;
        const auto save = [this, destination](QIODevice& input) {
            QSaveFile output(destination);
            const auto payload = input.readAll();
            if (!output.open(QIODevice::WriteOnly) || output.write(payload) != payload.size() ||
                !output.commit())
                QMessageBox::warning(this, windowTitle(), tr("Unable to save file: %1").arg(destination));
        };
        if (local) {
            QFile file(source.file);
            if (file.open(QIODevice::ReadOnly))
                save(file);
            else
                QMessageBox::warning(this, windowTitle(), file.errorString());
            return;
        }
        auto* network = new QNetworkAccessManager(this);
        auto* reply = network->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, network, save] {
            if (reply->error() == QNetworkReply::NoError)
                save(*reply);
            else
                QMessageBox::warning(this, windowTitle(), reply->errorString());
            network->deleteLater();
        });
    });
}

QVector<int> FileSelectDialog::wanted() const
{
    QVector<int> result;
    forEachFile(m_tree->invisibleRootItem(), [&result](QTreeWidgetItem* item) {
        if (item->checkState(0) == Qt::Checked)
            result.push_back(item->data(0, kFileIndexRole).toInt());
    });
    return result;
}

QVector<int> FileSelectDialog::unwanted() const
{
    QVector<int> result;
    forEachFile(m_tree->invisibleRootItem(), [&result](QTreeWidgetItem* item) {
        if (item->checkState(0) != Qt::Checked)
            result.push_back(item->data(0, kFileIndexRole).toInt());
    });
    return result;
}

bool FileSelectDialog::startTorrent() const
{
    return m_start->isChecked();
}

bool FileSelectDialog::sequentialDownload() const
{
    return m_sequential->isChecked();
}

tgui::RpcClient::AddTorrent FileSelectDialog::options() const
{
    tgui::RpcClient::AddTorrent options;
    options.downloadDir = m_destination->currentText().trimmed();
    options.peerLimit = m_peerLimit->value();
    options.start = startTorrent();
    options.sequential = sequentialDownload();
    if (m_label)
        options.labels = m_label->currentData().toStringList();
    return options;
}

QString FileSelectDialog::saveAs() const
{
    return m_saveAs->isEnabled() ? m_saveAs->text().trimmed() : QString();
}

void FileSelectDialog::accept()
{
    const auto path = options().downloadDir;
    if (m_destination->isEnabled() && path.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Specify a destination folder."));
        return;
    }
    const auto name = saveAs();
    if (m_saveAs->isEnabled() && (name.isEmpty() || name == "." || name == ".." ||
                                  name.contains('/') || name.contains('\\'))) {
        QMessageBox::warning(this, windowTitle(), tr("Specify a valid file or folder name."));
        return;
    }
    if (!path.isEmpty()) {
        m_history.insert(path, m_template->text().trimmed());
        QSettings().setValue(m_historyKey, m_history);
    }
    QDialog::accept();
}

void FileSelectDialog::browseDestination()
{
    const auto mappings = QSettings().value("paths/mappings").toStringList();
    QString local = tgui::mapRemoteToLocal(options().downloadDir, mappings);
    if (!QFileInfo(local).isDir()) {
        for (const auto& mapping : mappings) {
            const int separator = mapping.indexOf('=');
            const QString candidate = mapping.mid(separator + 1).trimmed();
            if (separator > 0 && QFileInfo(candidate).isDir()) {
                local = candidate;
                break;
            }
        }
    }
    if (!QFileInfo(local).isDir()) {
        QMessageBox::warning(this, windowTitle(),
            tr("The destination is not accessible locally. Check path mappings or enter the daemon path."));
        return;
    }
    const QPointer<FileSelectDialog> dialog(this);
    const auto selected = QFileDialog::getExistingDirectory(this, tr("Destination folder:"),
        QDir::fromNativeSeparators(local));
    if (dialog && !selected.isEmpty())
        m_destination->setCurrentText(tgui::mapLocalToRemote(selected, mappings));
}

void FileSelectDialog::setAll(Qt::CheckState state)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        m_tree->topLevelItem(i)->setCheckState(0, state);
}

void FileSelectDialog::setChildrenChecked(QTreeWidgetItem* item, Qt::CheckState state)
{
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        child->setCheckState(0, state);
        setChildrenChecked(child, state);
    }
}

void FileSelectDialog::updateAncestors(QTreeWidgetItem* item)
{
    for (QTreeWidgetItem* parent = item ? item->parent() : nullptr; parent;
         parent = parent->parent()) {
        int checked = 0;
        int partial = 0;
        const int count = parent->childCount();
        for (int i = 0; i < count; ++i) {
            const Qt::CheckState state = parent->child(i)->checkState(0);
            if (state == Qt::Checked)
                ++checked;
            else if (state == Qt::PartiallyChecked)
                ++partial;
        }
        const Qt::CheckState state = checked == count && partial == 0
            ? Qt::Checked
            : (checked == 0 && partial == 0 ? Qt::Unchecked : Qt::PartiallyChecked);
        if (parent->checkState(0) != state)
            parent->setCheckState(0, state);
    }
}

double FileSelectDialog::selectedSize() const
{
    double total = 0.0;
    forEachFile(m_tree->invisibleRootItem(), [&total](QTreeWidgetItem* item) {
        if (item->checkState(0) == Qt::Checked)
            total += item->data(0, kFileSizeRole).toDouble();
    });
    return total;
}

void FileSelectDialog::updateSummary()
{
    const auto selected = selectedSize();
    QString size = QString::fromStdString(tgui::formatSize(selected));
    if (selected != m_totalSize)
        size += QStringLiteral(" / ") + QString::fromStdString(tgui::formatSize(m_totalSize));
    m_summary->setText(tr("Size: %1").arg(size));
}
