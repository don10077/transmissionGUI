#include "optionsdialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBlock>
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
constexpr char kKeyPaths[] = "paths/mappings";

/** Возвращает названия меток из редактора в порядке ввода. */
QStringList readLabels(const QPlainTextEdit& editor)
{
    QStringList labels;
    for (const auto& line : editor.toPlainText().split(QLatin1Char('\n'))) {
        const auto label = line.trimmed();
        if (!label.isEmpty() && !labels.contains(label))
            labels.append(label);
    }
    return labels;
}
}  // namespace

OptionsDialog::OptionsDialog(QWidget* parent, int rpcVersion, tgui::RpcClient* rpc)
    : QDialog(parent)
{
    // Как transgui/options.lfm: «Application options» + вкладки
    // General / Advanced (+ Paths для маппинга путей).
    setWindowTitle(tr("Application options"));
    resize(620, 450);

    auto* tabs = new QTabWidget(this);

    // --- General: Data display -------------------------------------------
    auto* general = new QWidget(tabs);
    auto* generalLayout = new QVBoxLayout(general);
    generalLayout->setContentsMargins(10, 10, 10, 10);

    auto* dataGroup = new QGroupBox(tr("Data display"), general);
    auto* dataForm = new QFormLayout(dataGroup);
    dataForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_refresh = new QSpinBox(dataGroup);
    m_refresh->setRange(1, 999);
    m_refresh->setMaximumWidth(160);
    m_refresh->setSuffix(tr(" seconds"));
    m_refresh->setValue(QSettings().value(kKeyRefresh, 2).toInt());
    dataForm->addRow(tr("Data refresh interval:"), m_refresh);

    generalLayout->addWidget(dataGroup);
    generalLayout->addStretch(1);
    tabs->addTab(general, tr("General"));

    // --- Advanced: tray, font size, language ------------------------------
    auto* advanced = new QWidget(tabs);
    auto* advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(10, 10, 10, 10);
    advancedLayout->setSpacing(10);

    auto* tray = new QGroupBox(tr("Tray icon"), advanced);
    auto* trayGrid = new QGridLayout(tray);
    trayGrid->setHorizontalSpacing(16);
    trayGrid->setVerticalSpacing(6);

    m_minimizeToTray = new QCheckBox(tr("Minimize to tray"), tray);
    m_minimizeToTray->setChecked(QSettings().value(kKeyMinimizeToTray, false).toBool());
    m_closeToTray = new QCheckBox(tr("Close to tray"), tray);
    m_closeToTray->setChecked(QSettings().value(kKeyCloseToTray, true).toBool());
    m_trayIcon = new QCheckBox(tr("Tray icon always visible"), tray);
    m_trayIcon->setChecked(QSettings().value(kKeyTrayIcon, true).toBool());
    m_trayNotify = new QCheckBox(tr("Show notifications in tray icon"), tray);
    m_trayNotify->setChecked(QSettings().value(kKeyTrayNotify, false).toBool());

    trayGrid->addWidget(m_minimizeToTray, 0, 0);
    trayGrid->addWidget(m_closeToTray, 1, 0);
    trayGrid->addWidget(m_trayIcon, 2, 0);
    trayGrid->addWidget(m_trayNotify, 3, 0);
    trayGrid->setColumnStretch(0, 1);
    trayGrid->setColumnStretch(1, 1);
    advancedLayout->addWidget(tray);

    auto* look = new QGroupBox(tr("Interface"), advanced);
    auto* lookForm = new QFormLayout(look);
    lookForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_scale = new QSpinBox(look);
    m_scale->setRange(75, 200);
    m_scale->setMaximumWidth(160);
    m_scale->setSingleStep(5);
    m_scale->setSuffix(tr(" %"));
    m_scale->setValue(QSettings().value(kKeyScale, 100).toInt());
    lookForm->addRow(tr("Font size:"), m_scale);

    m_language = new QComboBox(look);
    m_language->addItem(tr("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    {
        const QString lang = QSettings().value(kKeyLanguage, QStringLiteral("ru")).toString();
        const int idx = m_language->findData(lang);
        if (idx >= 0)
            m_language->setCurrentIndex(idx);
    }
    lookForm->addRow(tr("Language:"), m_language);
    generalLayout->insertWidget(1, look);
    advancedLayout->addStretch(1);
    tabs->addTab(advanced, tr("Advanced"));

    // --- Paths: маппинг remote→local (как вкладка Paths transgui) ----------
    auto* pathsPage = new QWidget(tabs);
    auto* pathsLayout = new QVBoxLayout(pathsPage);
    pathsLayout->setContentsMargins(10, 10, 10, 10);

    auto* pathsHint = new QLabel(
        tr("Map the daemon's (remote) paths to local ones, one per line: "
           "remote=local\n"
           "Example: /var/downloads/music=Z:/music\n"
           "Used when opening files/torrents with double-click."),
        pathsPage);
    pathsHint->setWordWrap(true);
    m_paths = new QPlainTextEdit(pathsPage);
    m_paths->setPlainText(
        QSettings().value(kKeyPaths, QStringList()).toStringList().join(QLatin1Char('\n')));
    pathsLayout->addWidget(pathsHint);
    pathsLayout->addWidget(m_paths, 1);
    tabs->addTab(pathsPage, tr("Paths"));

    auto* sequentialPage = new QWidget(tabs);
    auto* sequentialLayout = new QVBoxLayout(sequentialPage);
    sequentialLayout->setContentsMargins(16, 16, 16, 16);
    sequentialLayout->setSpacing(10);
    m_autoSequential = new QCheckBox(tr("Automatically enable sequential download for these extensions"), sequentialPage);
    m_autoSequential->setObjectName(QStringLiteral("autoSequential"));
    m_autoSequential->setChecked(QSettings().value("downloads/autoSequential", false).toBool());
    auto* hint = new QLabel(tr("Only for torrents containing one file. You can change the checkbox in the Add torrent dialog."), sequentialPage);
    hint->setWordWrap(true);
    auto* extensionsLabel = new QLabel(tr("Extensions (separated by spaces, commas or new lines):"), sequentialPage);
    m_sequentialExtensions = new QPlainTextEdit(sequentialPage);
    m_sequentialExtensions->setObjectName(QStringLiteral("sequentialExtensions"));
    m_sequentialExtensions->setPlainText(QSettings().value("downloads/sequentialExtensions", tgui::defaultSequentialExtensions()).toString());
    sequentialLayout->addWidget(m_autoSequential);
    sequentialLayout->addWidget(hint);
    sequentialLayout->addWidget(extensionsLabel);
    sequentialLayout->addWidget(m_sequentialExtensions, 1);
    if (rpcVersion < 18) {
        auto* unsupported = new QLabel(tr("Connect to a daemon supporting sequential download (RPC 18 or newer) to edit this setting."), sequentialPage);
        unsupported->setWordWrap(true);
        sequentialLayout->addWidget(unsupported);
        m_autoSequential->setEnabled(false);
    }
    m_sequentialExtensions->setEnabled(rpcVersion >= 18 && m_autoSequential->isChecked());
    connect(m_autoSequential, &QCheckBox::toggled, m_sequentialExtensions, &QWidget::setEnabled);
    tabs->addTab(sequentialPage, tr("Sequential download"));

    auto* labelsPage = new QWidget(tabs);
    auto* labelsLayout = new QVBoxLayout(labelsPage);
    m_labels = new QPlainTextEdit(torrentlabels::configured().join(QLatin1Char('\n')), labelsPage);
    m_labels->setObjectName(QStringLiteral("labelNames"));
    auto* labelHint = new QLabel(tr("Enter one label per line."), labelsPage);
    labelHint->setBuddy(m_labels);
    labelsLayout->addWidget(labelHint);
    labelsLayout->addWidget(m_labels);
    auto* labelActions = new QHBoxLayout;
    auto* restore = new QPushButton(tr("Restore default labels"), labelsPage);
    restore->setObjectName(QStringLiteral("restoreDefaultLabels"));
    auto* rename = new QPushButton(tr("Rename selected label…"), labelsPage);
    rename->setObjectName(QStringLiteral("renameLabel"));
    labelActions->addWidget(restore);
    labelActions->addWidget(rename);
    labelActions->addStretch(1);
    labelsLayout->addLayout(labelActions);

    const auto updateLabelActions = [this, restore, rename, client = QPointer(rpc)] {
        const auto labels = readLabels(*m_labels);
        const auto& defaults = torrentlabels::presets();
        restore->setEnabled(std::any_of(defaults.cbegin(), defaults.cend(),
                                       [&labels](const QString& name) { return !labels.contains(name); }));
        rename->setEnabled(client && client->supports(16) &&
                           torrentlabels::configured().contains(m_labels->textCursor().block().text().trimmed()));
    };
    connect(m_labels, &QPlainTextEdit::textChanged, this, updateLabelActions);
    connect(m_labels, &QPlainTextEdit::cursorPositionChanged, this, updateLabelActions);
    connect(restore, &QPushButton::clicked, this, [this] {
        auto labels = readLabels(*m_labels);
        for (const auto& name : torrentlabels::presets())
            if (!labels.contains(name))
                labels.append(name);
        m_labels->setPlainText(labels.join(QLatin1Char('\n')));
    });
    if (rpc) {
        connect(rename, &QPushButton::clicked, this, [this, rpc] { renameLabel(*rpc); });
        connect(rpc, &QObject::destroyed, this, [this, rename] {
            if (m_renamingLabel) {
                m_renamingLabel = false;
                setEnabled(true);
            }
            rename->setEnabled(false);
        });
    }
    updateLabelActions();
    tabs->addTab(labelsPage, tr("Labels"));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, tabs, labelsPage] {
        if (apply()) {
            accept();
        } else {
            tabs->setCurrentWidget(labelsPage);
            m_labels->setFocus();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    root->addWidget(tabs, 1);
    root->addWidget(buttons);
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

bool OptionsDialog::apply()
{
    const auto labels = readLabels(*m_labels);
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

    s.setValue(kKeyLanguage, newLang);
    s.setValue(kKeyScale, newScale);
    s.setValue(kKeyRefresh, refreshIntervalSec());
    s.setValue(kKeyCloseToTray, m_closeToTray->isChecked());
    s.setValue(kKeyMinimizeToTray, m_minimizeToTray->isChecked());
    s.setValue(kKeyTrayIcon, m_trayIcon->isChecked());
    s.setValue(kKeyTrayNotify, m_trayNotify->isChecked());
    s.setValue("downloads/autoSequential", m_autoSequential->isChecked());
    s.setValue("downloads/sequentialExtensions", m_sequentialExtensions->toPlainText());
    s.setValue(kKeyPaths, tgui::pathMappingsFromText(pathMappingsText()));
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
    const auto from = m_labels->textCursor().block().text().trimmed();
    auto configured = torrentlabels::configured();
    const auto index = configured.indexOf(from);
    if (index < 0)
        return;

    const QPointer client(&rpc);
    bool accepted = false;
    const auto to = QInputDialog::getText(this, tr("Rename label"),
        tr("New name (saved immediately and applied on the connected server):"),
        QLineEdit::Normal, from, &accepted).trimmed();
    if (!accepted || !client || to == from)
        return;
    if (to.isEmpty() || to.contains(QLatin1Char(',')) || configured.contains(to) ||
        readLabels(*m_labels).contains(to)) {
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
        auto lines = dialog->m_labels->toPlainText().split(QLatin1Char('\n'));
        for (auto& line : lines)
            if (line.trimmed() == from)
                line = to;
        dialog->m_labels->setPlainText(lines.join(QLatin1Char('\n')));
    });
}
