#include "statsform.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
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
    m_scroll->setWidget(m_content);
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->setSpacing(12);

    if (!m_stats.has_value()) {
        auto* placeholder = new QLabel(tr("Statistics unavailable"), m_content);
        placeholder->setObjectName(QStringLiteral("fieldLabel"));
        contentLayout->addWidget(placeholder);
        contentLayout->addStretch(1);
        return;
    }

    auto* card = new QFrame(m_content);
    card->setObjectName(QStringLiteral("statsCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto* title = new QLabel(tr("Session"), card);
    title->setObjectName(QStringLiteral("infoCardTitle"));
    cardLayout->addWidget(title);

    auto* body = new QWidget(card);
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(12, 8, 12, 12);
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(0, 1);
    cardLayout->addWidget(body);

    const auto& s = *m_stats;

    auto cell = [this, grid](int r, int c, const QString& text, bool header = false,
                             Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
        auto* label = new QLabel(text, m_content);
        if (header)
            label->setObjectName(QStringLiteral("statsHeader"));
        else
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setAlignment(align);
        grid->addWidget(label, r, c);
        return label;
    };

    cell(0, 0, tr("Metric"), true);
    cell(0, 1, tr("Current"), true, Qt::AlignRight | Qt::AlignVCenter);
    cell(0, 2, tr("Total"), true, Qt::AlignRight | Qt::AlignVCenter);

    const auto fmtSize = [](double bytes) {
        return QString::fromStdString(tgui::formatSize(bytes));
    };

    auto row = [cell](int r, const QString& name, const QString& cur, const QString& cum) {
        cell(r, 0, name);
        cell(r, 1, cur, false, Qt::AlignRight | Qt::AlignVCenter);
        cell(r, 2, cum, false, Qt::AlignRight | Qt::AlignVCenter);
    };

    row(1, tr("Downloaded"), fmtSize(s.current.downloadedBytes),
        fmtSize(s.cumulative.downloadedBytes));
    row(2, tr("Uploaded"), fmtSize(s.current.uploadedBytes),
        fmtSize(s.cumulative.uploadedBytes));
    row(3, tr("Files added"), QString::number(s.current.filesAdded),
        QString::number(s.cumulative.filesAdded));
    row(4, tr("Active for"), fmtDuration(s.current.secondsActive),
        fmtDuration(s.cumulative.secondsActive));

    contentLayout->addWidget(card);
    contentLayout->addStretch(1);
}
