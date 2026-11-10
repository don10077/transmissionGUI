#include "statsform.h"

#include <QFrame>
#include <QGridLayout>
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
    // Автообновление приходит раз в 2 секунды: если данные не изменились,
    // не пересобираем панель (без мерцания, сохраняем выделение текста
    // и позицию прокрутки).
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
    // Контент живёт в отдельном контейнере; пересборка = заменить widget
    // у QScrollArea (стандартный каскад деструктора QWidget).
    if (m_content) {
        m_scroll->takeWidget();
        delete m_content;
        m_content = nullptr;
    }

    m_content = new QWidget;
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(4, 4, 4, 4);
    contentLayout->setSpacing(4);
    m_scroll->setWidget(m_content);
    auto* title = new QLabel(tr("Global statistics:"), m_content);
    contentLayout->addWidget(title);
    auto* table = new QTableWidget(4, 3, m_content);
    table->setObjectName(QStringLiteral("statisticsTable"));
    table->setHorizontalHeaderLabels({QString(), tr("Current"), tr("Cumulative")});
    table->verticalHeader()->hide();
    table->verticalHeader()->setMinimumSectionSize(20);
    table->verticalHeader()->setDefaultSectionSize(20);
    table->setColumnWidth(0, 200);
    table->setColumnWidth(1, 120);
    table->setColumnWidth(2, 120);
    table->horizontalHeader()->setStretchLastSection(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setStyleSheet(QStringLiteral("QTableView { gridline-color: #D9D9D9; }"));
    const tgui::SessionStats stats = m_stats.value_or(tgui::SessionStats{});
    const QStringList names{tr("Downloaded"), tr("Uploaded"), tr("Files added"), tr("Active for")};
    auto values = [](const tgui::StatsBlock& block) {
        return QStringList{QString::fromStdString(tgui::formatSize(block.downloadedBytes)),
                           QString::fromStdString(tgui::formatSize(block.uploadedBytes)),
                           QString::number(block.filesAdded), fmtDuration(block.secondsActive)};
    };
    const auto current = values(stats.current);
    const auto cumulative = values(stats.cumulative);
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 3; ++column) {
            auto* item = new QTableWidgetItem(column == 0 ? names.at(row) :
                !m_stats ? QString() : column == 1 ? current.at(row) : cumulative.at(row));
            if (column > 0) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(row, column, item);
        }
    contentLayout->addWidget(table, 1);

}
