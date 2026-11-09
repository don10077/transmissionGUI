#include "propsform.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

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

} // namespace

PropsForm::PropsForm(QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
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
    // Весь контент живёт в отдельном контейнере. Пересборка = снять старый
    // контейнер с layout и удалить его: деструктор QWidget сам каскадно
    // разрушает поддерево (layouts, виджеты). Ручной обход findChildren
    // опасен: Qt 6.12 в деструкторе layout ставит управляемые виджеты в
    // очередь deleteLater, и последующий delete по снимку списка даёт
    // обращение к мёртвым объектам (segfault, воспроизводилось в тестах).
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

    if (!m_torrent.has_value()) {
        auto* placeholder = new QLabel(tr("Torrent not selected"), m_content);
        placeholder->setEnabled(false);
        contentLayout->addWidget(placeholder);
        return;
    }

    const auto& t = *m_torrent;
    const double swd = t.sizeWhenDone.value_or(0.0);
    const double ltd = t.leftUntilDone.value_or(0.0);
    const double percent = tgui::percentComplete(swd, ltd);

    // «Downloaded: [======] 100.0%» (panProgress, tabGeneral).
    auto* progressRow = new QWidget(m_content);
    auto* progressLayout = new QHBoxLayout(progressRow);
    progressLayout->addWidget(new QLabel(tr("Downloaded:"), progressRow));
    auto* bar = new QProgressBar(progressRow);
    bar->setRange(0, 1000);
    bar->setValue(qRound(percent * 10.0));
    bar->setFormat(QStringLiteral("%1%").arg(percent, 0, 'f', 1));
    progressLayout->addWidget(bar, 1);
    contentLayout->addWidget(progressRow);

    auto makeGroup = [this](const QString& title) {
        auto* box = new QGroupBox(title, m_content);
        auto* grid = new QGridLayout(box);
        grid->setColumnStretch(7, 1);
        return qMakePair(box, grid);
    };

    auto makeCell = [this](QGridLayout* grid, int row, int col, const QString& label,
                           const QString& value) {
        auto* l = new QLabel(label + ":", m_content);
        l->setEnabled(false);
        grid->addWidget(l, row, col);
        auto* v = new QLabel(value, m_content);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(v, row, col + 1);
    };

    // Секция «Transfer» (panTransfer) — три пары label/value на строку.
    auto [transferBox, grid] = makeGroup(tr("Transfer"));

    const bool finished = tgui::statusIsFinished(
        t.status.value_or(-1), t.sizeWhenDone.value_or(0.0), t.leftUntilDone.value_or(0.0));
    const QString error = (t.errorString.has_value() && !t.errorString->isEmpty())
                              ? *t.errorString
                              : dash();
    makeCell(grid, 0, 0, tr("Status"), TorrentModel::statusText(t.status.value_or(-1), finished));
    makeCell(grid, 0, 2, tr("Error"), error);
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
    contentLayout->addWidget(transferBox);

    // Секция «Torrent» (panGeneralInfo) — две пары label/value на строку.
    auto [torrentBox, tgrid] = makeGroup(tr("Torrent"));

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
    contentLayout->addWidget(torrentBox);
}