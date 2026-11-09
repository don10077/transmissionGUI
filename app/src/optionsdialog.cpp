#include "optionsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr char kKeyLanguage[] = "ui/language";
constexpr char kKeyScale[] = "ui/scale";
constexpr char kKeyRefresh[] = "ui/refreshInterval";
constexpr char kKeyCloseToTray[] = "ui/closeToTray";
constexpr char kKeyMinimizeToTray[] = "ui/minimizeToTray";
constexpr char kKeyTrayIcon[] = "ui/trayIcon";
constexpr char kKeyTrayNotify[] = "ui/trayNotify";
}  // namespace

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Options"));

    // --- Интерфейс: язык, масштаб, интервал обновления -------------------
    auto* iface = new QGroupBox(tr("Interface"), this);
    auto* ifaceForm = new QFormLayout(iface);

    m_language = new QComboBox(iface);
    m_language->addItem(tr("English"), QStringLiteral("en"));
    m_language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    {
        const QString lang = QSettings().value(kKeyLanguage, QStringLiteral("ru")).toString();
        const int idx = m_language->findData(lang);
        if (idx >= 0)
            m_language->setCurrentIndex(idx);
    }
    ifaceForm->addRow(tr("Language"), m_language);

    m_scale = new QSpinBox(iface);
    m_scale->setRange(50, 200);
    m_scale->setSingleStep(5);
    m_scale->setSuffix(tr(" %"));
    m_scale->setValue(QSettings().value(kKeyScale, 100).toInt());
    ifaceForm->addRow(tr("Interface scale"), m_scale);

    m_refresh = new QSpinBox(iface);
    m_refresh->setRange(1, 3600);
    m_refresh->setSuffix(tr(" s"));
    m_refresh->setValue(QSettings().value(kKeyRefresh, 2).toInt());
    ifaceForm->addRow(tr("Refresh interval"), m_refresh);

    // --- Трей -------------------------------------------------------------
    auto* tray = new QGroupBox(tr("Tray"), this);
    auto* trayLayout = new QVBoxLayout(tray);

    m_closeToTray = new QCheckBox(tr("Close to tray (instead of quitting)"), tray);
    m_closeToTray->setChecked(QSettings().value(kKeyCloseToTray, true).toBool());
    m_minimizeToTray = new QCheckBox(tr("Minimize to tray"), tray);
    m_minimizeToTray->setChecked(QSettings().value(kKeyMinimizeToTray, false).toBool());
    m_trayIcon = new QCheckBox(tr("Always show tray icon"), tray);
    m_trayIcon->setChecked(QSettings().value(kKeyTrayIcon, true).toBool());
    m_trayNotify = new QCheckBox(tr("Show tray notifications"), tray);
    m_trayNotify->setChecked(QSettings().value(kKeyTrayNotify, false).toBool());

    for (QCheckBox* cb : {m_closeToTray, m_minimizeToTray, m_trayIcon, m_trayNotify})
        trayLayout->addWidget(cb);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addWidget(iface);
    root->addWidget(tray);
    root->addStretch(1);
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
    s.sync();
}