#pragma once

#include <QDialog>
#include <QJsonObject>

#include "tgui/models.h"

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;

// W09: параметры демона (session-set). Источник — SessionInfo; применение — RpcClient::setSession.
// Ядро (fromSession / toSessionSetJson) — чистая логика, тестируется без GUI.
struct DaemonSettings {
    // Bandwidth.
    bool speedLimitDownEnabled = false;
    int speedLimitDown = 100; // КБ/с
    bool speedLimitUpEnabled = false;
    int speedLimitUp = 100;   // КБ/с
    bool altSpeedEnabled = false;
    int altSpeedDown = 100;   // КБ/с
    int altSpeedUp = 100;     // КБ/с
    // Network.
    bool dht = true;
    bool pex = true;
    bool lpd = true;
    bool utp = true;
    int peerPort = 51413;
    // Download / queue.
    QString downloadDir;
    bool startAddedTorrents = true;
    // Other.
    bool ratioLimitEnabled = false;
    double ratioLimit = 2.0;
    bool blocklistEnabled = false;
    QString blocklistUrl;

    static DaemonSettings fromSession(const tgui::SessionInfo& s);
    // Payload для RPC-метода session-set (ключи — по документации Transmission).
    QJsonObject toSessionSetJson() const;
};

class DaemonOptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DaemonOptionsDialog(const tgui::SessionInfo& initial, QWidget* parent = nullptr);

    DaemonSettings settings() const;          // текущие значения формы
    void setSettings(const DaemonSettings& s); // заполнить форму
    QJsonObject sessionSet() const { return settings().toSessionSetJson(); }

private:
    QCheckBox* m_speedLimitDownEnabled = nullptr;
    QSpinBox* m_speedLimitDown = nullptr;
    QCheckBox* m_speedLimitUpEnabled = nullptr;
    QSpinBox* m_speedLimitUp = nullptr;
    QCheckBox* m_altSpeedEnabled = nullptr;
    QSpinBox* m_altSpeedDown = nullptr;
    QSpinBox* m_altSpeedUp = nullptr;
    QCheckBox* m_dht = nullptr;
    QCheckBox* m_pex = nullptr;
    QCheckBox* m_lpd = nullptr;
    QCheckBox* m_utp = nullptr;
    QSpinBox* m_peerPort = nullptr;
    QLineEdit* m_downloadDir = nullptr;
    QCheckBox* m_startAddedTorrents = nullptr;
    QCheckBox* m_ratioLimitEnabled = nullptr;
    QDoubleSpinBox* m_ratioLimit = nullptr;
    QCheckBox* m_blocklistEnabled = nullptr;
    QLineEdit* m_blocklistUrl = nullptr;
};