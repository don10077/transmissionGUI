#pragma once

#include <QWidget>

#include "tgui/models.h"

#include <optional>

class QVBoxLayout;

// Панель «Statistics»: session-stats демона (current / cumulative).
class StatsForm : public QWidget {
    Q_OBJECT
public:
    explicit StatsForm(QWidget* parent = nullptr);

    void setStats(const tgui::SessionStats& stats);
    void clear();
    const tgui::SessionStats* stats() const
    {
        return m_stats.has_value() ? &m_stats.value() : nullptr;
    }

private:
    void rebuild();

    QVBoxLayout* m_layout = nullptr;
    QWidget* m_content = nullptr;
    std::optional<tgui::SessionStats> m_stats;
};