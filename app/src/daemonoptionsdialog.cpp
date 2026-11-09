#include "daemonoptionsdialog.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

DaemonSettings DaemonSettings::fromSession(const tgui::SessionInfo& s)
{
    DaemonSettings d;
    d.speedLimitDownEnabled = s.speedLimitDownEnabled;
    d.speedLimitDown = s.speedLimitDown;
    d.speedLimitUpEnabled = s.speedLimitUpEnabled;
    d.speedLimitUp = s.speedLimitUp;
    d.altSpeedEnabled = s.altSpeedEnabled;
    d.altSpeedDown = s.altSpeedDown > 0 ? s.altSpeedDown : 100;
    d.altSpeedUp = s.altSpeedUp > 0 ? s.altSpeedUp : 100;
    d.dht = s.dhtEnabled;
    d.pex = s.pexEnabled;
    d.lpd = s.lpdEnabled;
    d.utp = s.utpEnabled;
    d.peerPort = s.peerPort > 0 ? s.peerPort : 51413;
    d.downloadDir = s.downloadDir;
    d.startAddedTorrents = s.startAddedTorrents;
    d.ratioLimitEnabled = s.ratioLimitEnabled;
    d.ratioLimit = s.ratioLimit > 0 ? s.ratioLimit : 2.0;
    d.blocklistEnabled = s.blocklistEnabled;
    d.blocklistUrl = s.blocklistUrl;
    return d;
}

QJsonObject DaemonSettings::toSessionSetJson() const
{
    QJsonObject j;
    j[QStringLiteral("speed-limit-down-enabled")] = speedLimitDownEnabled;
    j[QStringLiteral("speed-limit-down")] = speedLimitDown;
    j[QStringLiteral("speed-limit-up-enabled")] = speedLimitUpEnabled;
    j[QStringLiteral("speed-limit-up")] = speedLimitUp;
    j[QStringLiteral("alt-speed-enabled")] = altSpeedEnabled;
    j[QStringLiteral("alt-speed-down")] = altSpeedDown;
    j[QStringLiteral("alt-speed-up")] = altSpeedUp;
    j[QStringLiteral("dht-enabled")] = dht;
    j[QStringLiteral("pex-enabled")] = pex;
    j[QStringLiteral("lpd-enabled")] = lpd;
    j[QStringLiteral("utp-enabled")] = utp;
    j[QStringLiteral("peer-port")] = peerPort;
    j[QStringLiteral("download-dir")] = downloadDir;
    j[QStringLiteral("start-added-torrents")] = startAddedTorrents;
    j[QStringLiteral("ratio-limit-enabled")] = ratioLimitEnabled;
    j[QStringLiteral("ratio-limit")] = ratioLimit;
    j[QStringLiteral("blocklist-enabled")] = blocklistEnabled;
    j[QStringLiteral("blocklist-url")] = blocklistUrl;
    return j;
}

namespace {
QSpinBox* makeKbSpin(QWidget* parent, int value)
{
    auto* s = new QSpinBox(parent);
    s->setRange(1, 1000000);
    s->setSuffix(QStringLiteral(" KB/s"));
    s->setValue(value > 0 ? value : 100);
    return s;
}
}  // namespace

DaemonOptionsDialog::DaemonOptionsDialog(const tgui::SessionInfo& initial, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Daemon options"));
    const DaemonSettings s = DaemonSettings::fromSession(initial);

    // --- Bandwidth ---
    auto* bw = new QGroupBox(tr("Bandwidth"), this);
    auto* bwForm = new QFormLayout(bw);
    m_speedLimitDownEnabled = new QCheckBox(tr("Limit"), bw);
    m_speedLimitDown = makeKbSpin(bw, s.speedLimitDown);
    auto* rowDown = new QWidget(bw);
    {
        auto* l = new QVBoxLayout(rowDown);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_speedLimitDownEnabled);
        l->addWidget(m_speedLimitDown);
    }
    bwForm->addRow(tr("Download (KB/s)"), rowDown);
    m_speedLimitUpEnabled = new QCheckBox(tr("Limit"), bw);
    m_speedLimitUp = makeKbSpin(bw, s.speedLimitUp);
    auto* rowUp = new QWidget(bw);
    {
        auto* l = new QVBoxLayout(rowUp);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_speedLimitUpEnabled);
        l->addWidget(m_speedLimitUp);
    }
    bwForm->addRow(tr("Upload (KB/s)"), rowUp);
    m_altSpeedEnabled = new QCheckBox(tr("Alternative speeds (when enabled, override the limits above)"), bw);
    bwForm->addRow(QString(), m_altSpeedEnabled);
    auto* altRow = new QWidget(bw);
    {
        auto* l = new QHBoxLayout(altRow);
        m_altSpeedDown = makeKbSpin(altRow, s.altSpeedDown);
        m_altSpeedUp = makeKbSpin(altRow, s.altSpeedUp);
        l->addWidget(m_altSpeedDown);
        l->addWidget(m_altSpeedUp);
    }
    bwForm->addRow(tr("Alt speed (KB/s)"), altRow);

    // --- Network ---
    auto* net = new QGroupBox(tr("Network"), this);
    auto* netLayout = new QVBoxLayout(net);
    m_dht = new QCheckBox(tr("Enable DHT (IPv6) peer exchange"), net);
    m_pex = new QCheckBox(tr("Enable peer exchange (PEX)"), net);
    m_lpd = new QCheckBox(tr("Enable Local Peer Discovery (LPD)"), net);
    m_utp = new QCheckBox(tr("Enable uTP"), net);
    for (auto* cb : {m_dht, m_pex, m_lpd, m_utp})
        netLayout->addWidget(cb);
    m_peerPort = new QSpinBox(net);
    m_peerPort->setRange(1, 65535);
    auto* portRow = new QHBoxLayout;
    portRow->addWidget(new QLabel(tr("Incoming peer port"), net));
    portRow->addWidget(m_peerPort);
    portRow->addStretch(1);
    netLayout->addLayout(portRow);

    // --- Download ---
    auto* dl = new QGroupBox(tr("Download"), this);
    auto* dlForm = new QFormLayout(dl);
    m_downloadDir = new QLineEdit(dl);
    dlForm->addRow(tr("Directory"), m_downloadDir);
    m_startAddedTorrents = new QCheckBox(tr("Start added torrents"), dl);
    dlForm->addRow(QString(), m_startAddedTorrents);

    // --- Other ---
    auto* other = new QGroupBox(tr("Other"), this);
    auto* otherForm = new QFormLayout(other);
    m_ratioLimitEnabled = new QCheckBox(tr("Stop seeding at ratio"), other);
    otherForm->addRow(QString(), m_ratioLimitEnabled);
    m_ratioLimit = new QDoubleSpinBox(other);
    m_ratioLimit->setRange(0.0, 100.0);
    m_ratioLimit->setDecimals(2);
    otherForm->addRow(tr("Seed ratio limit"), m_ratioLimit);
    m_blocklistEnabled = new QCheckBox(tr("Enable blocklist"), other);
    otherForm->addRow(QString(), m_blocklistEnabled);
    m_blocklistUrl = new QLineEdit(other);
    otherForm->addRow(tr("Blocklist URL"), m_blocklistUrl);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* root = new QVBoxLayout(this);
    root->addWidget(bw);
    root->addWidget(net);
    root->addWidget(dl);
    root->addWidget(other);
    root->addStretch(1);
    root->addWidget(buttons);

    connect(m_speedLimitDownEnabled, &QCheckBox::toggled, m_speedLimitDown, &QWidget::setEnabled);
    connect(m_speedLimitUpEnabled, &QCheckBox::toggled, m_speedLimitUp, &QWidget::setEnabled);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setSettings(s);
}

void DaemonOptionsDialog::setSettings(const DaemonSettings& s)
{
    m_speedLimitDownEnabled->setChecked(s.speedLimitDownEnabled);
    m_speedLimitDown->setValue(s.speedLimitDown > 0 ? s.speedLimitDown : 100);
    m_speedLimitUpEnabled->setChecked(s.speedLimitUpEnabled);
    m_speedLimitUp->setValue(s.speedLimitUp > 0 ? s.speedLimitUp : 100);
    m_altSpeedEnabled->setChecked(s.altSpeedEnabled);
    m_altSpeedDown->setValue(s.altSpeedDown > 0 ? s.altSpeedDown : 100);
    m_altSpeedUp->setValue(s.altSpeedUp > 0 ? s.altSpeedUp : 100);
    m_dht->setChecked(s.dht);
    m_pex->setChecked(s.pex);
    m_lpd->setChecked(s.lpd);
    m_utp->setChecked(s.utp);
    m_peerPort->setValue(s.peerPort > 0 ? s.peerPort : 51413);
    m_downloadDir->setText(s.downloadDir);
    m_startAddedTorrents->setChecked(s.startAddedTorrents);
    m_ratioLimitEnabled->setChecked(s.ratioLimitEnabled);
    m_ratioLimit->setValue(s.ratioLimit > 0 ? s.ratioLimit : 2.0);
    m_blocklistEnabled->setChecked(s.blocklistEnabled);
    m_blocklistUrl->setText(s.blocklistUrl);
    m_speedLimitDown->setEnabled(s.speedLimitDownEnabled);
    m_speedLimitUp->setEnabled(s.speedLimitUpEnabled);
}

DaemonSettings DaemonOptionsDialog::settings() const
{
    DaemonSettings d;
    d.speedLimitDownEnabled = m_speedLimitDownEnabled->isChecked();
    d.speedLimitDown = m_speedLimitDown->value();
    d.speedLimitUpEnabled = m_speedLimitUpEnabled->isChecked();
    d.speedLimitUp = m_speedLimitUp->value();
    d.altSpeedEnabled = m_altSpeedEnabled->isChecked();
    d.altSpeedDown = m_altSpeedDown->value();
    d.altSpeedUp = m_altSpeedUp->value();
    d.dht = m_dht->isChecked();
    d.pex = m_pex->isChecked();
    d.lpd = m_lpd->isChecked();
    d.utp = m_utp->isChecked();
    d.peerPort = m_peerPort->value();
    d.downloadDir = m_downloadDir->text().trimmed();
    d.startAddedTorrents = m_startAddedTorrents->isChecked();
    d.ratioLimitEnabled = m_ratioLimitEnabled->isChecked();
    d.ratioLimit = m_ratioLimit->value();
    d.blocklistEnabled = m_blocklistEnabled->isChecked();
    d.blocklistUrl = m_blocklistUrl->text().trimmed();
    return d;
}