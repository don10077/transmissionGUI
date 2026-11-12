#pragma once

#include <QDialog>
#include <QJsonObject>

#include "tgui/models.h"

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QTimeEdit;
class QGroupBox;

// W09: параметры демона (session-set). Источник — SessionInfo; применение — RpcClient::setSession.
// Ядро (fromSession / toSessionSetJson / changesFrom) — чистая логика, тестируется без GUI.
struct DaemonSettings {
    // Bandwidth.
    bool speedLimitDownEnabled = false;
    int speedLimitDown = 100; // КБ/с
    bool speedLimitUpEnabled = false;
    int speedLimitUp = 100;   // КБ/с
    bool altSpeedEnabled = false;
    int altSpeedDown = 100;   // КБ/с
    int altSpeedUp = 100;     // КБ/с
    // Расписание альтернативных скоростей (RPC >= 5).
    bool altSpeedTimeEnabled = false;
    int altSpeedTimeBegin = 0; // минуты после полуночи
    int altSpeedTimeEnd = 0;   // минуты после полуночи (1440 — полночь)
    int altSpeedTimeDay = 0;   // битовая маска: Sunday=1, Monday=2, … Saturday=64
    // Network.
    bool dht = true;
    bool pex = true;
    bool lpd = true;
    bool utp = true;
    int peerPort = 51413;
    bool portForwardingEnabled = false;
    bool peerPortRandomOnStart = false;
    int peerPortRandomLow = 0;
    int peerPortRandomHigh = 0;
    QString encryption = QStringLiteral("preferred"); // required | preferred | tolerated
    int peerLimitGlobal = 0;
    int peerLimitPerTorrent = 0;
    // Download / queue.
    QString downloadDir;
    bool startAddedTorrents = true;
    QString incompleteDir;
    bool incompleteDirEnabled = false;
    bool renamePartialFiles = false;
    bool downloadQueueEnabled = false;
    int downloadQueueSize = 0;
    bool seedQueueEnabled = false;
    int seedQueueSize = 0;
    bool queueStalledEnabled = false;
    int queueStalledMinutes = 0;
    // Other.
    bool ratioLimitEnabled = false;
    double ratioLimit = 2.0;
    bool blocklistEnabled = false;
    QString blocklistUrl;
    bool idleSeedingLimitEnabled = false;
    int idleSeedingLimit = 0; // минуты
    int cacheSizeMb = 0;
    bool hasCacheSize = false; // демон вернул cache-size-mb
    int rpcVersion = 0;        // для версионных гейтов формы

    static DaemonSettings fromSession(const tgui::SessionInfo& s);
    // Payload для RPC-метода session-set (ключи — по документации Transmission).
    QJsonObject toSessionSetJson() const;
    // Только поля, отличающиеся от base (не перезаписываем чужие изменения).
    QJsonObject changesFrom(const DaemonSettings& base) const;
};

class DaemonOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DaemonOptionsDialog(const tgui::SessionInfo& initial, QWidget* parent = nullptr);

    DaemonSettings settings() const;          // текущие значения формы
    void setSettings(const DaemonSettings& s); // заполнить форму
    QJsonObject sessionSet() const { return settings().toSessionSetJson(); }
    QJsonObject changedSessionSet() const { return settings().changesFrom(m_initial); }

private:
    void applyFeatureGates();
    void updateEnabledState();

    DaemonSettings m_initial;

    QCheckBox* m_speedLimitDownEnabled = nullptr;
    QSpinBox* m_speedLimitDown = nullptr;
    QCheckBox* m_speedLimitUpEnabled = nullptr;
    QSpinBox* m_speedLimitUp = nullptr;
    QCheckBox* m_altSpeedEnabled = nullptr;
    QSpinBox* m_altSpeedDown = nullptr;
    QSpinBox* m_altSpeedUp = nullptr;
    QGroupBox* m_scheduleGroup = nullptr;
    QCheckBox* m_altSpeedTimeEnabled = nullptr;
    QTimeEdit* m_altSpeedTimeBegin = nullptr;
    QTimeEdit* m_altSpeedTimeEnd = nullptr;
    QCheckBox* m_days[7] = {};
    QCheckBox* m_dht = nullptr;
    QCheckBox* m_pex = nullptr;
    QCheckBox* m_lpd = nullptr;
    QCheckBox* m_utp = nullptr;
    QComboBox* m_encryption = nullptr;
    QSpinBox* m_peerLimitGlobal = nullptr;
    QSpinBox* m_peerLimitPerTorrent = nullptr;
    QSpinBox* m_peerPort = nullptr;
    QCheckBox* m_portForwarding = nullptr;
    QCheckBox* m_peerPortRandom = nullptr;
    QSpinBox* m_peerPortRandomLow = nullptr;
    QSpinBox* m_peerPortRandomHigh = nullptr;
    QLineEdit* m_downloadDir = nullptr;
    QCheckBox* m_startAddedTorrents = nullptr;
    QGroupBox* m_incompleteGroup = nullptr;
    QCheckBox* m_incompleteDirEnabled = nullptr;
    QLineEdit* m_incompleteDir = nullptr;
    QCheckBox* m_renamePartialFiles = nullptr;
    QGroupBox* m_queueGroup = nullptr;
    QCheckBox* m_downloadQueueEnabled = nullptr;
    QSpinBox* m_downloadQueueSize = nullptr;
    QCheckBox* m_seedQueueEnabled = nullptr;
    QSpinBox* m_seedQueueSize = nullptr;
    QCheckBox* m_queueStalledEnabled = nullptr;
    QSpinBox* m_queueStalledMinutes = nullptr;
    QCheckBox* m_ratioLimitEnabled = nullptr;
    QDoubleSpinBox* m_ratioLimit = nullptr;
    QCheckBox* m_blocklistEnabled = nullptr;
    QLineEdit* m_blocklistUrl = nullptr;
    QGroupBox* m_idleGroup = nullptr;
    QCheckBox* m_idleSeedingEnabled = nullptr;
    QSpinBox* m_idleSeedingLimit = nullptr;
    QGroupBox* m_cacheGroup = nullptr;
    QSpinBox* m_cacheSize = nullptr;
};
