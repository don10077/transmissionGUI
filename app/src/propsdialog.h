#pragma once

#include <QDialog>

#include "tgui/models.h"

// Editable per-torrent limits and trackers; changes are sent only after acceptance.
// rpcVersion — для последовательной загрузки (RPC ≥ 18); 0 — неизвестно (пункт недоступен).
class PropsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PropsDialog(const tgui::Torrent& torrent, QWidget* parent = nullptr,
                         int rpcVersion = 0, int page = 0);
    QJsonObject changes() const { return m_changes; }
private:
    QJsonObject m_changes;
};
