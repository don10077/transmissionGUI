#include "optionsdialog.h"

#include "theme.h"
#include "assoc.h"

#include <algorithm>

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include "pathmap.h"
#include "sequentialpolicy.h"
#include "torrentlabels.h"
#include "tgui/rpcclient.h"

namespace {
constexpr char kKeyLanguage[] = "ui/language";
constexpr char kKeyScale[] = "ui/scale";
constexpr char kKeyRefresh[] = "ui/refreshInterval";
constexpr char kKeyCloseToTray[] = "ui/closeToTray";
constexpr char kKeyMinimizeToTray[] = "ui/minimizeToTray";
constexpr char kKeyTrayIcon[] = "ui/trayIcon";
constexpr char kKeyTrayNotify[] = "ui/trayNotify";
constexpr char kKeyToolbarStyle[] = "ui/toolbarButtonStyle";

QLabel* noteLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hintLabel"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QStringList labelNames(const QListWidget& list)
{
    QStringList labels;
    for (int i = 0; i < list.count(); ++i) {
        const auto label = list.item(i)->text().trimmed();
        if (!label.isEmpty() && !labels.contains(label))
            labels.append(label);
    }
    return labels;
}

void addLabelItem(QListWidget* list, const QString& name)
{
    list->addItem(new QListWidgetItem(torrentlabels::labelIcon(name), name));
}
}  // namespace

OptionsDialog::OptionsDialog(QWidget* parent, int rpcVersion, tgui::RpcClient* rpc, const QString& profile)
    : QDialog(parent), m_preferences(profile), m_rpc(rpc), m_rpcVersion(rpcVersion)
{
    // design/mockup.html, DLG.opt: Общие / Интерфейс / Последовательная
    // загрузка / Метки, затем дополнительные вкладки (Пути / Скорость /
    // Добавление торрентов).
    setWindowTitle(tr("Application options"));
    resize(640, 500);

    auto* tabs = new QTabWidget(this);
    m_tabs = tabs;

    // --- Общие -----------------------------------------------------------
    auto* general = new QWidget(tabs);
    auto* generalLayout = new QVBoxLayout(general);
    generalLayout->setContentsMargins(14, 12, 14, 12);
    generalLayout->setSpacing(7);

    auto* addGroup = new QGroupBox(tr("Adding torrents"), general);
    auto* addForm = tgui::theme::formGrid(addGroup);
    m_skipAddDialog = new QCheckBox(tr("Show the add torrent dialog when adding a torrent"), addGroup);
    m_skipAddDialog->setObjectName(QStringLiteral("skipAddDialog"));
    m_skipAddDialog->setChecked(!QSettings().value(skipAddDialogKey, false).toBool());
    tgui::theme::addFullRow(addForm, m_skipAddDialog);
    m_deleteSourceTorrent = new QCheckBox(tr("Delete the .torrent file after successful addition"), addGroup);
    m_deleteSourceTorrent->setObjectName(QStringLiteral("deleteSourceTorrent"));
    m_deleteSourceTorrent->setChecked(QSettings().value(deleteSourceTorrentKey, false).toBool());
    tgui::theme::addFullRow(addForm, m_deleteSourceTorrent);
    m_clipboardLinks = new QCheckBox(tr("Automatically add links from the clipboard"), addGroup);
    m_clipboardLinks->setObjectName(QStringLiteral("clipboardLinks"));
    m_clipboardLinks->setChecked(QSettings().value(clipboardLinksKey, false).toBool());
    tgui::theme::addFullRow(addForm, m_clipboardLinks);
    generalLayout->addWidget(addGroup);

    auto* dataGroup = new QGroupBox(tr("Data display"), general);
    auto* dataForm = tgui::theme::formGrid(dataGroup);
    m_refresh = new QSpinBox(dataGroup);
    m_refresh->setRange(1, 999);
    m_refresh->setSuffix(tr(" sec"));
    m_refresh->setValue(QSettings().value(kKeyRefresh, 2).toInt());
    tgui::theme::addFormRow(dataForm, tr("Refresh interval:"), m_refresh);
    m_backgroundRefresh = new QSpinBox(dataGroup);
    m_backgroundRefresh->setObjectName(QStringLiteral("backgroundRefresh"));
    m_backgroundRefresh->setRange(1, 999);
    m_backgroundRefresh->setSuffix(tr(" sec"));
    m_backgroundRefresh->setValue(QSettings().value("ui/backgroundRefreshInterval", 10).toInt());
    tgui::theme::addFormRow(dataForm, tr("Refresh interval while minimized:"), m_backgroundRefresh);
    m_smoothRates = new QCheckBox(tr("Smooth transfer rates"), dataGroup);
    m_smoothRates->setObjectName(QStringLiteral("smoothRates"));
    m_smoothRates->setChecked(QSettings().value("ui/smoothRates", false).toBool());
    tgui::theme::addFullRow(dataForm, m_smoothRates);
    generalLayout->addWidget(dataGroup);
    generalLayout->addStretch(1);

    // --- Интерфейс -------------------------------------------------------
    auto* interfacePage = new QWidget(tabs);
    auto* interfaceForm = tgui::theme::formGrid(interfacePage);
    interfaceForm->setContentsMargins(14, 12, 14, 12);

    m_language = new QComboBox(interfacePage);
    m_language->addItem(tr("System"), QStringLiteral("system"));
    m_language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("Deutsch"), QStringLiteral("de"));
    m_language->addItem(QStringLiteral("Українська"), QStringLiteral("uk"));
    {
        const QString lang = QSettings().value(kKeyLanguage, QStringLiteral("ru")).toString();
        const int idx = m_language->findData(lang);
        if (idx >= 0)
            m_language->setCurrentIndex(idx);
    }
    tgui::theme::addFormRow(interfaceForm, tr("Language:"), m_language);

    m_scale = new QSpinBox(interfacePage);
    m_scale->setRange(75, 200);
    m_scale->setSingleStep(5);
    m_scale->setSuffix(tr(" %"));
    m_scale->setValue(QSettings().value(kKeyScale, 100).toInt());
    tgui::theme::addFormRow(interfaceForm, tr("Font size:"), m_scale);

    m_toolbarStyle = new QComboBox(interfacePage);
    m_toolbarStyle->addItem(tr("Icons only"), 0);
    m_toolbarStyle->addItem(tr("Text beside icons"), 1);
    m_toolbarStyle->addItem(tr("Text under icons"), 2);
    m_toolbarStyle->setCurrentIndex(
        qBound(0, QSettings().value(kKeyToolbarStyle, 2).toInt(), m_toolbarStyle->count() - 1));
    tgui::theme::addFormRow(interfaceForm, tr("Toolbar buttons:"), m_toolbarStyle);

    m_progressColors = tgui::theme::loadProgressColors(QSettings());
    auto* progressColors = new QPushButton(tr("Progress bar colors…"), interfacePage);
    progressColors->setObjectName(QStringLiteral("progressColorsButton"));
    connect(progressColors, &QPushButton::clicked, this, &OptionsDialog::editProgressColors);
    tgui::theme::addFullRow(interfaceForm, progressColors);

    auto* tray = new QGroupBox(tr("Tray icon"), interfacePage);
    auto* trayGrid = new QGridLayout(tray);
    trayGrid->setHorizontalSpacing(16);
    trayGrid->setVerticalSpacing(8);
    m_minimizeToTray = new QCheckBox(tr("Minimize to tray"), tray);
    m_minimizeToTray->setChecked(QSettings().value(kKeyMinimizeToTray, false).toBool());
    m_closeToTray = new QCheckBox(tr("Close to tray"), tray);
    m_closeToTray->setChecked(QSettings().value(kKeyCloseToTray, true).toBool());
    m_trayIcon = new QCheckBox(tr("Always show the tray icon"), tray);
    m_trayIcon->setChecked(QSettings().value(kKeyTrayIcon, true).toBool());
    m_trayNotify = new QCheckBox(tr("Show notifications"), tray);
    m_trayNotify->setChecked(QSettings().value(kKeyTrayNotify, false).toBool());
    trayGrid->addWidget(m_minimizeToTray, 0, 0);
    trayGrid->addWidget(m_closeToTray, 0, 1);
    trayGrid->addWidget(m_trayIcon, 1, 0);
    trayGrid->addWidget(m_trayNotify, 1, 1);
    trayGrid->setColumnStretch(0, 1);
    trayGrid->setColumnStretch(1, 1);
    tgui::theme::addFullRow(interfaceForm, tray);

    auto* updateRow = new QHBoxLayout;
    updateRow->setSpacing(8);
    m_checkUpdates = new QCheckBox(tr("Check for updates every:"), interfacePage);
    m_checkUpdates->setObjectName(QStringLiteral("checkUpdates"));
    m_checkUpdates->setChecked(QSettings().value("ui/updateCheckEnabled", true).toBool());
    m_updateIntervalDays = new QSpinBox(interfacePage);
    m_updateIntervalDays->setObjectName(QStringLiteral("updateIntervalDays"));
    m_updateIntervalDays->setRange(1, 365);
    m_updateIntervalDays->setSuffix(tr(" days"));
    m_updateIntervalDays->setValue(QSettings().value("ui/updateCheckDays", 7).toInt());
    m_updateIntervalDays->setEnabled(m_checkUpdates->isChecked());
    updateRow->addWidget(m_checkUpdates);
    updateRow->addWidget(m_updateIntervalDays);
    updateRow->addStretch(1);
    connect(m_checkUpdates, &QCheckBox::toggled, m_updateIntervalDays, &QWidget::setEnabled);
    tgui::theme::addFullRow(interfaceForm, updateRow);

    auto* integration = new QGroupBox(tr("System integration"), interfacePage);
    auto* integrationForm = tgui::theme::formGrid(integration);
#ifdef Q_OS_WIN
    m_torrentHandler = new QCheckBox(tr("Open .torrent files with this application"), integration);
    m_torrentHandler->setObjectName(QStringLiteral("torrentHandler"));
    m_torrentHandler->setChecked(torrentHandlerRegistered());
    tgui::theme::addFullRow(integrationForm, m_torrentHandler);
    m_magnetHandler = new QCheckBox(tr("Open magnet links with this application"), integration);
    m_magnetHandler->setObjectName(QStringLiteral("magnetHandler"));
    m_magnetHandler->setChecked(magnetHandlerRegistered());
    tgui::theme::addFullRow(integrationForm, m_magnetHandler);
#else
    tgui::theme::addFullRow(integrationForm, noteLabel(
        tr("File and magnet associations are configured by the desktop environment."), integration));
#endif
    tgui::theme::addFullRow(interfaceForm, integration);
    interfaceForm->setRowStretch(interfaceForm->rowCount(), 1);

    // --- Пути ------------------------------------------------------------
    auto* pathsPage = new QWidget(tabs);
    auto* pathsForm = tgui::theme::formGrid(pathsPage);
    pathsForm->setContentsMargins(14, 12, 14, 12);
    tgui::theme::addFullRow(pathsForm, noteLabel(
        tr("Map the daemon's (remote) paths to local ones, one pair per line: remote=local.\n"
           "Example: /var/downloads/music=Z:/music\n"
           "Used when opening files/folders with double-click."),
        pathsPage));
    auto* profileLabel = new QLabel(profile.isEmpty() ? tr("Connections without a profile")
                                                     : tr("Profile: %1").arg(profile),
                                    pathsPage);
    profileLabel->setTextFormat(Qt::PlainText);
    tgui::theme::addFullRow(pathsForm, profileLabel);
    m_paths = new QPlainTextEdit(pathsPage);
    m_paths->setPlainText(m_preferences.mappings().join(QLatin1Char('\n')));
    tgui::theme::addFullRow(pathsForm, m_paths);
    pathsForm->setRowStretch(pathsForm->rowCount() - 1, 1);

    // --- Ограничения скорости -------------------------------------------
    auto* speedsPage = new QWidget(tabs);
    auto* speedsForm = tgui::theme::formGrid(speedsPage);
    speedsForm->setContentsMargins(14, 12, 14, 12);
    tgui::theme::addFullRow(speedsForm, noteLabel(
        tr("Speed presets for the limit menu, separated by commas or spaces."), speedsPage));
    const auto makeSpeedsEdit = [this, speedsPage](bool download, const QString& objectName) {
        QStringList values;
        for (int speed : m_preferences.speedPresets(download))
            values.append(QString::number(speed));
        auto* edit = new QLineEdit(values.join(QStringLiteral(", ")), speedsPage);
        edit->setObjectName(objectName);
        return edit;
    };
    m_downloadSpeeds = makeSpeedsEdit(true, QStringLiteral("downloadSpeedPresets"));
    m_uploadSpeeds = makeSpeedsEdit(false, QStringLiteral("uploadSpeedPresets"));
    tgui::theme::addFormRow(speedsForm, tr("Download presets (KB/s):"), m_downloadSpeeds);
    tgui::theme::addFormRow(speedsForm, tr("Upload presets (KB/s):"), m_uploadSpeeds);
    speedsForm->setRowStretch(speedsForm->rowCount(), 1);

    // --- Добавление торрентов -------------------------------------------
    auto* addingPage = new QWidget(tabs);
    auto* addingLayout = new QVBoxLayout(addingPage);
    addingLayout->setContentsMargins(14, 12, 14, 12);
    addingLayout->setSpacing(7);

    auto* watch = new QGroupBox(tr("Watch folder"), addingPage);
    auto* watchForm = tgui::theme::formGrid(watch);
    tgui::theme::addFullRow(watchForm, noteLabel(
        tr("Torrent files placed in the folder are added automatically while the application is running."),
        watch));
    auto* folderRow = new QHBoxLayout;
    m_watchFolder = new QLineEdit(QSettings().value(watchFolderKey).toString(), watch);
    m_watchFolder->setObjectName(QStringLiteral("watchFolder"));
    m_watchFolder->setPlaceholderText(tr("Leave empty to disable monitoring"));
    auto* browse = new QPushButton(tr("Browse…"), watch);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Choose a watch folder"), m_watchFolder->text());
        if (!path.isEmpty())
            m_watchFolder->setText(path);
    });
    folderRow->addWidget(m_watchFolder, 1);
    folderRow->addWidget(browse);
    tgui::theme::addFormRow(watchForm, tr("Local folder:"), folderRow);
    m_watchDestination = new QLineEdit(QSettings().value(watchDestinationKey).toString(), watch);
    m_watchDestination->setObjectName(QStringLiteral("watchDestination"));
    m_watchDestination->setPlaceholderText(tr("Use the daemon's default folder"));
    tgui::theme::addFormRow(watchForm, tr("Download folder on the daemon:"), m_watchDestination);
    addingLayout->addWidget(watch);
    addingLayout->addStretch(1);

    // --- Последовательная загрузка --------------------------------------
    auto* sequentialPage = new QWidget(tabs);
    auto* sequentialLayout = new QVBoxLayout(sequentialPage);
    sequentialLayout->setContentsMargins(14, 12, 14, 12);
    sequentialLayout->setSpacing(8);
    m_autoSequential = new QCheckBox(tr("Enable automatically for files with these extensions:"), sequentialPage);
    m_autoSequential->setObjectName(QStringLiteral("autoSequential"));
    m_autoSequential->setChecked(QSettings().value("downloads/autoSequential", false).toBool());
    auto* hint = noteLabel(
        tr("Only for torrents containing one file. You can change the checkbox in the Add torrent dialog."),
        sequentialPage);
    m_sequentialExtensions = new QPlainTextEdit(sequentialPage);
    m_sequentialExtensions->setObjectName(QStringLiteral("sequentialExtensions"));
    m_sequentialExtensions->setPlainText(QSettings().value("downloads/sequentialExtensions", tgui::defaultSequentialExtensions()).toString());
    sequentialLayout->addWidget(m_autoSequential);
    sequentialLayout->addWidget(hint);
    sequentialLayout->addWidget(m_sequentialExtensions, 1);
    if (rpcVersion < 18) {
        sequentialLayout->addWidget(noteLabel(
            tr("Requires Transmission 4.1 or newer (RPC 18) to edit this setting."), sequentialPage));
        m_autoSequential->setEnabled(false);
    }
    m_sequentialExtensions->setEnabled(rpcVersion >= 18 && m_autoSequential->isChecked());
    connect(m_autoSequential, &QCheckBox::toggled, m_sequentialExtensions, &QWidget::setEnabled);

    // --- Метки -----------------------------------------------------------
    auto* labelsPage = new QWidget(tabs);
    auto* labelsLayout = new QVBoxLayout(labelsPage);
    labelsLayout->setContentsMargins(14, 12, 14, 12);
    labelsLayout->setSpacing(8);
    labelsLayout->addWidget(noteLabel(
        tr("The label catalog is used in the add torrent dialog, torrent properties, the context menu and the filter panel."),
        labelsPage));
    m_labelList = new QListWidget(labelsPage);
    m_labelList->setObjectName(QStringLiteral("labelList"));
    for (const auto& label : torrentlabels::configured())
        addLabelItem(m_labelList, label);

    auto* addButton = new QPushButton(tr("Add…"), labelsPage);
    addButton->setObjectName(QStringLiteral("addLabel"));
    auto* renameButton = new QPushButton(tr("Rename…"), labelsPage);
    renameButton->setObjectName(QStringLiteral("renameLabel"));
    auto* deleteButton = new QPushButton(tr("Delete"), labelsPage);
    deleteButton->setObjectName(QStringLiteral("deleteLabel"));
    auto* upButton = new QPushButton(tr("Up"), labelsPage);
    upButton->setObjectName(QStringLiteral("moveLabelUp"));
    auto* downButton = new QPushButton(tr("Down"), labelsPage);
    downButton->setObjectName(QStringLiteral("moveLabelDown"));
    auto* restore = new QPushButton(tr("Standard"), labelsPage);
    restore->setObjectName(QStringLiteral("restoreDefaultLabels"));
    auto* labelButtons = new QVBoxLayout;
    labelButtons->setSpacing(6);
    labelButtons->addWidget(addButton);
    labelButtons->addWidget(renameButton);
    labelButtons->addWidget(deleteButton);
    labelButtons->addWidget(upButton);
    labelButtons->addWidget(downButton);
    labelButtons->addStretch(1);
    labelButtons->addWidget(restore);

    auto* labelColumns = new QHBoxLayout;
    labelColumns->setSpacing(8);
    labelColumns->addWidget(m_labelList, 1);
    labelColumns->addLayout(labelButtons);
    labelsLayout->addLayout(labelColumns, 1);
    labelsLayout->addWidget(noteLabel(
        tr("Renaming a label is applied to all torrents on the server (Transmission 4.0+, RPC 16)."),
        labelsPage));

    connect(addButton, &QPushButton::clicked, this, &OptionsDialog::addLabel);
    connect(deleteButton, &QPushButton::clicked, this, &OptionsDialog::removeLabel);
    connect(upButton, &QPushButton::clicked, this, [this] { moveLabel(-1); });
    connect(downButton, &QPushButton::clicked, this, [this] { moveLabel(1); });
    connect(restore, &QPushButton::clicked, this, &OptionsDialog::restoreDefaultLabels);
    connect(m_labelList, &QListWidget::itemDoubleClicked, this, [this, renameButton] { renameButton->click(); });
    connect(m_labelList, &QListWidget::currentRowChanged, this, &OptionsDialog::updateLabelActions);
    connect(m_labelList, &QListWidget::itemSelectionChanged, this, &OptionsDialog::updateLabelActions);
    connect(m_labelList->model(), &QAbstractItemModel::rowsInserted, this,
            [this] { updateLabelActions(); });
    connect(m_labelList->model(), &QAbstractItemModel::rowsRemoved, this,
            [this] { updateLabelActions(); });
    if (rpc) {
        connect(renameButton, &QPushButton::clicked, this, [this] {
            if (m_rpc)
                renameLabel(*m_rpc);
        });
        connect(rpc, &QObject::destroyed, this, [this] {
            m_rpc = nullptr;
            if (m_renamingLabel) {
                m_renamingLabel = false;
                setEnabled(true);
            }
            updateLabelActions();
        });
    } else {
        connect(renameButton, &QPushButton::clicked, this, [this] {
            auto* item = m_labelList->currentItem();
            if (!item)
                return;
            bool accepted = false;
            const auto name = QInputDialog::getText(this, tr("Rename label"), tr("New name:"),
                                                    QLineEdit::Normal, item->text(), &accepted).trimmed();
            if (accepted && !name.isEmpty())
                item->setText(name);
            updateLabelActions();
        });
    }
    updateLabelActions();

    // Порядок вкладок — как в макете (дополнительные возможности — в конце).
    tabs->addTab(general, tr("General"));
    tabs->addTab(interfacePage, tr("Interface"));
    tabs->addTab(sequentialPage, tr("Sequential download"));
    tabs->addTab(labelsPage, tr("Labels"));
    tabs->addTab(pathsPage, tr("Paths"));
    tabs->addTab(speedsPage, tr("Speed limits"));
    tabs->addTab(addingPage, tr("Adding torrents"));

    // --- Кнопки ----------------------------------------------------------
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, tabs, labelsPage] {
        if (apply()) {
            accept();
        } else {
            tabs->setCurrentWidget(labelsPage);
            m_labelList->setFocus();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(8);
    root->addWidget(tabs, 1);
    root->addWidget(buttons);
    // Ширина окна подстраивается под панель вкладок: в русском языке
    // подписи длиннее, и все вкладки должны быть видны без стрелок прокрутки.
    const int tabsWidth = tabs->tabBar()->sizeHint().width();
    if (tabsWidth > 0)
        resize(qMax(width(), tabsWidth + 24), height());
    tgui::theme::decorateDialog(this);
}

/** Позволяет выбрать цвета и вернуть стандартную палитру до сохранения настроек. */
void OptionsDialog::editProgressColors()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Progress bar colors"));
    auto* form = tgui::theme::formGrid(&dialog);
    auto edited = m_progressColors;
    const std::array labels{tr("Downloading"), tr("Seeding"), tr("Finished"), tr("Waiting"),
                            tr("Verifying"), tr("Paused"), tr("Error")};
    static_assert(labels.size() == std::tuple_size_v<tgui::theme::ProgressColors>);
    std::array<QPushButton*, labels.size()> fields{};
    const auto updateField = [&edited, &fields](size_t index) {
        const auto color = edited[index];
        QPixmap swatch(24, 14);
        swatch.fill(color);
        fields[index]->setIcon(QIcon(swatch));
        fields[index]->setIconSize(swatch.size());
        fields[index]->setText(color.name().toUpper());
    };
    for (size_t i = 0; i < labels.size(); ++i) {
        auto* button = new QPushButton(&dialog);
        fields[i] = button;
        updateField(i);
        tgui::theme::addFormRow(form, labels[i], button);
        connect(button, &QPushButton::clicked, &dialog, [&, i] {
            const auto color = QColorDialog::getColor(edited[i], &dialog, labels[i],
                                                       QColorDialog::DontUseNativeDialog);
            if (color.isValid()) {
                edited[i] = color;
                updateField(i);
            }
        });
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* reset = buttons->addButton(tr("Restore defaults"), QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("resetProgressColors"));
    connect(reset, &QPushButton::clicked, &dialog, [&] {
        edited = tgui::theme::defaultProgressColors();
        for (size_t i = 0; i < fields.size(); ++i)
            updateField(i);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    tgui::theme::addFullRow(form, buttons);
    tgui::theme::decorateDialog(&dialog);
    if (dialog.exec() == QDialog::Accepted)
        m_progressColors = edited;
}

void OptionsDialog::setCurrentPage(int page)
{
    if (m_tabs && page >= 0 && page < m_tabs->count())
        m_tabs->setCurrentIndex(page);
}

QString OptionsDialog::currentLanguage() const
{
    return m_language->currentData().toString();
}

void OptionsDialog::setLanguage(const QString& code)
{
    const int idx = m_language->findData(code);
    if (idx >= 0)
        m_language->setCurrentIndex(idx);
}

int OptionsDialog::scale() const
{
    return m_scale->value();
}

void OptionsDialog::setScale(int percent)
{
    m_scale->setValue(percent);
}

int OptionsDialog::refreshIntervalSec() const
{
    return m_refresh->value();
}

void OptionsDialog::setRefreshIntervalSec(int sec)
{
    m_refresh->setValue(sec);
}

QString OptionsDialog::pathMappingsText() const
{
    return m_paths->toPlainText();
}

void OptionsDialog::setPathMappingsText(const QString& text)
{
    m_paths->setPlainText(text);
}

void OptionsDialog::addLabel()
{
    bool accepted = false;
    const auto name = QInputDialog::getText(this, tr("New label"), tr("Name:"),
                                            QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;
    if (name.contains(QLatin1Char(',')) || labelNames(*m_labelList).contains(name)) {
        QMessageBox::warning(this, tr("New label"), tr("Enter a unique name without commas."));
        return;
    }
    addLabelItem(m_labelList, name);
    m_labelList->setCurrentRow(m_labelList->count() - 1);
    updateLabelActions();
}

void OptionsDialog::removeLabel()
{
    delete m_labelList->takeItem(m_labelList->currentRow());
    updateLabelActions();
}

void OptionsDialog::moveLabel(int delta)
{
    const int row = m_labelList->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_labelList->count())
        return;
    QListWidgetItem* item = m_labelList->takeItem(row);
    m_labelList->insertItem(target, item);
    m_labelList->setCurrentRow(target);
}

void OptionsDialog::restoreDefaultLabels()
{
    auto labels = labelNames(*m_labelList);
    for (const auto& name : torrentlabels::presets())
        if (!labels.contains(name))
            addLabelItem(m_labelList, name);
    updateLabelActions();
}

void OptionsDialog::updateLabelActions()
{
    auto* restore = findChild<QPushButton*>(QStringLiteral("restoreDefaultLabels"));
    auto* rename = findChild<QPushButton*>(QStringLiteral("renameLabel"));
    const auto labels = labelNames(*m_labelList);
    const auto& defaults = torrentlabels::presets();
    if (restore)
        restore->setEnabled(std::any_of(defaults.cbegin(), defaults.cend(),
                                        [&labels](const QString& name) { return !labels.contains(name); }));
    if (rename)
        rename->setEnabled(m_labelList->currentItem() != nullptr);
}

bool OptionsDialog::apply()
{
    const auto downloadSpeeds = ProfilePreferences::parseSpeeds(m_downloadSpeeds->text());
    const auto uploadSpeeds = ProfilePreferences::parseSpeeds(m_uploadSpeeds->text());
    if (!downloadSpeeds || !uploadSpeeds) {
        QMessageBox::warning(this, tr("Speed limits"), tr("Speed presets must be non-negative whole numbers."));
        return false;
    }
    const auto labels = labelNames(*m_labelList);
    for (const auto& label : labels) {
        if (label.contains(QLatin1Char(','))) {
            QMessageBox::warning(this, tr("Labels"), tr("Label names cannot contain commas."));
            return false;
        }
    }

    QSettings s;
    const QString newLang = currentLanguage();
    const int newScale = scale();
    m_restartRequired = (s.value(kKeyLanguage, QStringLiteral("ru")).toString() != newLang) ||
                        (s.value(kKeyScale, 100).toInt() != newScale);

#ifdef Q_OS_WIN
    if (m_torrentHandler && m_magnetHandler &&
        (m_torrentHandler->isChecked() != torrentHandlerRegistered() ||
         m_magnetHandler->isChecked() != magnetHandlerRegistered())) {
        if (!applyTorrentHandlers(m_torrentHandler->isChecked(), m_magnetHandler->isChecked())) {
            QMessageBox::warning(this, tr("System integration"),
                                 tr("Could not register the handlers in Windows."));
            return false;
        }
    }
#endif

    s.setValue(kKeyLanguage, newLang);
    s.setValue(kKeyScale, newScale);
    s.setValue(kKeyToolbarStyle, m_toolbarStyle->currentData().toInt());
    tgui::theme::saveProgressColors(s, m_progressColors);
    s.setValue(kKeyRefresh, refreshIntervalSec());
    s.setValue("ui/backgroundRefreshInterval", m_backgroundRefresh->value());
    s.setValue("ui/smoothRates", m_smoothRates->isChecked());
    s.setValue(kKeyCloseToTray, m_closeToTray->isChecked());
    s.setValue(kKeyMinimizeToTray, m_minimizeToTray->isChecked());
    s.setValue(kKeyTrayIcon, m_trayIcon->isChecked());
    s.setValue(kKeyTrayNotify, m_trayNotify->isChecked());
    s.setValue("ui/updateCheckEnabled", m_checkUpdates->isChecked());
    s.setValue("ui/updateCheckDays", m_updateIntervalDays->value());
    // В макете флажок «Показывать окно…»; ключ хранит обратное значение.
    s.setValue(skipAddDialogKey, !m_skipAddDialog->isChecked());
    s.setValue(deleteSourceTorrentKey, m_deleteSourceTorrent->isChecked());
    s.setValue(clipboardLinksKey, m_clipboardLinks->isChecked());
    s.setValue(watchFolderKey, m_watchFolder->text().trimmed());
    s.setValue(watchDestinationKey, m_watchDestination->text().trimmed());
    s.setValue("downloads/autoSequential", m_autoSequential->isChecked());
    s.setValue("downloads/sequentialExtensions", m_sequentialExtensions->toPlainText());
    m_preferences.setMappings(tgui::pathMappingsFromText(pathMappingsText()));
    m_preferences.setSpeedPresets(true, *downloadSpeeds);
    m_preferences.setSpeedPresets(false, *uploadSpeeds);
    s.setValue(torrentlabels::settingsKey, labels);
    s.sync();
    return true;
}

/** Оставляет окно открытым до получения результата переименования. */
void OptionsDialog::reject()
{
    if (!m_renamingLabel)
        QDialog::reject();
}

/** Сохраняет новое имя в каталоге после обновления назначений на текущем сервере. */
void OptionsDialog::renameLabel(tgui::RpcClient& rpc)
{
    auto* item = m_labelList->currentItem();
    if (!item)
        return;
    const auto from = item->text().trimmed();
    auto configured = torrentlabels::configured();
    const auto index = configured.indexOf(from);
    if (index < 0)
        return;

    if (!rpc.supports(16)) {
        bool accepted = false;
        const auto to = QInputDialog::getText(this, tr("Rename label"), tr("New name:"),
                                              QLineEdit::Normal, from, &accepted).trimmed();
        if (accepted && !to.isEmpty() && !to.contains(QLatin1Char(',')) &&
            !labelNames(*m_labelList).contains(to))
            item->setText(to);
        updateLabelActions();
        return;
    }

    const QPointer client(&rpc);
    bool accepted = false;
    const auto to = QInputDialog::getText(this, tr("Rename label"),
        tr("New name (saved immediately and applied on the connected server):"),
        QLineEdit::Normal, from, &accepted).trimmed();
    if (!accepted || !client || to == from)
        return;
    if (to.isEmpty() || to.contains(QLatin1Char(',')) || configured.contains(to) ||
        labelNames(*m_labelList).contains(to)) {
        QMessageBox::warning(this, tr("Rename label"),
                             tr("Enter a unique, nonempty name without commas."));
        return;
    }
    configured[index] = to;
    m_renamingLabel = true;
    setEnabled(false);
    rpc.renameLabel(from, to, [dialog = QPointer(this), from, to, configured](const tgui::JsonRpcResult& result) {
        if (result.ok)
            QSettings().setValue(torrentlabels::settingsKey, configured);
        if (!dialog)
            return;
        dialog->m_renamingLabel = false;
        dialog->setEnabled(true);
        if (!result.ok) {
            QMessageBox::warning(dialog, tr("Rename label"),
                tr("The rename did not finish. You can retry with the same name.\n%1").arg(result.error));
            return;
        }
        for (int i = 0; i < dialog->m_labelList->count(); ++i)
            if (dialog->m_labelList->item(i)->text().trimmed() == from)
                dialog->m_labelList->item(i)->setText(to);
        dialog->updateLabelActions();
        emit dialog->labelRenamed(from, to);
    });
}
