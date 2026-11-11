#include "propsform.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDateTime>
#include <QUrl>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

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

/** Attach whole-value copying to either the caption or its value. */
void addCopyAction(QWidget* widget, const QString& value, const QString& caption)
{
    auto* copy = new QAction(caption, widget);
    copy->setObjectName(QStringLiteral("copyFieldValue"));
    QObject::connect(copy, &QAction::triggered, widget, [value] {
        QApplication::clipboard()->setText(value);
    });
    widget->addAction(copy);
    widget->setContextMenuPolicy(Qt::ActionsContextMenu);
}

/** Return a supported external link, or an empty URL for ordinary text. */
QUrl fieldLink(const QString& value)
{
    const QUrl url(value.trimmed(), QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (url.isValid() && ((scheme == QStringLiteral("http") ||
                           scheme == QStringLiteral("https")) && !url.host().isEmpty()))
        return url;
    return {};
}

// Нижняя часть шкалы General — карта кусков (референс: FTorrentProgress).
// Рисуем 1:1: серая рамка, белый фон, синие пиксели имеющихся кусков.
class PieceMapBar : public QWidget {
public:
    explicit PieceMapBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(21);
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
        const QColor highlight = palette().color(QPalette::Highlight);
        const QColor background = palette().color(QPalette::Base);
        const QColor border = palette().color(QPalette::Shadow);
        p.fillRect(rect(), background);
        // ProcessPieces: six-pixel overall bar, a gap, then the piece map.
        const bool hasMap = m_pieceCount > 0;
        const QRect overall(0, 0, width(), hasMap ? 6 : height());
        p.fillRect(overall, border);
        const QRect fill = overall.adjusted(1, 1, -1, -1);
        p.fillRect(fill, background);
        p.fillRect(QRect(fill.topLeft(), QSize(qRound(fill.width() *
                       qBound(0.0, m_percent, 100.0) / 100.0), fill.height())), highlight);
        if (!hasMap)
            return;
        const QRect map(0, 7, width(), 14);
        p.fillRect(map, border);
        const QRect inner = map.adjusted(1, 1, -1, -1);
        p.fillRect(inner, background);
        const QByteArray bits = QByteArray::fromBase64(m_pieces.toLatin1());
        // Preserve every present piece when reducing maps above 4000 pieces,
        // matching the reference bitmap before stretching it to the panel.
        const int samples = qMin(m_pieceCount, 4000);
        QImage bitmap(samples, 1, QImage::Format_RGB32);
        bitmap.fill(background);
        for (int piece = 0; piece < m_pieceCount && piece / 8 < bits.size(); ++piece)
            if (static_cast<unsigned char>(bits.at(piece / 8)) & (0x80u >> (piece % 8)))
                bitmap.setPixelColor(int(qint64(piece) * samples / m_pieceCount), 0, highlight);
        p.drawImage(inner, bitmap);

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
    m_layout->setSpacing(0);

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
    const int rebuildSerial = ++m_rebuildSerial;
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
    m_builtTorrentId = torrentId;

    delete m_progressRow;
    m_progressRow = nullptr;

    if (m_content) {
        m_scroll->takeWidget();
        delete m_content;
        m_content = nullptr;
    }

    m_content = new QWidget;
    // QScrollArea::setWidget() требует, чтобы layout уже был у виджета.
    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    // Let the scroll area use the content's real minimum height, including
    // labels whose height changes when the panel is resized.
    contentLayout->setSizeConstraint(QLayout::SetMinimumSize);

    if (!m_torrent.has_value()) {
        auto* placeholder = new QLabel(tr("Torrent not selected"), m_content);
        placeholder->setObjectName(QStringLiteral("fieldLabel"));
        contentLayout->addWidget(placeholder);
        contentLayout->addStretch(1);
        m_scroll->setWidget(m_content);
        m_pendingScroll = -1;
        m_scroll->verticalScrollBar()->setValue(0);
        return;
    }

    const auto& t = *m_torrent;
    const double swd = t.sizeWhenDone.value_or(0.0);
    const double ltd = t.leftUntilDone.value_or(0.0);
    const double percent = tgui::percentComplete(swd, ltd);

    // «Downloaded: [======] 100.0%» (panProgress, tabGeneral).
    // Одна шкала — карта кусков, как pbDownloaded/FTorrentProgress в старом transgui.
    auto* progressCard = new QFrame(this);
    m_progressRow = progressCard;
    progressCard->setObjectName(QStringLiteral("progressRow"));
    auto* progressLayout = new QHBoxLayout(progressCard);
    progressLayout->setContentsMargins(8, 6, 8, 6);
    progressLayout->setSpacing(10);
    auto* downloadedLabel = new QLabel(tr("Downloaded:"), progressCard);
    downloadedLabel->setObjectName(QStringLiteral("fieldLabel"));
    progressLayout->addWidget(downloadedLabel);
    auto* pieceBar = new PieceMapBar(progressCard);
    pieceBar->setData(percent, t.pieces, t.pieceCount.value_or(0));
    progressLayout->addWidget(pieceBar, 1);
    // Процент — отдельной меткой справа от шкалы, как в эталоне.
    auto* percentLabel = new QLabel(
        QStringLiteral("%1%").arg(percent, 0, 'f', 1), progressCard);
    percentLabel->setObjectName(QStringLiteral("fieldValue"));
    progressLayout->addWidget(percentLabel);
    m_layout->insertWidget(0, progressCard);

    auto makeCard = [this, contentLayout](const QString& title, int pairs = 3) {
        auto* card = new QFrame(m_content);
        // Без рамки/подложки: в старом transgui секции — плоский блок
        // (panel BevelOuter = bvNone), а не «карточка».
        card->setObjectName(QStringLiteral("sectionPanel"));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        // Заголовок секции — просто жирный текст
        // (txTransferHeader/txTorrentHeader в старом transgui).
        auto* header = new QLabel(title, card);
        header->setObjectName(QStringLiteral("sectionTitle"));
        cardLayout->addWidget(header);

        auto* body = new QWidget(card);
        auto* grid = new QGridLayout(body);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(4);
        // Тянутся только колонки значений реально используемых пар, иначе
        // пустая колонка забирает ширину и текст уезжает за край.
        for (int i = 1; i < pairs * 2; i += 2)
            grid->setColumnStretch(i, 1);
        cardLayout->addWidget(body);

        contentLayout->addWidget(card);
        return qMakePair(card, grid);
    };

    auto makeCell = [this](QGridLayout* grid, int row, int col, const QString& label,
                           const QString& value,
                           const QColor& valueColor = QColor()) -> QLabel* {
        auto* l = new QLabel(label + ":", m_content);
        l->setObjectName(QStringLiteral("fieldLabel"));
        addCopyAction(l, value, tr("Copy"));
        grid->addWidget(l, row, col);

        auto* v = new QLabel(value, m_content);
        v->setObjectName(QStringLiteral("fieldValue"));
        v->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        v->setWordWrap(false);
        v->setToolTip(value);
        v->setTextFormat(Qt::PlainText);
        addCopyAction(v, value, tr("Copy"));
        const QUrl link = fieldLink(value);
        if (!link.isEmpty()) {
            v->setTextFormat(Qt::RichText);
            v->setText(QStringLiteral("<a href=\"%1\">%2</a>")
                           .arg(link.toString(QUrl::FullyEncoded).toHtmlEscaped(),
                                value.toHtmlEscaped()));
            v->setTextInteractionFlags(v->textInteractionFlags() |
                                      Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
            v->setOpenExternalLinks(true);
        }
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
    const QString effectiveError = tgui::torrentError(t);
    const QString error = effectiveError.isEmpty() ? dash() : effectiveError;
    Q_UNUSED(transferCard);
    makeCell(grid, 0, 0, tr("Status"),
             TorrentModel::statusText(t.status.value_or(-1), finished && effectiveError.isEmpty()));
    makeCell(grid, 0, 2, tr("Error"), error,
             error == dash() ? QColor() : tgui::theme::color::red);
    QString remaining = fmtSize(ltd);
    if (t.eta.has_value() && t.eta.value() >= 0) {
        const QString eta = QString::fromStdString(tgui::formatEta(t.eta.value()));
        if (!eta.isEmpty())
            remaining = QStringLiteral("%1 (%2)").arg(eta, fmtSize(ltd));
    }
    makeCell(grid, 0, 4, tr("Remaining"), remaining);

    makeCell(grid, 1, 0, tr("Downloaded"),
             t.downloadedEver ? fmtSize(*t.downloadedEver) : dash());
    makeCell(grid, 1, 2, tr("Uploaded"), t.uploadedEver ? fmtSize(*t.uploadedEver) : dash());
    QString wasted = dash();
    if (t.corruptEver.has_value()) {
        int hashfails = 0;
        if (t.pieceSize.value_or(0.0) > 0.0)
            hashfails = static_cast<int>(std::llround(*t.corruptEver / *t.pieceSize));
        wasted = QStringLiteral("%1 (%2 %3)")
                     .arg(fmtSize(*t.corruptEver))
                     .arg(hashfails)
                     .arg(QObject::tr("hashfails"));
    }
    makeCell(grid, 1, 4, tr("Wasted"), wasted);

    auto speedText = [](int rate, std::optional<double> total, std::optional<int> seconds) {
        QString s = QString::fromStdString(tgui::formatRate(rate));
        if (total && seconds && seconds.value() > 0 && total.value() > 0)
            s += QStringLiteral(" (%1: %2)")
                     .arg(QObject::tr("average"))
                     .arg(QString::fromStdString(
                         tgui::formatRate(static_cast<qint64>(total.value() / seconds.value()))));
        return s;
    };
    makeCell(grid, 2, 0, tr("Download speed"),
             speedText(t.rateDownload.value_or(0), t.downloadedEver, t.secondsDownloading));
    makeCell(grid, 2, 2, tr("Upload speed"),
             speedText(t.rateUpload.value_or(0), t.uploadedEver, t.secondsSeeding));
    QString ratio = t.uploadRatio ? QString::fromStdString(tgui::formatRatio(*t.uploadRatio))
                                  : dash();
    if (t.secondsSeeding.has_value() && t.secondsSeeding.value() > 0)
        ratio += QStringLiteral(" (%1)")
                     .arg(QString::fromStdString(tgui::formatEta(*t.secondsSeeding)));
    makeCell(grid, 2, 4, tr("Share ratio"), ratio);

    auto limitText = [this](bool enabled, int limitKb) {
        if (!m_session.has_value())
            return QStringLiteral("—");
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

    const int seeders = (!t.trackerStats.isEmpty() && t.trackerStats.first().seederCount >= 0)
                            ? t.trackerStats.first().seederCount
                            : -1;
    const int leechers = (!t.trackerStats.isEmpty() && t.trackerStats.first().leecherCount >= 0)
                             ? t.trackerStats.first().leecherCount
                             : -1;
    QString seedsText = QStringLiteral("%1 %2").arg(t.peersSendingToUs.value_or(0))
                            .arg(QObject::tr("connected"));
    if (seeders >= 0)
        seedsText = QStringLiteral("%1 %2 %3 %4")
                        .arg(t.peersSendingToUs.value_or(0))
                        .arg(QObject::tr("of"))
                        .arg(seeders)
                        .arg(QObject::tr("connected"));
    makeCell(grid, 4, 0, tr("Seeds"), seedsText);
    QString peersText = QStringLiteral("%1 %2").arg(t.peersGettingFromUs.value_or(0))
                            .arg(QObject::tr("connected"));
    if (leechers >= 0)
        peersText = QStringLiteral("%1 %2 %3 %4")
                        .arg(t.peersGettingFromUs.value_or(0))
                        .arg(QObject::tr("of"))
                        .arg(leechers)
                        .arg(QObject::tr("in swarm"));
    makeCell(grid, 4, 2, tr("Peers"), peersText);
    makeCell(grid, 4, 4, tr("Max peers"),
             t.maxConnectedPeers ? QString::number(*t.maxConnectedPeers) : dash());

    QString trackerUpdate = dash();
    if (!t.trackerStats.isEmpty()) {
        const auto& st = t.trackerStats.first();
        if (st.announceState == 2 || st.announceState == 3)
            trackerUpdate = tr("Updating...");
        else if (st.nextAnnounceTime > 0)
            trackerUpdate = fmtUnixTime(st.nextAnnounceTime);
    }
    makeCell(grid, 5, 0, tr("Tracker"),
             t.trackerStats.isEmpty() ? tr("No tracker") : t.trackerStats.first().announce);
    makeCell(grid, 5, 2, tr("Tracker update on"), trackerUpdate);
    makeCell(grid, 5, 4, tr("Last active"), fmtUnixTime(t.activityDate.value_or(0)));

    // Секция «Torrent» (panGeneralInfo) — две пары label/value на строку.
    auto [torrentCard, tgrid] = makeCard(tr("Torrent"), 2);

    QString name = t.name.value_or(dash());
    if (t.downloadDir.has_value() && !t.downloadDir->isEmpty())
        name = *t.downloadDir + QStringLiteral("/") + name;
    makeCell(tgrid, 0, 0, tr("Name"), name);

    QString created = fmtUnixTime(t.dateCreated.value_or(0));
    if (t.creator.has_value() && !t.creator->isEmpty())
        created += " by " + *t.creator;
    makeCell(tgrid, 0, 2, tr("Created on"), created);

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
    makeCell(tgrid, 3, 0, tr("Added on"), fmtUnixTime(t.addedDate.value_or(0)));
    makeCell(tgrid, 3, 2, tr("Completed on"), fmtUnixTime(t.doneDate.value_or(0)));
    auto* magnetLabel = new QLabel(tr("Magnet Link") + ":", m_content);
    magnetLabel->setObjectName(QStringLiteral("fieldLabel"));
    addCopyAction(magnetLabel, t.magnetLink.value_or(dash()), tr("Copy"));
    tgrid->addWidget(magnetLabel, 4, 0);
    // Like txMagnetLink: a single read-only line, selectable without wrapping.
    auto* magnet = new QLineEdit(t.magnetLink.value_or(dash()), m_content);
    magnet->setObjectName(QStringLiteral("magnetLink"));
    magnet->setReadOnly(true);
    magnet->setFrame(false);
    magnet->setMinimumWidth(0);
    magnet->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    magnet->setToolTip(magnet->text());
    magnet->setStyleSheet(QStringLiteral("border: none; padding: 0; background: transparent;"));
    magnet->setCursorPosition(0);
    // Copy the entire link even when it is clipped or partially selected.
    auto* copyMagnet = new QAction(tr("Copy"), magnet);
    copyMagnet->setObjectName(QStringLiteral("copyFieldValue"));
    connect(copyMagnet, &QAction::triggered, magnet, [magnet] {
        QApplication::clipboard()->setText(magnet->text());
    });
    magnet->addAction(copyMagnet);
    magnet->setContextMenuPolicy(Qt::ActionsContextMenu);
    const QUrl magnetUrl(magnet->text(), QUrl::StrictMode);
    if (magnetUrl.isValid() && magnetUrl.scheme() == QStringLiteral("magnet")) {
        auto* open = new QAction(tr("Open link"), magnet);
        open->setObjectName(QStringLiteral("openFieldLink"));
        connect(open, &QAction::triggered, magnet, [magnetUrl] {
            QDesktopServices::openUrl(magnetUrl);
        });
        magnet->addAction(open);
        connect(magnet, &QLineEdit::returnPressed, open, &QAction::trigger);
    }
    tgrid->addWidget(magnet, 4, 1);
    makeCell(tgrid, 4, 2, tr("Labels"),
             t.labels && !t.labels->isEmpty() ? t.labels->join(QStringLiteral(", ")) : dash());

    Q_UNUSED(torrentCard);
    contentLayout->addStretch(1);
    contentLayout->activate();
    m_scroll->setWidget(m_content);
    // Geometry and the scrollbar range settle after setWidget(). Do not let
    // a later refresh overwrite the position captured from the user's scroll.
    const int scrollToRestore = sameTorrent ? m_pendingScroll : 0;
    QTimer::singleShot(0, this, [this, scrollToRestore, rebuildSerial] {
        if (rebuildSerial != m_rebuildSerial)
            return;
        if (scrollToRestore > 0)
            m_scroll->verticalScrollBar()->setValue(scrollToRestore);
        m_pendingScroll = -1;
    });
}
