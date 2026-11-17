#include "fileselectdialog.h"
#include "optionsdialog.h"
#include "theme.h"
#include "torrentlabels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
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
constexpr int kFreeSpaceDebounceMs = 250; // отложенный запрос free-space при смене пути

/** Кэширует совпадения простых масок расширений, остальные проверяет по имени. */
class DestinationPatterns {
public:
    explicit DestinationPatterns(const QString& text)
    {
        const auto masks = text.split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts);
        for (const auto& mask : masks) {
            const QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(mask),
                                             QRegularExpression::CaseInsensitiveOption);
            const bool suffixOnly = mask.startsWith("*.") &&
                !mask.sliced(2).contains(QRegularExpression(R"([.*?\[\]\\/])"));
            (suffixOnly ? m_suffixPatterns : m_namePatterns).append(pattern);
        }
    }

    bool isEmpty() const { return m_suffixPatterns.isEmpty() && m_namePatterns.isEmpty(); }

    bool matches(const QString& name)
    {
        if (!m_suffixPatterns.isEmpty()) {
            const auto dot = name.lastIndexOf('.');
            if (dot >= 0) {
                const auto suffix = name.sliced(dot);
                auto match = m_suffixMatches.constFind(suffix);
                if (match == m_suffixMatches.cend())
                    match = m_suffixMatches.insert(suffix, anyMatch(m_suffixPatterns, suffix));
                if (*match)
                    return true;
            }
        }
        return anyMatch(m_namePatterns, name);
    }

private:
    static bool anyMatch(const QList<QRegularExpression>& patterns, const QString& name)
    {
        return std::ranges::any_of(patterns, [&name](const auto& pattern) {
            return pattern.match(name).hasMatch();
        });
    }

    QList<QRegularExpression> m_suffixPatterns;
    QList<QRegularExpression> m_namePatterns;
    QHash<QString, bool> m_suffixMatches;
};

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
                                   bool useDestinationTemplates, const QString& profile)
    : QDialog(parent), m_preferences(profile), m_rpc(rpc)
{
    setWindowTitle(tr("Add new torrent"));
    resize(660, 510);

    auto* destination = new QWidget(this);
    auto* form = tgui::theme::formGrid(destination);
    form->setContentsMargins(0, 0, 0, 0);
    auto* pathRow = new QHBoxLayout;
    m_destination = new QComboBox(this);
    m_destination->setObjectName(QStringLiteral("destinationFolder"));
    m_destination->setEditable(true);
    m_destination->setInsertPolicy(QComboBox::NoInsert);
    m_history = m_preferences.destinations(rpc ? &rpc->options() : nullptr);
    m_destination->addItems(m_history.keys());
    if (!details.downloadDir.isEmpty() && m_destination->findText(details.downloadDir) < 0)
        m_destination->addItem(details.downloadDir);
    auto* pathEdit = m_destination->lineEdit();
    pathEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(pathEdit, &QLineEdit::customContextMenuRequested, this, [this, pathEdit](const QPoint& point) {
        auto* menu = pathEdit->createStandardContextMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        const auto path = m_destination->currentText().trimmed();
        menu->addSeparator();
        auto* remove = menu->addAction(tr("Remove folder from history"));
        remove->setEnabled(m_history.contains(path));
        connect(remove, &QAction::triggered, this, [this, path] {
            if (!m_history.remove(path))
                return;
            const int index = m_destination->findText(path);
            if (index >= 0)
                m_destination->removeItem(index);
            m_preferences.setDestinations(m_rpc ? &m_rpc->options() : nullptr, m_history);
        });
        menu->popup(pathEdit->mapToGlobal(point));
    });
    m_browse = new QPushButton(tr("Browse…"), this);
    connect(m_browse, &QPushButton::clicked, this, &FileSelectDialog::browseDestination);
    pathRow->addWidget(m_destination, 1);
    pathRow->addWidget(m_browse);
    tgui::theme::addFormRow(form, tr("Destination folder:"), pathRow);

    // Как в макете: свободное место — вторая строка без подписи.
    auto* freeSpace = new QLabel(this);
    freeSpace->setObjectName(QStringLiteral("hintLabel"));
    tgui::theme::addFormRow(form, QString(), freeSpace);

    m_template = new QLineEdit(this);
    m_template->setObjectName(QStringLiteral("destinationTemplate"));
    m_template->setPlaceholderText(tr("For example: *.mkv *.mp4 — select matching files"));

    m_saveAs = new QLineEdit(details.name, this);
    m_saveAs->setObjectName(QStringLiteral("saveAs"));
    m_saveAs->setEnabled(rpcVersion >= tgui::kRpcFreeSpace && !details.files.isEmpty());
    tgui::theme::addFormRow(form, tr("Save as:"), m_saveAs);

    // Порядок строк как в макете: «Сохранить как», «Метка», «Лимит пиров».
    const auto labels = torrentlabels::configured();
    if (rpcVersion >= tgui::kRpcLabels && !labels.isEmpty()) {
        m_label = torrentlabels::createPicker(destination, labels);
        tgui::theme::addFormRow(form, tr("Label"), m_label);
    }
    m_peerLimit = new QSpinBox(this);
    m_peerLimit->setObjectName(QStringLiteral("peerLimit"));
    m_peerLimit->setRange(1, 65535);
    m_peerLimit->setValue(details.peerLimit);
    tgui::theme::addFormRow(form, tr("Peer limit:"), m_peerLimit);

    QString initialPath = details.downloadDir;
    double bestSize = 0.0;
    QList<QString> names;
    if (useDestinationTemplates && !m_history.isEmpty()) {
        names.reserve(details.files.size());
        for (const auto& file : details.files)
            names.append(QFileInfo(file.name).fileName());
    }
    for (auto it = m_history.cbegin(); useDestinationTemplates && it != m_history.cend(); ++it) {
        DestinationPatterns patterns(it.value().toString());
        if (patterns.isEmpty())
            continue;
        double matchedSize = 0.0;
        for (qsizetype row = 0; row < details.files.size(); ++row)
            if (patterns.matches(names.at(row)))
                matchedSize += details.files.at(row).size;
        if (matchedSize > bestSize) {
            bestSize = matchedSize;
            initialPath = it.key();
        }
    }
    if (rpcVersion < tgui::kRpcSetLocation) {
        initialPath = details.downloadDir;
        m_destination->setEnabled(false);
        m_browse->setEnabled(false);
        m_template->setEnabled(false);
    }
    m_destination->setCurrentText(initialPath);
    m_template->setText(m_history.value(initialPath).toString());
    connect(m_destination, &QComboBox::currentTextChanged, this, [this](const QString& path) {
        m_template->setText(m_history.value(path.trimmed()).toString());
    });

    freeSpace->setText(tr("Free disk space: %1").arg(QStringLiteral("—")));
    if (rpc && rpcVersion >= tgui::kRpcFreeSpace) {
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(kFreeSpaceDebounceMs);
        connect(timer, &QTimer::timeout, this, [this] {
            if (!m_rpc)
                return;
            const auto path = m_destination->currentText().trimmed();
            if (!path.isEmpty())
                m_rpc->getFreeSpace(path);
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
    m_tree->setMinimumHeight(190);
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
    QList<QTreeWidgetItem*> topLevelItems;
    QHash<QString, QTreeWidgetItem*> nodes;
    QHash<QString, double> folderSizes;
    const QString root = details.name + '/';
    const bool sharedRoot = !details.name.isEmpty() &&
        std::all_of(details.files.cbegin(), details.files.cend(), [&root](const auto& file) {
            return file.name.startsWith(root);
        });
    for (const auto& file : details.files) {
        QString path;
        QTreeWidgetItem* parentItem = nullptr;
        const auto parts = file.name.split('/', Qt::SkipEmptyParts);
        for (int i = 0; i < parts.size(); ++i) {
            if (!path.isEmpty())
                path += '/';
            path += parts.at(i);
            QTreeWidgetItem* item = nodes.value(path);
            if (!item) {
                item = new QTreeWidgetItem;
                if (parentItem)
                    parentItem->addChild(item);
                else
                    topLevelItems.append(item);
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
            parentItem->setIcon(0, tgui::theme::fileIcon(parentItem->text(0)));
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
        item->setIcon(0, tgui::theme::icon(tgui::theme::Icon::Folder));
        item->setText(1, QString::fromStdString(tgui::formatSize(it.value())));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    m_tree->addTopLevelItems(topLevelItems);
    m_tree->collapseAll();
    if (sharedRoot && m_tree->topLevelItemCount() == 1)
        m_tree->topLevelItem(0)->setExpanded(true);
    connect(m_template, &QLineEdit::textEdited, this, &FileSelectDialog::applyTemplate);

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
    if (rpcVersion < tgui::kRpcSequential) {
        m_sequential->setChecked(false);
        m_sequential->setEnabled(false);
        m_sequential->setToolTip(tr("Requires Transmission 4.1 or newer (RPC 18)"));
    }

    // Как в макете: «Не показывать это окно» дублирует настройку
    // «Пропускать окно добавления» и включается только при отметке.
    m_skip = new QCheckBox(tr("Do not show this window"), this);
    m_skip->setObjectName(QStringLiteral("dontShowAddDialog"));
    m_skip->setChecked(QSettings().value(OptionsDialog::skipAddDialogKey, false).toBool());
    m_skip->setToolTip(tr("Same as \"Show the add torrent dialog\" in the application options"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &FileSelectDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_footer = new QHBoxLayout;
    m_footer->addStretch(1);
    m_footer->addWidget(buttons);

    auto* optionsRow = new QHBoxLayout;
    optionsRow->addWidget(m_start);
    optionsRow->addSpacing(12);
    optionsRow->addWidget(m_sequential);
    optionsRow->addSpacing(12);
    optionsRow->addWidget(m_skip);
    optionsRow->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    m_body = layout;
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(7);
    layout->addWidget(destination);
    if (!details.files.isEmpty()) {
        auto* contents = new QGroupBox(tr("Torrent contents"), this);
        contents->setObjectName(QStringLiteral("addTorrentGroup"));
        m_contents = contents;
        auto* contentsLayout = new QVBoxLayout(contents);
        // Шаблон — внутри группы «Содержимое торрента», над деревом файлов.
        auto* templateRow = new QHBoxLayout();
        templateRow->addWidget(new QLabel(tr("Template:"), contents));
        templateRow->addWidget(m_template, 1);
        contentsLayout->addLayout(templateRow);
        contentsLayout->addWidget(m_tree, 1);
        contentsLayout->addLayout(selectRow);
        layout->addWidget(contents, 1);
    } else {
        m_tree->hide();
        btnAll->hide();
        btnNone->hide();
        m_summary->hide();
        delete selectRow;
        layout->setSizeConstraint(QLayout::SetFixedSize);
    }
    layout->addLayout(optionsRow);
    layout->addLayout(m_footer);
    updateSummary();
    tgui::theme::decorateDialog(this);
}

/** Позволяет сохранить метаданные локального .torrent, включая восстановленную копию. */
void FileSelectDialog::offerMetainfoCopy(const QByteArray& metainfo)
{
    if (metainfo.isEmpty())
        return;
    auto* button = new QPushButton(tr("Save .torrent file…"), this);
    m_footer->insertWidget(0, button);
    connect(button, &QPushButton::clicked, this, [this, metainfo] {
        const QPointer<FileSelectDialog> dialog(this);
        const QString destination = QFileDialog::getSaveFileName(
            this, tr("Save .torrent file…"), {}, tr("Torrent files (*.torrent)"));
        if (!dialog || destination.isEmpty())
            return;
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly) || output.write(metainfo) != metainfo.size() ||
            !output.commit())
            QMessageBox::warning(this, windowTitle(), tr("Unable to save file: %1").arg(destination));
    });
}

/** Добавляет выбор старой раздачи до отправки метаданных демону. */
void FileSelectDialog::offerReplacement(const QVector<tgui::Torrent>& torrents, int selectedId)
{
    if (!m_contents)
        return;

    auto* candidates = new QComboBox(this);
    candidates->setObjectName(QStringLiteral("replacementTorrent"));
    candidates->setEditable(true);
    candidates->setInsertPolicy(QComboBox::NoInsert);
    candidates->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    candidates->setMaxVisibleItems(6);
    int selected = -1;
    for (const auto& torrent : torrents) {
        if (!torrent.hashString || torrent.hashString->isEmpty() || !torrent.downloadDir ||
            torrent.downloadDir->isEmpty())
            continue;
        const auto name = torrent.name.value_or(*torrent.hashString);
        const auto label = QStringLiteral("%1 — %2").arg(name, *torrent.downloadDir);
        candidates->addItem(label, *torrent.hashString);
        candidates->setItemData(candidates->count() - 1, label, Qt::ToolTipRole);
        if (torrent.id == selectedId)
            selected = candidates->count() - 1;
    }
    if (candidates->count() == 0) {
        delete candidates;
        return;
    }

    m_replace = new QCheckBox(tr("Replace an existing torrent"), this);
    m_replace->setObjectName(QStringLiteral("replaceExistingTorrent"));
    m_replacement = candidates;
    candidates->setAccessibleName(m_replace->text());
    candidates->setCurrentIndex(selected);
    candidates->setEnabled(false);
    auto* search = candidates->lineEdit();
    search->setPlaceholderText(tr("Find by torrent name or folder"));
    auto* completer = candidates->completer();
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setMaxVisibleItems(candidates->maxVisibleItems());
    m_body->insertWidget(1, m_replace);
    m_body->insertWidget(2, candidates);

    const auto updateTarget = [this] {
        m_ok->setEnabled(!m_replace->isChecked() || !replacementHash().isEmpty());
    };
    connect(candidates, &QComboBox::currentIndexChanged, this, updateTarget);
    connect(candidates, &QComboBox::editTextChanged, this, updateTarget);
    const bool canChangeFolder = m_destination->isEnabled();
    const bool canRename = m_saveAs->isEnabled();
    connect(m_replace, &QCheckBox::toggled, this,
            [this, candidates, search, updateTarget, canChangeFolder, canRename](bool checked) {
        candidates->setEnabled(checked);
        m_destination->setEnabled(!checked && canChangeFolder);
        m_browse->setEnabled(!checked && canChangeFolder);
        m_saveAs->setEnabled(!checked && canRename);
        if (checked) {
            search->setFocus();
            search->selectAll();
        }
        updateTarget();
    });
}

QString FileSelectDialog::replacementHash() const
{
    if (!m_replace || !m_replace->isChecked())
        return {};
    const int index = m_replacement->currentIndex();
    return index >= 0 && m_replacement->currentText() == m_replacement->itemText(index)
        ? m_replacement->currentData().toString() : QString();
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
    if (m_replace && m_replace->isChecked()) {
        if (replacementHash().isEmpty())
            return;
        if (m_skip->isChecked())
            QSettings().setValue(OptionsDialog::skipAddDialogKey, true);
        QDialog::accept();
        return;
    }
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
        m_preferences.setDestinations(m_rpc ? &m_rpc->options() : nullptr, m_history);
    }
    if (m_skip->isChecked())
        QSettings().setValue(OptionsDialog::skipAddDialogKey, true);
    QDialog::accept();
}

void FileSelectDialog::browseDestination()
{
    const auto mappings = m_preferences.mappings();
    QString local = tgui::mapRemoteToLocal(options().downloadDir, mappings).value_or(QString{});
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
    if (!dialog || selected.isEmpty())
        return;
    const auto mapped = tgui::mapLocalToRemote(selected, mappings);
    const bool localDaemon = m_rpc && m_rpc->options().proxy.type() == QNetworkProxy::NoProxy &&
        tgui::isLocalDaemon(m_rpc->options().url);
    if (!mapped && !localDaemon) {
        QMessageBox::warning(this, windowTitle(),
            tr("This folder has no path mapping. Enter the daemon path or configure a mapping."));
        return;
    }
    m_destination->setCurrentText(mapped.value_or(selected));
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

/** Отмечает подходящие файлы и пересчитывает папки одним обходом дерева. */
void FileSelectDialog::applyTemplate(const QString& text)
{
    DestinationPatterns patterns(text);
    if (patterns.isEmpty())
        return;
    const auto apply = [&patterns](auto&& self, QTreeWidgetItem* item) -> Qt::CheckState {
        const auto count = item->childCount();
        if (count == 0) {
            const bool match = patterns.matches(item->text(0));
            const auto state = match ? Qt::Checked : Qt::Unchecked;
            item->setCheckState(0, state);
            return state;
        }
        auto state = self(self, item->child(0));
        for (int i = 1; i < count; ++i)
            if (self(self, item->child(i)) != state)
                state = Qt::PartiallyChecked;
        item->setCheckState(0, state);
        return state;
    };
    m_updating = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        apply(apply, m_tree->topLevelItem(i));
    m_updating = false;
    updateSummary();
}

void FileSelectDialog::updateSummary()
{
    double size = 0;
    int selected = 0, total = 0;
    forEachFile(m_tree->invisibleRootItem(), [&](QTreeWidgetItem* item) {
        ++total;
        if (item->checkState(0) == Qt::Checked) {
            ++selected;
            size += item->data(0, kFileSizeRole).toDouble();
        }
    });
    m_summary->setText(tr("Selected: %1 of %2, %3").arg(selected).arg(total)
        .arg(QString::fromStdString(tgui::formatSize(size))));
}
