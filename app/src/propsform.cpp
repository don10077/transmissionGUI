#include "propsform.h"

#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "theme.h"
#include "torrentmodel.h"
#include "tgui/util.h"

namespace {

QString fmtUnixTime(int unix)
{
    if (unix <= 0)
        return QStringLiteral("—");
    return QDateTime::fromSecsSinceEpoch(unix).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}

QString fmtSize(double bytes)
{
    return QString::fromStdString(tgui::formatSize(bytes));
}

QString dash()
{
    return QStringLiteral("—");
}

QColor statusColor(int status, bool finished, bool error)
{
    using namespace tgui::theme::color;
    if (error)
        return red;
    if (finished)
        return green;
    switch (status) {
        case 4:
            return accent;
        case 6:
            return green;
        case 1:
        case 3:
        case 5:
            return amber;
        case 2:
            return purple;
        default:
            return textFaint;
    }
}

// Нижняя часть шкалы General — карта кусков (референс: FTorrentProgress).
// Рисуем 1:1: серая рамка, белый фон, синие пиксели имеющихся кусков.
class PieceMapBar : public QWidget {
public:
    explicit PieceMapBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(14);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setData(double percent, const std::optional<QString>& pieces, int pieceCount)
    {
        m_percent = percent;
        m_pieces = pieces.value_or(QString());
        m_pieceCount = pieceCount;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const QRect r = rect();
        p.fillRect(r, QColor(0xA0, 0xA0, 0xA0));
        const QRect inner = r.adjusted(1, 1, -1, -1);
        p.fillRect(inner, Qt::white);
        if (inner.width() <= 0)
            return;

        if (m_pieceCount > 0 && !m_pieces.isEmpty()) {
            const QByteArray bits = QByteArray::fromBase64(m_pieces.toLatin1());
            if (!bits.isEmpty()) {
                const int w = inner.width();
                p.setPen(Qt::NoPen);
                p.setBrush(tgui::theme::color::accent);
                for (int px = 0; px < w; ++px) {
                    const int piece =
                        static_cast<int>(qint64(px) * m_pieceCount / w);
                    const int byteIndex = piece / 8;
                    if (byteIndex >= bits.size())
                        break;
                    const int bit = 7 - (piece % 8);
                    if (static_cast<unsigned char>(bits.at(byteIndex)) & (1u << bit))
                        p.drawRect(inner.left() + px, inner.top(), 1, inner.height());
                }
                return;
            }
        }

        // RPC < 5 (нет карты кусков): заливка по общему проценту.
        const int fill =
            qRound(inner.width() * qBound(0.0, m_percent, 100.0) / 100.0);
        if (fill > 0)
            p.fillRect(QRect(inner.left(), inner.top(), fill, inner.height()),
                       tgui::theme::color::accent);
    }

private:
    double m_percent = 0.0;
    QString m_pieces;
    int m_pieceCount = 0;
};

} // namespace

PropsForm::PropsForm(QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_layout->addWidget(m_scroll);
}

void PropsForm::setTorrent(const tgui::Torrent& t)
{
    m_torrent = t;
    rebuild();
}

void PropsForm::setSession(const tgui::SessionInfo& info)
{
    m_session = info;
    if (m_torrent.has_value())
        rebuild();
}

void PropsForm::clear()
{
    m_torrent.reset();
    m_session.reset();
    rebuild();
}

void PropsForm::rebuild()
{
    // Контент целиком пересоздаётся в scroll-контейнере: никакого ручного
    // обхода findChildren (см. комментарий в истории проекта).
    // Позицию прокрутки сохраняем только при обновлении того же торрента:
    // при выборе другого — показываем панель с начала.
    const int torrentId = m_torrent.has_value() ? m_torrent->id : -1;
    const bool sameTorrent = torrentId >= 0 && torrentId == m_builtTorrentId;
    if (!sameTorrent)
        m_pendingScroll = -1;
    else if (m_pendingScroll < 0)
        m_pendingScroll = m_scroll->verticalScrollBar()->value();
    const int prevMinHeight =
        (sameTorrent && m_content) ? m_content->minimumHeight() : 0;
    m_builtTorrentId = torrentId;

    if (m_content) {
        m_scroll->takeWidget();
        delete m_content;
        m_content = nullptr;
    }

    m_content = new QWidget;
    // QScrollArea::setWidget() требует, чтобы layout уже был у виджета.
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(12, 12, 12, 12);
    contentLayout->setSpacing(12);
    m_scroll->setWidget(m_content);

    if (!m_torrent.has_value()) {
        auto* placeholder = new QLabel(tr("Torrent not selected"), m_content);
        placeholder->setObjectName(QStringLiteral("fieldLabel"));
        contentLayout->addWidget(placeholder);
        contentLayout->addStretch(1);
        m_pendingScroll = -1;
        m_scroll->verticalScrollBar()->setValue(0);
        return;
    }

    const auto& t = *m_torrent;
    const double swd = t.sizeWhenDone.value_or(0.0);
    const double ltd = t.leftUntilDone.value_or(0.0);
    const double percent = tgui::percentComplete(swd, ltd);

    // «Downloaded: [======] 100.0%» (panProgress, tabGeneral).
    // Обычная строка без карточки: в эталоне это не рамка и не «кнопка».
    auto* progressCard = new QFrame(m_content);
    progressCard->setObjectName(QStringLiteral("progressRow"));
    auto* progressLayout = new QHBoxLayout(progressCard);
    progressLayout->setContentsMargins(8, 6, 8, 6);
    progressLayout->setSpacing(10);
    auto* downloadedLabel = new QLabel(tr("Downloaded:"), progressCard);
    downloadedLabel->setObjectName(QStringLiteral("fieldLabel"));
    progressLayout->addWidget(downloadedLabel);
    auto* bars = new QWidget(progressCard);
    auto* barsLayout = new QVBoxLayout(bars);
    barsLayout->setContentsMargins(0, 0, 0, 0);
    barsLayout->setSpacing(1);

    // Верхняя тонкая полоса — общий процент загрузки.
    auto* overallBar = new QProgressBar(bars);
    overallBar->setObjectName(QStringLiteral("progressTop"));
    overallBar->setRange(0, 1000);
    overallBar->setValue(qRound(percent * 10.0));
    overallBar->setFormat(QStringLiteral("%1%").arg(percent, 0, 'f', 1));
    overallBar->setTextVisible(false);
    barsLayout->addWidget(overallBar);

    // Нижняя толстая полоса — карта кусков (FTorrentProgress в эталоне).
    auto* pieceBar = new PieceMapBar(bars);
    pieceBar->setData(percent, t.pieces, t.pieceCount.value_or(0));
    barsLayout->addWidget(pieceBar);

    progressLayout->addWidget(bars, 1);
    // Процент — отдельной меткой справа от шкалы, как в эталоне.
    auto* percentLabel = new QLabel(
        QStringLiteral("%1%").arg(percent, 0, 'f', 1), progressCard);
    percentLabel->setObjectName(QStringLiteral("fieldValue"));
    progressLayout->addWidget(percentLabel);
    contentLayout->addWidget(progressCard);

    auto makeCard = [this, contentLayout](const QString& title, int pairs = 3) {
        auto* card = new QFrame(m_content);
        card->setObjectName(QStringLiteral("infoCard"));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        // Заголовок секции — серая полоса на всю ширину (как в эталоне).
        auto* header = new QLabel(title, card);
        header->setObjectName(QStringLiteral("infoCardTitle"));
        cardLayout->addWidget(header);

        auto* body = new QWidget(card);
        auto* grid = new QGridLayout(body);
        grid->setContentsMargins(12, 9, 12, 11);
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(6);
        // Тянутся только колонки значений реально используемых пар, иначе
        // пустая колонка забирает ширину и текст уезжает за край.
        for (int i = 1; i < pairs * 2; i += 2)
            grid->setColumnStretch(i, 1);
        cardLayout->addWidget(body);

        contentLayout->addWidget(card);
        return qMakePair(card, grid);
    };

    auto makeCell = [this](QGridLayout* grid, int row, int col, const QString& label,
                           const QString& value, bool rich = false,
                           const QColor& valueColor = QColor()) -> QLabel* {
        auto* l = new QLabel(label + ":", m_content);
        l->setObjectName(QStringLiteral("fieldLabel"));
        grid->addWidget(l, row, col);

        auto* v = new QLabel(value, m_content);
        v->setObjectName(QStringLiteral("fieldValue"));
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        v->setTextFormat(rich ? Qt::RichText : Qt::PlainText);
        // Ignored по горизонтали: длинные неразрывные значения (magnet, hash)
        // не должны распирать карточку шире панели.
        v->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        v->setMinimumWidth(0);
        if (valueColor.isValid())
            v->setStyleSheet(QStringLiteral("color: %1;").arg(valueColor.name()));
        grid->addWidget(v, row, col + 1);
        return v;
    };

    // Секция «Transfer» (panTransfer) — три пары label/value на строку.
    auto [transferCard, grid] = makeCard(tr("Transfer"));

    const bool finished = tgui::statusIsFinished(
        t.status.value_or(-1), t.sizeWhenDone.value_or(0.0), t.leftUntilDone.value_or(0.0));
    const bool errored =
        t.errorString.has_value() && !t.errorString->isEmpty() && t.status.value_or(-1) != 0;
    const QString error = (t.errorString.has_value() && !t.errorString->isEmpty())
                              ? *t.errorString
                              : dash();
    const int status = t.status.value_or(-1);
    const QColor statusTint = statusColor(status, finished, errored);
    const QString statusRich =
        QStringLiteral("<span style=\"color:%1; font-size:12pt;\">●</span>&nbsp;%2")
            .arg(statusTint.name(),
                 TorrentModel::statusText(status, finished));

    Q_UNUSED(transferCard);
    makeCell(grid, 0, 0, tr("Status"), statusRich, true);
    makeCell(grid, 0, 2, tr("Error"), error, false,
             error == dash() ? QColor() : tgui::theme::color::red);
    makeCell(grid, 0, 4, tr("Left"), fmtSize(ltd));

    makeCell(grid, 1, 0, tr("Downloaded"),
             t.downloadedEver ? fmtSize(*t.downloadedEver) : dash());
    makeCell(grid, 1, 2, tr("Uploaded"), t.uploadedEver ? fmtSize(*t.uploadedEver) : dash());
    const int seeders = (!t.trackerStats.isEmpty() && t.trackerStats.first().seederCount >= 0)
                            ? t.trackerStats.first().seederCount
                            : -1;
    makeCell(grid, 1, 4, tr("Loaded peers"),
             QStringLiteral("%1 of %2").arg(t.peersSendingToUs.value_or(0)).arg(seeders));

    auto speedText = [](int rate, std::optional<double> total, std::optional<int> seconds) {
        QString s = QString::fromStdString(tgui::formatRate(rate));
        if (total && seconds && seconds.value() > 0 && total.value() > 0)
            s += QStringLiteral(" (%1: %2)")
                     .arg(QObject::tr("average"))
                     .arg(QString::fromStdString(
                         tgui::formatRate(static_cast<qint64>(total.value() / seconds.value()))));
        return s;
    };
    makeCell(grid, 2, 0, tr("Speed"), speedText(t.rateDownload.value_or(0), t.downloadedEver,
                                                t.secondsDownloading));
    makeCell(grid, 2, 2, tr("Up speed"), speedText(t.rateUpload.value_or(0), t.uploadedEver,
                                                   t.secondsSeeding));
    makeCell(grid, 2, 4, tr("Ratio"),
             t.uploadRatio ? QString::fromStdString(tgui::formatRatio(*t.uploadRatio)) : dash());

    auto limitText = [this](bool enabled, int limitKb) {
        if (!m_session.has_value())
            return dash();
        if (!enabled || limitKb < 0)
            return tr("Unlimited");
        return fmtSize(limitKb * 1024.0) + "/s";
    };
    makeCell(grid, 3, 0, tr("Down limit"),
             limitText(m_session ? m_session->speedLimitDownEnabled : false,
                       m_session ? m_session->speedLimitDown : 0));
    makeCell(grid, 3, 2, tr("Up limit"),
             limitText(m_session ? m_session->speedLimitUpEnabled : false,
                       m_session ? m_session->speedLimitUp : 0));
    makeCell(grid, 3, 4, tr("Max peers"),
             t.maxConnectedPeers ? QString::number(*t.maxConnectedPeers) : dash());

    makeCell(grid, 4, 0, tr("Seeds"),
             QStringLiteral("%1 %2").arg(t.peersSendingToUs.value_or(0))
                 .arg(QObject::tr("connected")));
    makeCell(grid, 4, 2, tr("Peers"),
             QStringLiteral("%1 %2").arg(t.peersGettingFromUs.value_or(0))
                 .arg(QObject::tr("in swarm")));

    QString trackerUpdate = dash();
    if (!t.trackerStats.isEmpty() &&
        (t.trackerStats.first().announceState == 2 || t.trackerStats.first().announceState == 3))
        trackerUpdate = tr("Updating...");
    makeCell(grid, 5, 0, tr("Tracker"),
             t.trackerStats.isEmpty() ? tr("No tracker") : t.trackerStats.first().announce);
    makeCell(grid, 5, 2, tr("Tracker update"), trackerUpdate);
    makeCell(grid, 6, 0, tr("Last active"), fmtUnixTime(t.activityDate.value_or(0)));

    // Секция «Torrent» (panGeneralInfo) — две пары label/value на строку.
    auto [torrentCard, tgrid] = makeCard(tr("Torrent"), 2);

    QString name = t.name.value_or(dash());
    if (t.downloadDir.has_value() && !t.downloadDir->isEmpty())
        name = *t.downloadDir + QStringLiteral("/") + name;
    makeCell(tgrid, 0, 0, tr("Name"), name);

    QString created = fmtUnixTime(t.dateCreated.value_or(0));
    if (t.creator.has_value() && !t.creator->isEmpty())
        created += " by " + *t.creator;
    makeCell(tgrid, 0, 2, tr("Created"), created);

    const QString totalSizeText = (swd > 0.0)
                                      ? QStringLiteral("%1 (%2 %3)")
                                            .arg(fmtSize(t.totalSize.value_or(swd)))
                                            .arg(fmtSize(swd - ltd))
                                            .arg(QObject::tr("done"))
                                      : dash();
    makeCell(tgrid, 1, 0, tr("Total size"), totalSizeText);

    QString pieces = dash();
    if (t.pieceCount.has_value()) {
        int have = -1;
        if (t.haveValid.has_value() && t.pieceSize.value_or(0.0) > 0.0)
            have = (t.haveValid.value() >= swd - ltd)
                       ? t.pieceCount.value()
                       : static_cast<int>(t.haveValid.value() / t.pieceSize.value());
        pieces = QStringLiteral("%1 x %2 (%3 %4)")
                     .arg(t.pieceCount.value())
                     .arg(fmtSize(t.pieceSize.value_or(0.0)))
                     .arg(QObject::tr("have"))
                     .arg(have >= 0 ? QString::number(have) : dash());
    }
    makeCell(tgrid, 1, 2, tr("Pieces"), pieces);

    makeCell(tgrid, 2, 0, tr("Hash"), t.hashString.value_or(dash()));
    makeCell(tgrid, 2, 2, tr("Comment"), t.comment.value_or(dash()));
    makeCell(tgrid, 3, 0, tr("Added"), fmtUnixTime(t.addedDate.value_or(0)));
    makeCell(tgrid, 3, 2, tr("Completed"), fmtUnixTime(t.doneDate.value_or(0)));
    makeCell(tgrid, 4, 0, tr("Magnet link"), t.magnetLink.value_or(dash()));

    Q_UNUSED(torrentCard);
    contentLayout->addStretch(1);

    if (prevMinHeight > 0)
        m_content->setMinimumHeight(prevMinHeight);

    // Не даём QScrollArea сжимать карточки: при нехватке высоты панель
    // должна прокручиваться, а не сплющивать содержимое. sizeHint корректен
    // только после первой раскладки, поэтому фиксируем высоту следующим тиком.
    QTimer::singleShot(0, this, [this] {
        if (!m_content)
            return;
        m_content->setMinimumHeight(m_content->sizeHint().height());

        // Возвращаем прокрутку после пересборки (автообновление раз в 2 с).
        if (m_pendingScroll > 0) {
            QTimer::singleShot(0, this, [this] {
                if (m_pendingScroll > 0) {
                    m_scroll->verticalScrollBar()->setValue(m_pendingScroll);
                    m_pendingScroll = -1;
                }
            });
        }
    });
}
