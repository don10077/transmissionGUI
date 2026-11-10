#include "optionsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "pathmap.h"

namespace {
constexpr char kKeyLanguage[] = "ui/language";
constexpr char kKeyScale[] = "ui/scale";
constexpr char kKeyRefresh[] = "ui/refreshInterval";
constexpr char kKeyCloseToTray[] = "ui/closeToTray";
constexpr char kKeyMinimizeToTray[] = "ui/minimizeToTray";
constexpr char kKeyTrayIcon[] = "ui/trayIcon";
constexpr char kKeyTrayNotify[] = "ui/trayNotify";
constexpr char kKeyPaths[] = "paths/mappings";
}  // namespace

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    // Как transgui/options.lfm: «Application options» + вкладки
    // General / Advanced (+ Paths для маппинга путей).
    setWindowTitle(tr("Application options"));
    resize(560, 380);

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
    trayGrid->addWidget(m_closeToTray, 0, 1);
    trayGrid->addWidget(m_trayIcon, 1, 0);
    trayGrid->addWidget(m_trayNotify, 1, 1);
    trayGrid->setColumnStretch(0, 1);
    trayGrid->setColumnStretch(1, 1);
    advancedLayout->addWidget(tray);

    auto* look = new QGroupBox(tr("Interface"), advanced);
    auto* lookForm = new QFormLayout(look);
    lookForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_scale = new QSpinBox(look);
    m_scale->setRange(75, 200);
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
    advancedLayout->addWidget(look);
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

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
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

void OptionsDialog::apply()
{
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
    s.setValue(kKeyPaths, tgui::pathMappingsFromText(pathMappingsText()));
    s.sync();
}
