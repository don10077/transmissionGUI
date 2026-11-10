#pragma once

#include <QDialog>

#include "tgui/models.h"

// Editable per-torrent limits and trackers; changes are sent only after acceptance.
class PropsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PropsDialog(const tgui::Torrent& torrent, QWidget* parent = nullptr);
    QJsonObject changes() const { return m_changes; }
private:
    QJsonObject m_changes;
};