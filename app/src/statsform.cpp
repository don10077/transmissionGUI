#include "statsform.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "tgui/util.h"

namespace {

// 93782 → «1д 01:16:22»; 0 → «00:00:00».
QString fmtDuration(long long seconds)
{
    if (seconds < 0)
        seconds = 0;
    const long long days = seconds / 86400;
    const long long hours = (seconds % 86400) / 3600;
    const long long minutes = (seconds % 3600) / 60;
    const long long secs = seconds % 60;
    return QStringLiteral("%1d %2:%3:%4")
        .arg(days)
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
}

} // namespace

StatsForm::StatsForm(QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
}

void StatsForm::setStats(const tgui::SessionStats& stats)
{
    m_stats = stats;
    rebuild();
}

void StatsForm::clear()
{
    m_stats.reset();
    rebuild();
}

void StatsForm::rebuild()
{
    // Контент живёт в отдельном контейнере; пересборка = удалить контейнер
    // (стандартный каскад деструктора QWidget). См. PropsForm::rebuild —
    // ручной обход findChildren в Qt 6.12 приводит к segfault.
    if (m_content) {
        if (m_layout)
            m_layout->removeWidget(m_content);
        delete m_content;
        m_content = nullptr;
    }

    m_content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    m_layout->addWidget(m_content);

    if (!m_stats.has_value()) {
        auto* placeholder = new QLabel(tr("Statistics unavailable"), m_content);
        placeholder->setEnabled(false);
        contentLayout->addWidget(placeholder);
        return;
    }

    const auto& s = *m_stats;
    auto* grid = new QGridLayout();

    auto cell = [this, grid](int r, int c, const QString& text, bool header = false) {
        auto* label = new QLabel(text, m_content);
        if (!header)
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(label, r, c);
        return label;
    };

    cell(0, 0, tr("Metric"), true);
    cell(0, 1, tr("Current"), true);
    cell(0, 2, tr("Total"), true);

    const auto fmtSize = [](double bytes) {
        return QString::fromStdString(tgui::formatSize(bytes));
    };

    auto row = [cell](int r, const QString& name, const QString& cur, const QString& cum) {
        cell(r, 0, name);
        cell(r, 1, cur);
        cell(r, 2, cum);
    };

    row(1, tr("Downloaded"), fmtSize(s.current.downloadedBytes),
        fmtSize(s.cumulative.downloadedBytes));
    row(2, tr("Uploaded"), fmtSize(s.current.uploadedBytes),
        fmtSize(s.cumulative.uploadedBytes));
    row(3, tr("Files added"), QString::number(s.current.filesAdded),
        QString::number(s.cumulative.filesAdded));
    row(4, tr("Active for"), fmtDuration(s.current.secondsActive),
        fmtDuration(s.cumulative.secondsActive));

    grid->setColumnStretch(0, 1);
    contentLayout->addLayout(grid);
}