#include "daemonoptionsdialog.h"

#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTimeEdit>
#include <QTime>
#include <QVBoxLayout>
#include <QTabWidget>

#include "tgui/rpcclient.h"

namespace {

// Битовая маска alt-speed-time-day в Transmission: Sunday=1, Monday=2, … Saturday=64.
constexpr int kDayBits[7] = {2, 4, 8, 16, 32, 64, 1}; // Mon..Sun
const char* const kDayNames[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

QSpinBox* makeKbSpin(QWidget* parent, int value)
{
    auto* s = new QSpinBox(parent);
    s->setRange(0, 1000000000);
    s->setSuffix(DaemonOptionsDialog::tr(" KB/s"));
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

QLabel* noteLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hintLabel"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
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

DaemonOptionsDialog::DaemonOptionsDialog(const tgui::SessionInfo& initial, QWidget* parent,
                                         tgui::RpcClient* rpc)
    : QDialog(parent), m_initial(DaemonSettings::fromSession(initial))
{
    // design/mockup.html, DLG.daemon: Загрузка / Сеть / Скорость / Очередь.
    setWindowTitle(tr("Transmission options"));
    resize(640, 600);
    const DaemonSettings s = m_initial;

    auto* tabs = new QTabWidget(this);

    // --- Загрузка --------------------------------------------------------
    auto* downloadPage = new QWidget(tabs);
    auto* dlForm = tgui::theme::formGrid(downloadPage);
    dlForm->setContentsMargins(14, 12, 14, 12);
    m_downloadDir = new QLineEdit(downloadPage);
    tgui::theme::addFormRow(dlForm, tr("Default download folder:"), m_downloadDir);
    m_startAddedTorrents = new QCheckBox(tr("Start added torrents"), downloadPage);
    tgui::theme::addFullRow(dlForm, m_startAddedTorrents);

    // В макете незавершённые файлы — обычные строки формы, без группы.
    m_incompleteGroup = new QWidget(downloadPage);
    m_incompleteGroup->setObjectName(QStringLiteral("incompleteGroup"));
    {
        auto* form = tgui::theme::formGrid(m_incompleteGroup);
        form->setContentsMargins(0, 0, 0, 0);
        m_incompleteDirEnabled = new QCheckBox(tr("Incomplete folder:"), m_incompleteGroup);
        m_incompleteDirEnabled->setObjectName(QStringLiteral("incompleteDirEnabled"));
        m_incompleteDir = new QLineEdit(m_incompleteGroup);
        m_incompleteDir->setObjectName(QStringLiteral("incompleteDir"));
        form->addWidget(m_incompleteDirEnabled, 0, 0);
        form->addWidget(m_incompleteDir, 0, 1);
        m_renamePartialFiles = new QCheckBox(tr("Append .part to incomplete files"), m_incompleteGroup);
        m_renamePartialFiles->setObjectName(QStringLiteral("renamePartialFiles"));
        tgui::theme::addFullRow(form, m_renamePartialFiles);
    }
    tgui::theme::addFullRow(dlForm, m_incompleteGroup);

    m_cacheGroup = new QWidget(downloadPage);
    m_cacheGroup->setObjectName(QStringLiteral("cacheGroup"));
    {
        auto* form = tgui::theme::formGrid(m_cacheGroup);
        form->setContentsMargins(0, 0, 0, 0);
        m_cacheSize = makeCountSpin(m_cacheGroup, s.cacheSizeMb, 100000);
        m_cacheSize->setObjectName(QStringLiteral("cacheSize"));
        m_cacheSize->setSuffix(tr(" MB"));
        tgui::theme::addFormRow(form, tr("Disk cache size:"), m_cacheSize);
    }
    tgui::theme::addFullRow(dlForm, m_cacheGroup);

    m_idleGroup = new QGroupBox(tr("Seeding"), downloadPage);
    m_idleGroup->setObjectName(QStringLiteral("idleGroup"));
    {
        auto* form = tgui::theme::formGrid(m_idleGroup);
        m_ratioLimitEnabled = new QCheckBox(tr("Seed ratio:"), m_idleGroup);
        m_ratioLimit = new QDoubleSpinBox(m_idleGroup);
        m_ratioLimit->setRange(0.0, 1000000.0);
        m_ratioLimit->setDecimals(2);
        form->addWidget(m_ratioLimitEnabled, 0, 0);
        form->addWidget(m_ratioLimit, 0, 1, Qt::AlignLeft);
        m_idleSeedingEnabled = new QCheckBox(tr("Stop seeding when inactive for:"), m_idleGroup);
        m_idleSeedingEnabled->setObjectName(QStringLiteral("idleSeedingEnabled"));
        m_idleSeedingLimit = makeCountSpin(m_idleGroup, s.idleSeedingLimit, 100000);
        m_idleSeedingLimit->setObjectName(QStringLiteral("idleSeedingLimit"));
        m_idleSeedingLimit->setSuffix(tr(" min"));
        form->addWidget(m_idleSeedingEnabled, 1, 0);
        form->addWidget(m_idleSeedingLimit, 1, 1, Qt::AlignLeft);
        tgui::theme::addFullRow(dlForm, m_idleGroup);
    }
    dlForm->setRowStretch(dlForm->rowCount(), 1);
    tabs->addTab(downloadPage, tr("Download"));

    // --- Сеть ------------------------------------------------------------
    auto* networkPage = new QWidget(tabs);
    auto* netForm = tgui::theme::formGrid(networkPage);
    netForm->setContentsMargins(14, 12, 14, 12);
    m_peerPort = new QSpinBox(networkPage);
    m_peerPort->setRange(1, 65535);
    m_peerPort->setObjectName(QStringLiteral("peerPort"));
    auto* btnPortTest = new QPushButton(tr("Test port"), networkPage);
    btnPortTest->setObjectName(QStringLiteral("testPort"));
    auto* portStatus = new QLabel(networkPage);
    portStatus->setObjectName(QStringLiteral("hintLabel"));
    portStatus->setTextFormat(Qt::PlainText);
    auto* portRow = new QHBoxLayout;
    portRow->setSpacing(8);
    portRow->addWidget(m_peerPort);
    portRow->addWidget(btnPortTest);
    portRow->addWidget(portStatus, 1);
    tgui::theme::addFormRow(netForm, tr("Incoming port:"), portRow);
    m_peerPortRandom = new QCheckBox(tr("Random port on start"), networkPage);
    m_peerPortRandom->setObjectName(QStringLiteral("peerPortRandom"));
    tgui::theme::addFullRow(netForm, m_peerPortRandom);
    m_portForwarding = new QCheckBox(tr("Port forwarding (UPnP / NAT-PMP)"), networkPage);
    m_portForwarding->setObjectName(QStringLiteral("portForwarding"));
    tgui::theme::addFullRow(netForm, m_portForwarding);
    m_encryption = new QComboBox(networkPage);
    m_encryption->setObjectName(QStringLiteral("encryption"));
    m_encryption->addItem(tr("Prefer encryption"), QStringLiteral("preferred"));
    m_encryption->addItem(tr("Require encryption"), QStringLiteral("required"));
    m_encryption->addItem(tr("Allow without encryption"), QStringLiteral("tolerated"));
    tgui::theme::addFormRow(netForm, tr("Encryption:"), m_encryption);
    m_peerLimitGlobal = makeCountSpin(networkPage, s.peerLimitGlobal, 100000);
    m_peerLimitGlobal->setObjectName(QStringLiteral("peerLimitGlobal"));
    tgui::theme::addFormRow(netForm, tr("Global peer limit:"), m_peerLimitGlobal);
    m_peerLimitPerTorrent = makeCountSpin(networkPage, s.peerLimitPerTorrent, 100000);
    m_peerLimitPerTorrent->setObjectName(QStringLiteral("peerLimitPerTorrent"));
    tgui::theme::addFormRow(netForm, tr("Peer limit per torrent:"), m_peerLimitPerTorrent);

    auto* discovery = new QGroupBox(tr("Peer discovery"), networkPage);
    auto* discoveryGrid = new QGridLayout(discovery);
    discoveryGrid->setHorizontalSpacing(16);
    discoveryGrid->setVerticalSpacing(8);
    m_pex = new QCheckBox(tr("Peer exchange (PEX)"), discovery);
    m_dht = new QCheckBox(tr("Distributed hash table (DHT)"), discovery);
    m_lpd = new QCheckBox(tr("Local peer discovery (LPD)"), discovery);
    m_utp = new QCheckBox(tr("µTP protocol"), discovery);
    discoveryGrid->addWidget(m_pex, 0, 0);
    discoveryGrid->addWidget(m_dht, 0, 1);
    discoveryGrid->addWidget(m_lpd, 1, 0);
    discoveryGrid->addWidget(m_utp, 1, 1);
    discoveryGrid->setColumnStretch(0, 1);
    discoveryGrid->setColumnStretch(1, 1);
    tgui::theme::addFullRow(netForm, discovery);

    m_blocklistEnabled = new QCheckBox(tr("Blocklist:"), networkPage);
    m_blocklistUrl = new QLineEdit(networkPage);
    auto* btnBlocklistUpdate = new QPushButton(tr("Update"), networkPage);
    btnBlocklistUpdate->setObjectName(QStringLiteral("updateBlocklist"));
    auto* blockRow = new QHBoxLayout;
    blockRow->setSpacing(8);
    blockRow->addWidget(m_blocklistUrl, 1);
    blockRow->addWidget(btnBlocklistUpdate);
    netForm->addWidget(m_blocklistEnabled, netForm->rowCount(), 0);
    netForm->addLayout(blockRow, netForm->rowCount() - 1, 1);
    netForm->setRowStretch(netForm->rowCount(), 1);
    tabs->addTab(networkPage, tr("Network"));

    // Проверки доступны только при живом RPC-клиенте (макет: «Тест порта» и
    // «Обновить» у списка блокировок).
    const QPointer<tgui::RpcClient> client(rpc);
    if (client) {
        connect(btnPortTest, &QPushButton::clicked, this, [client, portStatus, btnPortTest] {
            if (!client)
                return;
            portStatus->setText(tr("Checking…"));
            btnPortTest->setEnabled(false);
            client->portTest([portStatus, btnPortTest](const tgui::JsonRpcResult& result) {
                btnPortTest->setEnabled(true);
                if (!result.ok) {
                    portStatus->setText(result.error);
                    return;
                }
                portStatus->setPalette(QPalette(result.arguments.value("port-is-open").toBool()
                                                     ? tgui::theme::color::green
                                                     : tgui::theme::color::red));
                portStatus->setText(result.arguments.value("port-is-open").toBool()
                                        ? tr("The port is open.")
                                        : tr("The port is closed."));
            });
        });
        connect(btnBlocklistUpdate, &QPushButton::clicked, this, [this, client, btnBlocklistUpdate] {
            if (!client)
                return;
            btnBlocklistUpdate->setEnabled(false);
            client->updateBlocklist([this, btnBlocklistUpdate](const tgui::JsonRpcResult& result) {
                btnBlocklistUpdate->setEnabled(true);
                if (!result.ok)
                    QMessageBox::warning(this, windowTitle(), result.error);
                else
                    QMessageBox::information(this, windowTitle(), tr("The blocklist has been updated."));
            });
        });
    } else {
        btnPortTest->setEnabled(false);
        btnBlocklistUpdate->setEnabled(false);
        btnPortTest->setToolTip(tr("Connect to the daemon to test the port."));
        btnBlocklistUpdate->setToolTip(tr("Connect to the daemon to update the blocklist."));
    }

    // --- Скорость --------------------------------------------------------
    auto* speedPage = new QWidget(tabs);
    auto* speedLayout = new QVBoxLayout(speedPage);
    speedLayout->setContentsMargins(14, 12, 14, 12);
    speedLayout->setSpacing(7);

    auto* globalGroup = new QGroupBox(tr("Global limits"), speedPage);
    auto* globalForm = tgui::theme::formGrid(globalGroup);
    m_speedLimitDownEnabled = new QCheckBox(tr("Max download speed:"), globalGroup);
    m_speedLimitDown = makeKbSpin(globalGroup, s.speedLimitDown);
    globalForm->addWidget(m_speedLimitDownEnabled, 0, 0);
    globalForm->addWidget(m_speedLimitDown, 0, 1, Qt::AlignLeft);
    m_speedLimitUpEnabled = new QCheckBox(tr("Max upload speed:"), globalGroup);
    m_speedLimitUp = makeKbSpin(globalGroup, s.speedLimitUp);
    globalForm->addWidget(m_speedLimitUpEnabled, 1, 0);
    globalForm->addWidget(m_speedLimitUp, 1, 1, Qt::AlignLeft);
    speedLayout->addWidget(globalGroup);

    m_scheduleGroup = new QGroupBox(tr("Alternative limits"), speedPage);
    m_scheduleGroup->setObjectName(QStringLiteral("scheduleGroup"));
    {
        auto* form = tgui::theme::formGrid(m_scheduleGroup);
        m_altSpeedDown = makeKbSpin(m_scheduleGroup, s.altSpeedDown);
        tgui::theme::addFormRow(form, tr("Max download speed:"), m_altSpeedDown);
        m_altSpeedUp = makeKbSpin(m_scheduleGroup, s.altSpeedUp);
        tgui::theme::addFormRow(form, tr("Max upload speed:"), m_altSpeedUp);
        m_altSpeedEnabled = new QCheckBox(tr("Use alternative limits now"), m_scheduleGroup);
        tgui::theme::addFullRow(form, m_altSpeedEnabled);
        m_altSpeedTimeEnabled = new QCheckBox(tr("Enable automatically on a schedule"), m_scheduleGroup);
        tgui::theme::addFullRow(form, m_altSpeedTimeEnabled);
        m_altSpeedTimeBegin = new QTimeEdit(m_scheduleGroup);
        m_altSpeedTimeBegin->setDisplayFormat(QStringLiteral("HH:mm"));
        m_altSpeedTimeEnd = new QTimeEdit(m_scheduleGroup);
        m_altSpeedTimeEnd->setDisplayFormat(QStringLiteral("HH:mm"));
        auto* timeRow = new QHBoxLayout;
        timeRow->setSpacing(8);
        timeRow->addWidget(new QLabel(tr("from"), m_scheduleGroup));
        timeRow->addWidget(m_altSpeedTimeBegin);
        timeRow->addWidget(new QLabel(tr("to"), m_scheduleGroup));
        timeRow->addWidget(m_altSpeedTimeEnd);
        timeRow->addStretch(1);
        tgui::theme::addFormRow(form, tr("Time:"), timeRow);
        auto* daysRow = new QWidget(m_scheduleGroup);
        auto* daysColumn = new QVBoxLayout(daysRow);
        daysColumn->setContentsMargins(0, 0, 0, 0);
        daysColumn->setSpacing(4);
        auto* daysLayout = new QHBoxLayout;
        daysLayout->setContentsMargins(0, 0, 0, 0);
        daysLayout->setSpacing(2);
        for (int i = 0; i < 7; ++i) {
            m_days[i] = new QCheckBox(tr(kDayNames[i]), daysRow);
            daysLayout->addWidget(m_days[i]);
        }
        daysLayout->addStretch(1);
        daysColumn->addLayout(daysLayout);
        // Быстрый выбор периода — ссылки под флажками, как в макете.
        auto* quickRow = new QHBoxLayout;
        quickRow->setContentsMargins(0, 0, 0, 0);
        quickRow->setSpacing(10);
        const auto addQuickPick = [this, daysRow, quickRow](const QString& text, const QString& name,
                                                           int mode) {
            auto* link = new QPushButton(text, daysRow);
            link->setObjectName(name);
            link->setCursor(Qt::PointingHandCursor);
            link->setProperty("linkStyle", true);
            connect(link, &QPushButton::clicked, this, [this, mode] {
                for (int i = 0; i < 7; ++i)
                    m_days[i]->setChecked(mode == 0 || (mode == 1 ? i < 5 : i >= 5));
            });
            quickRow->addWidget(link);
        };
        addQuickPick(tr("All days"), QStringLiteral("daysAll"), 0);
        addQuickPick(tr("Weekdays"), QStringLiteral("daysWeekdays"), 1);
        addQuickPick(tr("Weekends"), QStringLiteral("daysWeekends"), 2);
        quickRow->addStretch(1);
        daysColumn->addLayout(quickRow);
        tgui::theme::addFormRow(form, tr("Days:"), daysRow);
        speedLayout->addWidget(m_scheduleGroup);
    }
    speedLayout->addStretch(1);
    tabs->addTab(speedPage, tr("Speed"));

    // --- Очередь ---------------------------------------------------------
    auto* queuePage = new QWidget(tabs);
    auto* queueLayout = new QVBoxLayout(queuePage);
    queueLayout->setContentsMargins(14, 12, 14, 12);
    m_queueGroup = new QGroupBox(tr("Queue"), queuePage);
    m_queueGroup->setObjectName(QStringLiteral("queueGroup"));
    {
        auto* form = tgui::theme::formGrid(m_queueGroup);
        m_downloadQueueEnabled = new QCheckBox(tr("Download queue size:"), m_queueGroup);
        m_downloadQueueEnabled->setObjectName(QStringLiteral("downloadQueueEnabled"));
        m_downloadQueueSize = makeCountSpin(m_queueGroup, s.downloadQueueSize, 10000);
        m_downloadQueueSize->setObjectName(QStringLiteral("downloadQueueSize"));
        form->addWidget(m_downloadQueueEnabled, 0, 0);
        form->addWidget(m_downloadQueueSize, 0, 1, Qt::AlignLeft);
        m_seedQueueEnabled = new QCheckBox(tr("Seed queue size:"), m_queueGroup);
        m_seedQueueEnabled->setObjectName(QStringLiteral("seedQueueEnabled"));
        m_seedQueueSize = makeCountSpin(m_queueGroup, s.seedQueueSize, 10000);
        m_seedQueueSize->setObjectName(QStringLiteral("seedQueueSize"));
        form->addWidget(m_seedQueueEnabled, 1, 0);
        form->addWidget(m_seedQueueSize, 1, 1, Qt::AlignLeft);
        m_queueStalledEnabled = new QCheckBox(tr("Treat as stalled when inactive for:"), m_queueGroup);
        m_queueStalledEnabled->setObjectName(QStringLiteral("queueStalledEnabled"));
        m_queueStalledMinutes = makeCountSpin(m_queueGroup, s.queueStalledMinutes, 100000);
        m_queueStalledMinutes->setObjectName(QStringLiteral("queueStalledMinutes"));
        m_queueStalledMinutes->setSuffix(tr(" min"));
        form->addWidget(m_queueStalledEnabled, 2, 0);
        form->addWidget(m_queueStalledMinutes, 2, 1, Qt::AlignLeft);
        tgui::theme::addFullRow(form, noteLabel(
            tr("Stalled torrents do not take a place in the queue."), m_queueGroup));
    }
    queueLayout->addWidget(m_queueGroup);
    queueLayout->addStretch(1);
    tabs->addTab(queuePage, tr("Queue"));

    // --- Версия демона и кнопки -----------------------------------------
    // В макете версия стоит слева в футере, а кнопки — справа.
    auto* versionLabel = noteLabel(
        tr("Transmission %1 · RPC %2").arg(initial.version, QString::number(initial.rpcVersion)),
        this);
    versionLabel->setWordWrap(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    footer->addWidget(versionLabel);
    footer->addStretch(1);
    footer->addWidget(buttons);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(8);
    root->addWidget(tabs, 1);
    root->addLayout(footer);

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
    tgui::theme::decorateDialog(this);
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
