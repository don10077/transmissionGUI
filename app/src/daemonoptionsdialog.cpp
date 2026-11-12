#include "daemonoptionsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTimeEdit>
#include <QTime>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QScrollArea>

namespace {

// Битовая маска alt-speed-time-day в Transmission: Sunday=1, Monday=2, … Saturday=64.
constexpr int kDayBits[7] = {2, 4, 8, 16, 32, 64, 1}; // Mon..Sun
const char* const kDayNames[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

QSpinBox* makeKbSpin(QWidget* parent, int value)
{
    auto* s = new QSpinBox(parent);
    s->setRange(0, 1000000000);
    s->setSuffix(QStringLiteral(" KB/s"));
    s->setValue(qMax(0, value));
    return s;
}

QSpinBox* makeCountSpin(QWidget* parent, int value, int maxValue)
{
    auto* s = new QSpinBox(parent);
    s->setRange(0, maxValue);
    s->setValue(qMax(0, value));
    return s;
}

QTime minutesToTime(int minutes)
{
    const int normalized = ((minutes % 1440) + 1440) % 1440;
    return QTime(normalized / 60, normalized % 60);
}

int timeToMinutes(const QTime& time, bool endOfDay)
{
    const int minutes = time.hour() * 60 + time.minute();
    // 00:00 в поле «конец» — это полночь (1440), как в RPC-спецификации.
    return endOfDay && minutes == 0 ? 1440 : minutes;
}

}  // namespace

DaemonSettings DaemonSettings::fromSession(const tgui::SessionInfo& s)
{
    DaemonSettings d;
    d.speedLimitDownEnabled = s.speedLimitDownEnabled;
    d.speedLimitDown = s.speedLimitDown;
    d.speedLimitUpEnabled = s.speedLimitUpEnabled;
    d.speedLimitUp = s.speedLimitUp;
    d.altSpeedEnabled = s.altSpeedEnabled;
    d.altSpeedDown = qMax(0, s.altSpeedDown);
    d.altSpeedUp = qMax(0, s.altSpeedUp);
    d.altSpeedTimeEnabled = s.altSpeedTimeEnabled;
    // 00:00 в поле «конец» трактуем как полночь (1440), чтобы diff не слал лишнее.
    d.altSpeedTimeBegin = s.altSpeedTimeBegin % 1440;
    d.altSpeedTimeEnd = s.altSpeedTimeEnd % 1440;
    if (d.altSpeedTimeEnd == 0)
        d.altSpeedTimeEnd = 1440;
    d.altSpeedTimeDay = s.altSpeedTimeDay;
    d.dht = s.dhtEnabled;
    d.pex = s.pexEnabled;
    d.lpd = s.lpdEnabled;
    d.utp = s.utpEnabled;
    d.peerPort = s.peerPort > 0 ? s.peerPort : 51413;
    d.portForwardingEnabled = s.portForwardingEnabled;
    d.peerPortRandomOnStart = s.peerPortRandomOnStart;
    d.encryption = s.encryption.isEmpty() ? QStringLiteral("preferred") : s.encryption;
    d.peerLimitGlobal = s.peerLimitGlobal;
    d.peerLimitPerTorrent = s.peerLimitPerTorrent;
    d.downloadDir = s.downloadDir;
    d.startAddedTorrents = s.startAddedTorrents;
    d.incompleteDir = s.incompleteDir;
    d.incompleteDirEnabled = s.incompleteDirEnabled;
    d.renamePartialFiles = s.renamePartialFiles;
    d.downloadQueueEnabled = s.downloadQueueEnabled;
    d.downloadQueueSize = s.downloadQueueSize;
    d.seedQueueEnabled = s.seedQueueEnabled;
    d.seedQueueSize = s.seedQueueSize;
    d.queueStalledEnabled = s.queueStalledEnabled;
    d.queueStalledMinutes = s.queueStalledMinutes;
    d.ratioLimitEnabled = s.ratioLimitEnabled;
    d.ratioLimit = qMax(0.0, s.ratioLimit);
    d.blocklistEnabled = s.blocklistEnabled;
    d.blocklistUrl = s.blocklistUrl;
    d.idleSeedingLimitEnabled = s.idleSeedingLimitEnabled;
    d.idleSeedingLimit = s.idleSeedingLimit;
    d.cacheSizeMb = s.cacheSizeMb;
    d.hasCacheSize = s.hasCacheSize;
    d.rpcVersion = s.rpcVersion;
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
    j[QStringLiteral("alt-speed-time-enabled")] = altSpeedTimeEnabled;
    j[QStringLiteral("alt-speed-time-begin")] = altSpeedTimeBegin;
    j[QStringLiteral("alt-speed-time-end")] = altSpeedTimeEnd;
    j[QStringLiteral("alt-speed-time-day")] = altSpeedTimeDay;
    j[QStringLiteral("dht-enabled")] = dht;
    j[QStringLiteral("pex-enabled")] = pex;
    j[QStringLiteral("lpd-enabled")] = lpd;
    j[QStringLiteral("utp-enabled")] = utp;
    j[QStringLiteral("peer-port")] = peerPort;
    j[QStringLiteral("port-forwarding-enabled")] = portForwardingEnabled;
    j[QStringLiteral("peer-port-random-on-start")] = peerPortRandomOnStart;
    j[QStringLiteral("encryption")] = encryption;
    j[QStringLiteral("peer-limit-global")] = peerLimitGlobal;
    j[QStringLiteral("peer-limit-per-torrent")] = peerLimitPerTorrent;
    j[QStringLiteral("download-dir")] = downloadDir;
    j[QStringLiteral("start-added-torrents")] = startAddedTorrents;
    j[QStringLiteral("incomplete-dir")] = incompleteDir;
    j[QStringLiteral("incomplete-dir-enabled")] = incompleteDirEnabled;
    j[QStringLiteral("rename-partial-files")] = renamePartialFiles;
    j[QStringLiteral("download-queue-enabled")] = downloadQueueEnabled;
    j[QStringLiteral("download-queue-size")] = downloadQueueSize;
    j[QStringLiteral("seed-queue-enabled")] = seedQueueEnabled;
    j[QStringLiteral("seed-queue-size")] = seedQueueSize;
    j[QStringLiteral("queue-stalled-enabled")] = queueStalledEnabled;
    j[QStringLiteral("queue-stalled-minutes")] = queueStalledMinutes;
    j[QStringLiteral("ratio-limit-enabled")] = ratioLimitEnabled;
    j[QStringLiteral("ratio-limit")] = ratioLimit;
    j[QStringLiteral("blocklist-enabled")] = blocklistEnabled;
    j[QStringLiteral("blocklist-url")] = blocklistUrl;
    j[QStringLiteral("idle-seeding-limit-enabled")] = idleSeedingLimitEnabled;
    j[QStringLiteral("idle-seeding-limit")] = idleSeedingLimit;
    if (hasCacheSize)
        j[QStringLiteral("cache-size-mb")] = cacheSizeMb;
    return j;
}

QJsonObject DaemonSettings::changesFrom(const DaemonSettings& base) const
{
    const QJsonObject now = toSessionSetJson();
    const QJsonObject before = base.toSessionSetJson();
    QJsonObject result;
    for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
        if (before.value(it.key()) != it.value())
            result.insert(it.key(), it.value());
    }
    return result;
}

DaemonOptionsDialog::DaemonOptionsDialog(const tgui::SessionInfo& initial, QWidget* parent)
    : QDialog(parent), m_initial(DaemonSettings::fromSession(initial))
{
    setWindowTitle(tr("Daemon options"));
    resize(640, 560);
    const DaemonSettings s = m_initial;

    // --- Bandwidth ---
    auto* bw = new QGroupBox(tr("Bandwidth"), this);
    auto* bwForm = new QFormLayout(bw);
    m_speedLimitDownEnabled = new QCheckBox(tr("Limit"), bw);
    m_speedLimitDown = makeKbSpin(bw, s.speedLimitDown);
    auto* rowDown = new QWidget(bw);
    {
        auto* l = new QHBoxLayout(rowDown);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_speedLimitDownEnabled);
        l->addWidget(m_speedLimitDown);
    }
    bwForm->addRow(tr("Download (KB/s)"), rowDown);
    m_speedLimitUpEnabled = new QCheckBox(tr("Limit"), bw);
    m_speedLimitUp = makeKbSpin(bw, s.speedLimitUp);
    auto* rowUp = new QWidget(bw);
    {
        auto* l = new QHBoxLayout(rowUp);
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
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(tr("Download"), altRow));
        l->addWidget(m_altSpeedDown);
        l->addWidget(new QLabel(tr("Upload"), altRow));
        l->addWidget(m_altSpeedUp);
    }
    bwForm->addRow(tr("Alt speed (KB/s)"), altRow);

    m_scheduleGroup = new QGroupBox(tr("Alt-speed schedule"), bw);
    m_scheduleGroup->setObjectName(QStringLiteral("scheduleGroup"));
    {
        auto* form = new QFormLayout(m_scheduleGroup);
        m_altSpeedTimeEnabled = new QCheckBox(tr("Use schedule"), m_scheduleGroup);
        form->addRow(QString(), m_altSpeedTimeEnabled);
        m_altSpeedTimeBegin = new QTimeEdit(m_scheduleGroup);
        m_altSpeedTimeBegin->setDisplayFormat(QStringLiteral("HH:mm"));
        m_altSpeedTimeEnd = new QTimeEdit(m_scheduleGroup);
        m_altSpeedTimeEnd->setDisplayFormat(QStringLiteral("HH:mm"));
        form->addRow(tr("Begin"), m_altSpeedTimeBegin);
        form->addRow(tr("End"), m_altSpeedTimeEnd);
        auto* daysRow = new QWidget(m_scheduleGroup);
        auto* daysLayout = new QHBoxLayout(daysRow);
        daysLayout->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < 7; ++i) {
            m_days[i] = new QCheckBox(tr(kDayNames[i]), daysRow);
            daysLayout->addWidget(m_days[i]);
        }
        daysLayout->addStretch(1);
        form->addRow(tr("Days"), daysRow);
        bwForm->addRow(m_scheduleGroup);
    }

    // --- Network ---
    auto* net = new QGroupBox(tr("Network"), this);
    auto* netLayout = new QVBoxLayout(net);
    m_dht = new QCheckBox(tr("Enable DHT"), net);
    m_pex = new QCheckBox(tr("Enable peer exchange (PEX)"), net);
    m_lpd = new QCheckBox(tr("Enable Local Peer Discovery (LPD)"), net);
    m_utp = new QCheckBox(tr("Enable uTP"), net);
    for (auto* cb : {m_dht, m_pex, m_lpd, m_utp})
        netLayout->addWidget(cb);

    {
        auto* encRow = new QHBoxLayout;
        encRow->addWidget(new QLabel(tr("Encryption"), net));
        m_encryption = new QComboBox(net);
        m_encryption->setObjectName(QStringLiteral("encryption"));
        m_encryption->addItem(tr("Required"), QStringLiteral("required"));
        m_encryption->addItem(tr("Preferred"), QStringLiteral("preferred"));
        m_encryption->addItem(tr("Tolerated"), QStringLiteral("tolerated"));
        encRow->addWidget(m_encryption);
        encRow->addStretch(1);
        netLayout->addLayout(encRow);
    }
    {
        auto* peersRow = new QHBoxLayout;
        m_peerLimitGlobal = makeCountSpin(net, s.peerLimitGlobal, 100000);
        m_peerLimitGlobal->setObjectName(QStringLiteral("peerLimitGlobal"));
        m_peerLimitPerTorrent = makeCountSpin(net, s.peerLimitPerTorrent, 100000);
        m_peerLimitPerTorrent->setObjectName(QStringLiteral("peerLimitPerTorrent"));
        peersRow->addWidget(new QLabel(tr("Global peer limit"), net));
        peersRow->addWidget(m_peerLimitGlobal);
        peersRow->addWidget(new QLabel(tr("Per torrent"), net));
        peersRow->addWidget(m_peerLimitPerTorrent);
        peersRow->addStretch(1);
        netLayout->addLayout(peersRow);
    }
    {
        auto* portRow = new QHBoxLayout;
        m_peerPort = new QSpinBox(net);
        m_peerPort->setRange(1, 65535);
        m_peerPort->setObjectName(QStringLiteral("peerPort"));
        portRow->addWidget(new QLabel(tr("Incoming peer port"), net));
        portRow->addWidget(m_peerPort);
        m_peerPortRandom = new QCheckBox(tr("Random port on start"), net);
        m_peerPortRandom->setObjectName(QStringLiteral("peerPortRandom"));
        portRow->addWidget(m_peerPortRandom);
        portRow->addStretch(1);
        netLayout->addLayout(portRow);
    }
    m_portForwarding = new QCheckBox(tr("Enable port forwarding"), net);
    m_portForwarding->setObjectName(QStringLiteral("portForwarding"));
    netLayout->addWidget(m_portForwarding);

    // --- Download ---
    auto* dl = new QGroupBox(tr("Download"), this);
    auto* dlForm = new QFormLayout(dl);
    m_downloadDir = new QLineEdit(dl);
    dlForm->addRow(tr("Directory"), m_downloadDir);
    m_startAddedTorrents = new QCheckBox(tr("Start added torrents"), dl);
    dlForm->addRow(QString(), m_startAddedTorrents);

    m_incompleteGroup = new QGroupBox(tr("Incomplete files"), dl);
    m_incompleteGroup->setObjectName(QStringLiteral("incompleteGroup"));
    {
        auto* form = new QFormLayout(m_incompleteGroup);
        m_incompleteDirEnabled = new QCheckBox(tr("Use incomplete folder"), m_incompleteGroup);
        m_incompleteDirEnabled->setObjectName(QStringLiteral("incompleteDirEnabled"));
        form->addRow(QString(), m_incompleteDirEnabled);
        m_incompleteDir = new QLineEdit(m_incompleteGroup);
        m_incompleteDir->setObjectName(QStringLiteral("incompleteDir"));
        form->addRow(tr("Incomplete folder"), m_incompleteDir);
        m_renamePartialFiles = new QCheckBox(tr("Append .part to incomplete files"), m_incompleteGroup);
        m_renamePartialFiles->setObjectName(QStringLiteral("renamePartialFiles"));
        form->addRow(QString(), m_renamePartialFiles);
        dlForm->addRow(m_incompleteGroup);
    }

    m_queueGroup = new QGroupBox(tr("Queues"), dl);
    m_queueGroup->setObjectName(QStringLiteral("queueGroup"));
    {
        auto* form = new QFormLayout(m_queueGroup);
        auto* downRow = new QWidget(m_queueGroup);
        {
            auto* l = new QHBoxLayout(downRow);
            l->setContentsMargins(0, 0, 0, 0);
            m_downloadQueueEnabled = new QCheckBox(tr("Limit download queue"), downRow);
            m_downloadQueueEnabled->setObjectName(QStringLiteral("downloadQueueEnabled"));
            m_downloadQueueSize = makeCountSpin(downRow, s.downloadQueueSize, 10000);
            m_downloadQueueSize->setObjectName(QStringLiteral("downloadQueueSize"));
            l->addWidget(m_downloadQueueEnabled);
            l->addWidget(m_downloadQueueSize);
            l->addStretch(1);
        }
        form->addRow(QString(), downRow);
        auto* seedRow = new QWidget(m_queueGroup);
        {
            auto* l = new QHBoxLayout(seedRow);
            l->setContentsMargins(0, 0, 0, 0);
            m_seedQueueEnabled = new QCheckBox(tr("Limit seed queue"), seedRow);
            m_seedQueueEnabled->setObjectName(QStringLiteral("seedQueueEnabled"));
            m_seedQueueSize = makeCountSpin(seedRow, s.seedQueueSize, 10000);
            m_seedQueueSize->setObjectName(QStringLiteral("seedQueueSize"));
            l->addWidget(m_seedQueueEnabled);
            l->addWidget(m_seedQueueSize);
            l->addStretch(1);
        }
        form->addRow(QString(), seedRow);
        auto* stalledRow = new QWidget(m_queueGroup);
        {
            auto* l = new QHBoxLayout(stalledRow);
            l->setContentsMargins(0, 0, 0, 0);
            m_queueStalledEnabled = new QCheckBox(tr("Treat idle torrents as stalled"), stalledRow);
            m_queueStalledEnabled->setObjectName(QStringLiteral("queueStalledEnabled"));
            m_queueStalledMinutes = makeCountSpin(stalledRow, s.queueStalledMinutes, 100000);
            m_queueStalledMinutes->setObjectName(QStringLiteral("queueStalledMinutes"));
            m_queueStalledMinutes->setSuffix(QStringLiteral(" min"));
            l->addWidget(m_queueStalledEnabled);
            l->addWidget(m_queueStalledMinutes);
            l->addStretch(1);
        }
        form->addRow(QString(), stalledRow);
        dlForm->addRow(m_queueGroup);
    }

    // --- Other ---
    auto* other = new QGroupBox(tr("Other"), this);
    auto* otherForm = new QFormLayout(other);
    m_ratioLimitEnabled = new QCheckBox(tr("Stop seeding at ratio"), other);
    otherForm->addRow(QString(), m_ratioLimitEnabled);
    m_ratioLimit = new QDoubleSpinBox(other);
    m_ratioLimit->setRange(0.0, 1000000.0);
    m_ratioLimit->setMaximumWidth(180);
    m_ratioLimit->setDecimals(2);
    otherForm->addRow(tr("Seed ratio limit"), m_ratioLimit);
    m_blocklistEnabled = new QCheckBox(tr("Enable blocklist"), other);
    otherForm->addRow(QString(), m_blocklistEnabled);
    m_blocklistUrl = new QLineEdit(other);
    otherForm->addRow(tr("Blocklist URL"), m_blocklistUrl);

    m_idleGroup = new QGroupBox(tr("Idle seeding"), other);
    m_idleGroup->setObjectName(QStringLiteral("idleGroup"));
    {
        auto* form = new QFormLayout(m_idleGroup);
        m_idleSeedingEnabled = new QCheckBox(tr("Stop seeding when idle"), m_idleGroup);
        m_idleSeedingEnabled->setObjectName(QStringLiteral("idleSeedingEnabled"));
        form->addRow(QString(), m_idleSeedingEnabled);
        m_idleSeedingLimit = makeCountSpin(m_idleGroup, s.idleSeedingLimit, 100000);
        m_idleSeedingLimit->setObjectName(QStringLiteral("idleSeedingLimit"));
        m_idleSeedingLimit->setSuffix(QStringLiteral(" min"));
        form->addRow(tr("Idle limit"), m_idleSeedingLimit);
        otherForm->addRow(m_idleGroup);
    }
    m_cacheGroup = new QGroupBox(tr("Cache"), other);
    m_cacheGroup->setObjectName(QStringLiteral("cacheGroup"));
    {
        auto* form = new QFormLayout(m_cacheGroup);
        m_cacheSize = makeCountSpin(m_cacheGroup, s.cacheSizeMb, 100000);
        m_cacheSize->setObjectName(QStringLiteral("cacheSize"));
        m_cacheSize->setSuffix(QStringLiteral(" MB"));
        form->addRow(tr("Disk cache size"), m_cacheSize);
        otherForm->addRow(m_cacheGroup);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    auto* tabs = new QTabWidget(this);
    auto page = [tabs](QWidget* content, const QString& title) {
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* body = new QWidget;
        auto* layout = new QVBoxLayout(body);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->addWidget(content);
        layout->addStretch();
        scroll->setWidget(body);
        tabs->addTab(scroll, title);
    };
    page(bw, tr("Bandwidth")); page(net, tr("Network"));
    page(dl, tr("Download")); page(other, tr("Other"));
    root->addWidget(tabs, 1);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_speedLimitDownEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_speedLimitUpEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_altSpeedEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_altSpeedTimeEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_ratioLimitEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_blocklistEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_incompleteDirEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_downloadQueueEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_seedQueueEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_queueStalledEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    connect(m_idleSeedingEnabled, &QCheckBox::toggled, this, &DaemonOptionsDialog::updateEnabledState);
    setSettings(s);
    // Сравниваем с тем, что увидел пользователь: округление и ограничения формы не считаются правкой.
    m_initial = settings();
    applyFeatureGates();
}

void DaemonOptionsDialog::applyFeatureGates()
{
    const int version = m_initial.rpcVersion;
    auto gate = [](QWidget* widget, bool allowed, const QString& reason) {
        if (!widget)
            return;
        widget->setEnabled(allowed);
        widget->setToolTip(allowed ? QString() : reason);
    };
    gate(m_scheduleGroup, version >= 5, tr("Requires RPC version 5 or newer."));
    gate(m_incompleteGroup, version >= 7, tr("Requires RPC version 7 or newer."));
    gate(m_queueGroup, version >= 14, tr("Requires RPC version 14 or newer."));
    gate(m_idleGroup, version >= 10, tr("Requires RPC version 10 or newer."));
    gate(m_cacheGroup, version >= 10 && m_initial.hasCacheSize,
         m_initial.hasCacheSize ? tr("Requires RPC version 10 or newer.")
                                : tr("The daemon does not report a cache size setting."));
    gate(m_encryption, version >= 5, tr("Requires RPC version 5 or newer."));
    gate(m_peerLimitGlobal, version >= 5, tr("Requires RPC version 5 or newer."));
    gate(m_peerLimitPerTorrent, version >= 5, tr("Requires RPC version 5 or newer."));
    gate(m_portForwarding, version >= 5, tr("Requires RPC version 5 or newer."));
    gate(m_peerPortRandom, version >= 5, tr("Requires RPC version 5 or newer."));
}

void DaemonOptionsDialog::updateEnabledState()
{
    m_altSpeedDown->setEnabled(m_altSpeedEnabled->isChecked());
    m_altSpeedUp->setEnabled(m_altSpeedEnabled->isChecked());
    m_altSpeedTimeBegin->setEnabled(m_altSpeedTimeEnabled->isChecked());
    m_altSpeedTimeEnd->setEnabled(m_altSpeedTimeEnabled->isChecked());
    for (auto* day : m_days)
        day->setEnabled(m_altSpeedTimeEnabled->isChecked());
    m_ratioLimit->setEnabled(m_ratioLimitEnabled->isChecked());
    m_blocklistUrl->setEnabled(m_blocklistEnabled->isChecked());
    m_speedLimitDown->setEnabled(m_speedLimitDownEnabled->isChecked());
    m_speedLimitUp->setEnabled(m_speedLimitUpEnabled->isChecked());
    m_incompleteDir->setEnabled(m_incompleteDirEnabled->isChecked());
    m_downloadQueueSize->setEnabled(m_downloadQueueEnabled->isChecked());
    m_seedQueueSize->setEnabled(m_seedQueueEnabled->isChecked());
    m_queueStalledMinutes->setEnabled(m_queueStalledEnabled->isChecked());
    m_idleSeedingLimit->setEnabled(m_idleSeedingEnabled->isChecked());
}

void DaemonOptionsDialog::setSettings(const DaemonSettings& s)
{
    m_speedLimitDownEnabled->setChecked(s.speedLimitDownEnabled);
    m_speedLimitDown->setValue(qMax(0, s.speedLimitDown));
    m_speedLimitUpEnabled->setChecked(s.speedLimitUpEnabled);
    m_speedLimitUp->setValue(qMax(0, s.speedLimitUp));
    m_altSpeedEnabled->setChecked(s.altSpeedEnabled);
    m_altSpeedDown->setValue(qMax(0, s.altSpeedDown));
    m_altSpeedUp->setValue(qMax(0, s.altSpeedUp));
    m_altSpeedTimeEnabled->setChecked(s.altSpeedTimeEnabled);
    m_altSpeedTimeBegin->setTime(minutesToTime(s.altSpeedTimeBegin));
    m_altSpeedTimeEnd->setTime(minutesToTime(s.altSpeedTimeEnd));
    for (int i = 0; i < 7; ++i)
        m_days[i]->setChecked((s.altSpeedTimeDay & kDayBits[i]) != 0);
    m_dht->setChecked(s.dht);
    m_pex->setChecked(s.pex);
    m_lpd->setChecked(s.lpd);
    m_utp->setChecked(s.utp);
    m_peerPort->setValue(s.peerPort > 0 ? s.peerPort : 51413);
    m_portForwarding->setChecked(s.portForwardingEnabled);
    m_peerPortRandom->setChecked(s.peerPortRandomOnStart);
    const int encryptionIndex = m_encryption->findData(s.encryption);
    m_encryption->setCurrentIndex(encryptionIndex >= 0 ? encryptionIndex
                                                       : m_encryption->findData(QStringLiteral("preferred")));
    m_peerLimitGlobal->setValue(qMax(0, s.peerLimitGlobal));
    m_peerLimitPerTorrent->setValue(qMax(0, s.peerLimitPerTorrent));
    m_downloadDir->setText(s.downloadDir);
    m_startAddedTorrents->setChecked(s.startAddedTorrents);
    m_incompleteDirEnabled->setChecked(s.incompleteDirEnabled);
    m_incompleteDir->setText(s.incompleteDir);
    m_renamePartialFiles->setChecked(s.renamePartialFiles);
    m_downloadQueueEnabled->setChecked(s.downloadQueueEnabled);
    m_downloadQueueSize->setValue(qMax(0, s.downloadQueueSize));
    m_seedQueueEnabled->setChecked(s.seedQueueEnabled);
    m_seedQueueSize->setValue(qMax(0, s.seedQueueSize));
    m_queueStalledEnabled->setChecked(s.queueStalledEnabled);
    m_queueStalledMinutes->setValue(qMax(0, s.queueStalledMinutes));
    m_ratioLimitEnabled->setChecked(s.ratioLimitEnabled);
    m_ratioLimit->setValue(qMax(0.0, s.ratioLimit));
    m_blocklistEnabled->setChecked(s.blocklistEnabled);
    m_blocklistUrl->setText(s.blocklistUrl);
    m_idleSeedingEnabled->setChecked(s.idleSeedingLimitEnabled);
    m_idleSeedingLimit->setValue(qMax(0, s.idleSeedingLimit));
    m_cacheSize->setValue(qMax(0, s.cacheSizeMb));
    updateEnabledState();
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
    d.altSpeedTimeEnabled = m_altSpeedTimeEnabled->isChecked();
    d.altSpeedTimeBegin = timeToMinutes(m_altSpeedTimeBegin->time(), /*endOfDay=*/false);
    d.altSpeedTimeEnd = timeToMinutes(m_altSpeedTimeEnd->time(), /*endOfDay=*/true);
    d.altSpeedTimeDay = 0;
    for (int i = 0; i < 7; ++i)
        if (m_days[i]->isChecked())
            d.altSpeedTimeDay |= kDayBits[i];
    d.dht = m_dht->isChecked();
    d.pex = m_pex->isChecked();
    d.lpd = m_lpd->isChecked();
    d.utp = m_utp->isChecked();
    d.peerPort = m_peerPort->value();
    d.portForwardingEnabled = m_portForwarding->isChecked();
    d.peerPortRandomOnStart = m_peerPortRandom->isChecked();
    d.encryption = m_encryption->currentData().toString();
    d.peerLimitGlobal = m_peerLimitGlobal->value();
    d.peerLimitPerTorrent = m_peerLimitPerTorrent->value();
    d.downloadDir = m_downloadDir->text().trimmed();
    d.startAddedTorrents = m_startAddedTorrents->isChecked();
    d.incompleteDir = m_incompleteDir->text().trimmed();
    d.incompleteDirEnabled = m_incompleteDirEnabled->isChecked();
    d.renamePartialFiles = m_renamePartialFiles->isChecked();
    d.downloadQueueEnabled = m_downloadQueueEnabled->isChecked();
    d.downloadQueueSize = m_downloadQueueSize->value();
    d.seedQueueEnabled = m_seedQueueEnabled->isChecked();
    d.seedQueueSize = m_seedQueueSize->value();
    d.queueStalledEnabled = m_queueStalledEnabled->isChecked();
    d.queueStalledMinutes = m_queueStalledMinutes->value();
    d.ratioLimitEnabled = m_ratioLimitEnabled->isChecked();
    d.ratioLimit = m_ratioLimit->value();
    d.blocklistEnabled = m_blocklistEnabled->isChecked();
    d.blocklistUrl = m_blocklistUrl->text().trimmed();
    d.idleSeedingLimitEnabled = m_idleSeedingEnabled->isChecked();
    d.idleSeedingLimit = m_idleSeedingLimit->value();
    d.cacheSizeMb = m_cacheSize->value();
    d.hasCacheSize = m_initial.hasCacheSize;
    d.rpcVersion = m_initial.rpcVersion;
    return d;
}
