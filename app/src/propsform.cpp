#include "propsform.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDateTime>
#include <QUrl>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include "theme.h"
#include "torrentlabels.h"
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

/** Отображает сокращённый текст, сохраняя полное значение и выбор мышью. */
class PropertyValueLabel : public QLabel {
public:
    using QLabel::QLabel;

    /** Помещает иконки в тот же текстовый документ, в котором выделяются имена. */
    void setLabels(const QStringList& labels)
    {
        QList<QIcon> icons;
        QStringList runs;
        icons.reserve(labels.size());
        runs.reserve(labels.size());
        for (qsizetype i = 0; i < labels.size(); ++i) {
            const auto& label = labels.at(i);
            icons.append(torrentlabels::labelIcon(label));
            runs.append(QStringLiteral("<img src=\"tgui-label:/%1\" width=\"12\" height=\"12\">&nbsp;%2")
                        .arg(i).arg(label.toHtmlEscaped()));
        }

        m_hasLabelIcons = true;
        setAccessibleName(text());
        setTextFormat(Qt::RichText);
        setResourceProvider([icons, dpr = devicePixelRatioF()](const QUrl& url) -> QVariant {
            if (url.scheme() != QStringLiteral("tgui-label"))
                return {};
            bool ok = false;
            const auto index = url.path().mid(1).toLongLong(&ok);
            if (!ok || index < 0 || index >= icons.size())
                return {};
            return QVariant::fromValue(icons.at(index).pixmap(QSize(12, 12), dpr).toImage());
        });
        setText(QStringLiteral("<nobr>%1</nobr>").arg(runs.join(QStringLiteral(", "))));
    }

    QSize minimumSizeHint() const override
    {
        QSize hint = QLabel::minimumSizeHint();
        if (m_hasLabelIcons)
            hint.setWidth(0);
        return hint;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        const QRect area = contentsRect();
        if (textFormat() != Qt::PlainText || hasFocus() || hasSelectedText() ||
            fontMetrics().horizontalAdvance(text()) <= area.width()) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, area.width()));
    }

private:
    bool m_hasLabelIcons = false;
};

} // namespace

/** Карта кусков. Растр пересчитывается только при изменении исходных данных. */
class PieceMapBar : public QWidget {
public:
    explicit PieceMapBar(QWidget* parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setDual(false);
    }

    void setDual(bool dual)
    {
        m_dual = dual;
        setFixedHeight(dual ? 21 : 16);
        update();
    }

    void setData(double percent, const std::optional<QString>& pieces, int pieceCount, const QColor& color)
    {
        m_percent = percent;
        const QString encoded = pieces.value_or(QString());
        if (m_pieces != encoded || m_pieceCount != pieceCount || m_color != color) {
            m_pieces = encoded;
            m_pieceCount = pieceCount;
            m_color = color;
            rebuildMap();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& colors = tgui::theme::palette();
        QPainter painter(this);
        const auto drawTrack = [&](const QRect& track, bool map) {
            painter.fillRect(track, colors.pieceBorder);
            const QRect inner = track.adjusted(1, 1, -1, -1);
            if (!inner.isValid())
                return;
            painter.fillRect(inner, colors.pieceEmpty);
            if (map) {
                painter.drawImage(inner, m_map);
            } else {
                const int filled = qRound(inner.width() * qBound(0.0, m_percent, 100.0) / 100.0);
                painter.fillRect(QRect(inner.left(), inner.top(), filled, inner.height()), m_color);
            }
        };

        const bool hasMap = !m_map.isNull();
        if (m_dual && hasMap) {
            drawTrack(QRect(0, 0, width(), 6), false);
            drawTrack(QRect(0, 7, width(), 14), true);
        } else {
            drawTrack(rect(), hasMap);
        }
    }

private:
    void rebuildMap()
    {
        const QByteArray bits = QByteArray::fromBase64(m_pieces.toLatin1());
        if (m_pieceCount <= 0 || bits.isEmpty()) {
            m_map = {};
            return;
        }

        const int samples = qMin(m_pieceCount, 4000);
        m_map = QImage(samples, 1, QImage::Format_RGB32);
        m_map.fill(tgui::theme::palette().pieceEmpty);
        auto* pixels = reinterpret_cast<QRgb*>(m_map.scanLine(0));
        const QRgb filled = m_color.rgb();
        for (int piece = 0; piece < m_pieceCount && piece / 8 < bits.size(); ++piece) {
            if (static_cast<unsigned char>(bits.at(piece / 8)) & (0x80u >> (piece % 8)))
                pixels[int(qint64(piece) * samples / m_pieceCount)] = filled;
        }
    }

    double m_percent = 0.0;
    QString m_pieces;
    int m_pieceCount = 0;
    QColor m_color;
    QImage m_map;
    bool m_dual = false;
};

PropsForm::PropsForm(QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
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
    const bool limitsChanged = !m_session ||
        m_session->speedLimitDownEnabled != info.speedLimitDownEnabled ||
        m_session->speedLimitDown != info.speedLimitDown ||
        m_session->speedLimitUpEnabled != info.speedLimitUpEnabled ||
        m_session->speedLimitUp != info.speedLimitUp;
    m_session = info;
    if (m_torrent && limitsChanged)
        rebuild();
}

void PropsForm::setEditLabelsAction(QAction* action)
{
    m_editLabels = action;
}

void PropsForm::clear()
{
    m_torrent.reset();
    m_session.reset();
    rebuild();
}

QMenu* PropsForm::createCopyMenu(const QString& value, const QUrl& link)
{
    // Поля пересоздаются при обновлении RPC. Меню и его команды живут в PropsForm.
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("copyFieldMenu"));
    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    auto* copy = menu->addAction(tgui::theme::icon(tgui::theme::Icon::Copy), tr("Copy"));
    copy->setObjectName(QStringLiteral("copyFieldValue"));
    connect(copy, &QAction::triggered, menu, [value] { QApplication::clipboard()->setText(value); });
    if (!link.isEmpty()) {
        auto* open = menu->addAction(tr("Open link"));
        open->setObjectName(QStringLiteral("openFieldLink"));
        connect(open, &QAction::triggered, menu, [link] { QDesktopServices::openUrl(link); });
    }
    return menu;
}

void PropsForm::enableFieldMenu(QWidget* widget, const QString& value, const QUrl& link)
{
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widget, &QWidget::customContextMenuRequested, this, [this, widget, value, link](const QPoint& point) {
        createCopyMenu(value, link)->popup(widget->mapToGlobal(point));
    });
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

    if (!m_torrent) {
        delete m_progressRow;
        m_progressRow = nullptr;
        m_pieceBar = nullptr;
        m_progressPercent = nullptr;
    }

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

    if (!m_progressRow) {
        auto* progressRow = new QFrame(this);
        m_progressRow = progressRow;
        progressRow->setObjectName(QStringLiteral("progressRow"));
        auto* progressLayout = new QHBoxLayout(progressRow);
        progressLayout->setContentsMargins(8, 6, 8, 4);
        progressLayout->setSpacing(8);
        auto* downloaded = new QLabel(tr("Downloaded:"), progressRow);
        downloaded->setObjectName(QStringLiteral("fieldLabel"));
        progressLayout->addWidget(downloaded);
        m_pieceBar = new PieceMapBar(progressRow);
        progressLayout->addWidget(m_pieceBar, 1);
        m_progressPercent = new QLabel(progressRow);
        m_progressPercent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_progressPercent->setMinimumWidth(48);
        progressLayout->addWidget(m_progressPercent);
        m_layout->insertWidget(0, progressRow);

        m_pieceBar->setDual(QSettings().value("ui/pieceBarMode").toString() == QStringLiteral("dual"));
        const QList<QWidget*> progressFields{progressRow, downloaded, m_pieceBar, m_progressPercent};
        for (auto* widget : progressFields) {
            widget->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(widget, &QWidget::customContextMenuRequested, this, [this, widget](const QPoint& point) {
                auto* menu = createCopyMenu(m_progressPercent->text());
                auto* dual = menu->addAction(tr("Show separate download bar"));
                dual->setCheckable(true);
                dual->setChecked(QSettings().value("ui/pieceBarMode").toString() == QStringLiteral("dual"));
                connect(dual, &QAction::toggled, menu, [this](bool checked) {
                    QSettings().setValue("ui/pieceBarMode",
                                         checked ? QStringLiteral("dual") : QStringLiteral("single"));
                    if (m_pieceBar)
                        m_pieceBar->setDual(checked);
                });
                menu->popup(widget->mapToGlobal(point));
            });
        }
    }

    const auto& colors = tgui::theme::palette();
    const QColor pieceColor = !tgui::torrentError(t).isEmpty() ? colors.progressRed
                             : percent >= 100.0 ? colors.progressGreen : colors.blue;
    m_pieceBar->setData(percent, t.pieces, t.pieceCount.value_or(0), pieceColor);
    m_progressPercent->setText(QStringLiteral("%1%").arg(percent, 0, 'f', 1));

    auto makeCard = [this, contentLayout](const QString& title, int pairs) {
        auto* card = new QFrame(m_content);
        card->setObjectName(QStringLiteral("sectionPanel"));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 6, 0, 0);
        cardLayout->setSpacing(2);

        auto* header = new QLabel(title, card);
        header->setObjectName(QStringLiteral("sectionTitle"));
        QFont headerFont = header->font();
        if (headerFont.pointSizeF() > 0)
            headerFont.setPointSizeF(headerFont.pointSizeF() * 13.0 / 12.0);
        else
            headerFont.setPixelSize(qMax(1, qRound(headerFont.pixelSize() * 13.0 / 12.0)));
        header->setFont(headerFont);
        cardLayout->addWidget(header);
        auto* columns = new QHBoxLayout;
        columns->setContentsMargins(8, 2, 8, 4);
        columns->setSpacing(18);

        QList<QGridLayout*> grids;
        for (int i = 0; i < pairs; ++i) {
            auto* body = new QWidget(card);
            auto* grid = new QGridLayout(body);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(10);
            grid->setVerticalSpacing(1);
            grid->setColumnMinimumWidth(0, 110);
            grid->setColumnStretch(1, 1);
            grid->setAlignment(Qt::AlignTop);
            columns->addWidget(body, pairs == 2 && i == 0 ? 14 : 10);
            grids.append(grid);
        }
        cardLayout->addLayout(columns);
        contentLayout->addWidget(card);
        return grids;
    };

    auto makeCell = [this](const QList<QGridLayout*>& grids, int row, int col, const QString& label,
                           const QString& value, const QColor& valueColor = QColor()) -> PropertyValueLabel* {
        auto* grid = grids.at(col / 2);
        const QUrl link = fieldLink(value);
        auto* l = new QLabel(label + ":", m_content);
        l->setObjectName(QStringLiteral("fieldLabel"));
        enableFieldMenu(l, value, link);
        grid->addWidget(l, row, 0);

        auto* v = new PropertyValueLabel(value, m_content);
        v->setObjectName(QStringLiteral("fieldValue"));
        v->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        v->setWordWrap(false);
        v->setTextFormat(Qt::PlainText);
        enableFieldMenu(v, value, link);
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
        grid->addWidget(v, row, 1);
        return v;
    };

    // Секция «Transfer» (panTransfer) — три пары label/value на строку.
    const auto grid = makeCard(tr("Transfer"), 3);

    const bool finished = tgui::statusIsFinished(
        t.status.value_or(-1), t.sizeWhenDone.value_or(0.0), t.leftUntilDone.value_or(0.0));
    const QString effectiveError = tgui::torrentError(t);
    const QString error = effectiveError.isEmpty() ? dash() : effectiveError;
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

    const int priority = t.bandwidthPriority.value_or(0);
    makeCell(grid, 6, 4, tr("Priority"),
             priority > 0 ? QObject::tr("High") : priority < 0 ? QObject::tr("Low") : QObject::tr("Normal"));

    // Секция «Torrent» (panGeneralInfo) — две пары label/value на строку.
    const auto tgrid = makeCard(tr("Torrent"), 2);

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
    const QString magnetText = t.magnetLink.value_or(dash());
    const QUrl magnetUrl(magnetText, QUrl::StrictMode);
    const QUrl openMagnet = magnetUrl.isValid() && magnetUrl.scheme() == QStringLiteral("magnet")
        ? magnetUrl : QUrl{};
    auto* magnetLabel = new QLabel(tr("Magnet Link") + ":", m_content);
    magnetLabel->setObjectName(QStringLiteral("fieldLabel"));
    enableFieldMenu(magnetLabel, magnetText, openMagnet);
    tgrid.at(0)->addWidget(magnetLabel, 4, 0);
    // Like txMagnetLink: a single read-only line, selectable without wrapping.
    auto* magnet = new QLineEdit(magnetText, m_content);
    magnet->setObjectName(QStringLiteral("magnetLink"));
    magnet->setReadOnly(true);
    magnet->setFrame(false);
    magnet->setMinimumWidth(0);
    magnet->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    magnet->setStyleSheet(QStringLiteral("border: none; padding: 0; background: transparent;"));
    magnet->setCursorPosition(0);
    enableFieldMenu(magnet, magnetText, openMagnet);
    if (!openMagnet.isEmpty()) {
        connect(magnet, &QLineEdit::returnPressed, this, [openMagnet] {
            QDesktopServices::openUrl(openMagnet);
        });
    }
    tgrid.at(0)->addWidget(magnet, 4, 1);
    const bool hasLabels = t.labels && !t.labels->isEmpty();
    const QString labelsText = hasLabels ? t.labels->join(QStringLiteral(", ")) : dash();
    auto* labelValue = makeCell(tgrid, 4, 2, tr("Labels"), labelsText);
    if (hasLabels)
        labelValue->setLabels(*t.labels);
    if (auto* action = m_editLabels.data()) {
        auto* row = new QWidget(m_content);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        auto* edit = new QPushButton(hasLabels ? tr("Edit") : tr("Set…"), row);
        edit->setObjectName(QStringLiteral("propertyLink"));
        enableFieldMenu(row, labelsText);
        enableFieldMenu(edit, labelsText);
        const auto syncAction = [action, edit, labelValue, hasLabels] {
            edit->setVisible(action->isVisible());
            edit->setEnabled(action->isEnabled());
            labelValue->setVisible(hasLabels || !action->isVisible());
        };
        connect(action, &QAction::changed, edit, syncAction);
        connect(edit, &QPushButton::clicked, action, &QAction::trigger);
        tgrid.at(1)->removeWidget(labelValue);
        labelValue->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        layout->addWidget(labelValue);
        layout->addWidget(edit);
        layout->addStretch(1);
        tgrid.at(1)->addWidget(row, 4, 1);
        syncAction();
    }

    makeCell(tgrid, 5, 2, tr("Sequential download"),
             t.sequentialDownload ? (*t.sequentialDownload ? QObject::tr("yes") : QObject::tr("no")) : dash());
    makeCell(tgrid, 6, 2, tr("Private"),
             t.isPrivate ? (*t.isPrivate ? QObject::tr("yes") : QObject::tr("no")) : dash());

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
