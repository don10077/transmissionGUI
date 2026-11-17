#include "statsform.h"

#include <QFrame>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QVBoxLayout>

#include "tgui/util.h"

namespace {

// 93782 → «1d 01:16:22»; 0 → «00:00:00».
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

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_layout->addWidget(m_scroll);
}

void StatsForm::setStats(const tgui::SessionStats& stats)
{
    // При неизменной статистике не форматируем значения повторно.
    if (m_stats.has_value()) {
        const auto& a = *m_stats;
        const auto& b = stats;
        if (a.current.downloadedBytes == b.current.downloadedBytes &&
            a.current.uploadedBytes == b.current.uploadedBytes &&
            a.current.filesAdded == b.current.filesAdded &&
            a.current.secondsActive == b.current.secondsActive &&
            a.cumulative.downloadedBytes == b.cumulative.downloadedBytes &&
            a.cumulative.uploadedBytes == b.cumulative.uploadedBytes &&
            a.cumulative.filesAdded == b.cumulative.filesAdded &&
            a.cumulative.secondsActive == b.cumulative.secondsActive)
            return;
    }
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
    if (!m_table) {
        auto* content = new QWidget;
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(8, 6, 8, 6);
        contentLayout->setSpacing(8);
        m_scroll->setWidget(content);
        auto* title = new QLabel(tr("Global statistics:"), content);
        contentLayout->addWidget(title);
        const QStringList names{tr("Downloaded"), tr("Uploaded"), tr("Ratio"), tr("Files added"), tr("Active for")};
        auto* table = m_table = new QTableWidget(static_cast<int>(names.size()), 3, content);
        table->setObjectName(QStringLiteral("statisticsTable"));
        table->setHorizontalHeaderLabels({QString(), tr("Current"), tr("Cumulative")});
        for (int column = 1; column < table->columnCount(); ++column)
            table->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->verticalHeader()->hide();
        const int rowHeight = qMax(21, table->fontMetrics().height() + 5);
        table->verticalHeader()->setMinimumSectionSize(rowHeight);
        table->verticalHeader()->setDefaultSectionSize(rowHeight);
        table->setColumnWidth(0, 220);
        table->setColumnWidth(1, 150);
        table->setColumnWidth(2, 150);
        table->setMaximumWidth(520);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setStretchLastSection(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setMouseTracking(true);
        table->viewport()->setAttribute(Qt::WA_Hover);
        table->setShowGrid(false);
        table->setFrameShape(QFrame::NoFrame);
        for (int row = 0; row < table->rowCount(); ++row) {
            table->setItem(row, 0, new QTableWidgetItem(names.at(row)));
            for (int column = 1; column < table->columnCount(); ++column) {
                auto* item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                table->setItem(row, column, item);
            }
        }
        contentLayout->addWidget(table, 1);
    }

    const tgui::SessionStats stats = m_stats.value_or(tgui::SessionStats{});
    auto values = [](const tgui::StatsBlock& block) {
        const QString ratio = block.downloadedBytes > 0.0
            ? QString::fromStdString(tgui::formatRatio(block.uploadedBytes / block.downloadedBytes))
            : block.uploadedBytes > 0.0 ? QStringLiteral("∞") : QStringLiteral("—");
        return QStringList{QString::fromStdString(tgui::formatSize(block.downloadedBytes)),
                           QString::fromStdString(tgui::formatSize(block.uploadedBytes)),
                           ratio,
                           QString::number(block.filesAdded), fmtDuration(block.secondsActive)};
    };
    const auto current = values(stats.current);
    const auto cumulative = values(stats.cumulative);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        for (int column = 1; column < m_table->columnCount(); ++column) {
            auto* item = m_table->item(row, column);
            const auto text = !m_stats ? QString{} : column == 1 ? current.at(row) : cumulative.at(row);
            if (item->text() != text)
                item->setText(text);
        }
    }
}
