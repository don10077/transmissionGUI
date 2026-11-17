#include "torrentactions.h"

#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>

#include <algorithm>
#include <utility>

#include "connectioncontroller.h"
#include "daemonprofile.h"
#include "movedialog.h"
#include "pathmap.h"
#include "optionsdialog.h"
#include "propsdialog.h"
#include "statusbarcontroller.h"
#include "theme.h"
#include "torrentlabels.h"
#include "torrentmodel.h"
#include "tgui/util.h"

namespace {
/** Человекочитаемое имя действия для строки состояния (I18N-03):
    показываем действие, а не сырое имя RPC-метода. Неизвестные методы — как есть. */
static QString actionLabel(const QString& method)
{
    // Свободная функция: контекст указываем явно (I18N-03).
    static const QHash<QString, QString> kNames = {
        {QStringLiteral("torrent-start"), QCoreApplication::translate("MainWindow", "Start")},
        {QStringLiteral("torrent-start-now"), QCoreApplication::translate("MainWindow", "Start now")},
        {QStringLiteral("torrent-stop"), QCoreApplication::translate("MainWindow", "Stop")},
        {QStringLiteral("torrent-remove"), QCoreApplication::translate("MainWindow", "Remove")},
        {QStringLiteral("torrent-set"), QCoreApplication::translate("MainWindow", "Change settings")},
        {QStringLiteral("torrent-rename-path"), QCoreApplication::translate("MainWindow", "Rename")},
        {QStringLiteral("torrent-set-location"), QCoreApplication::translate("MainWindow", "Move")},
        {QStringLiteral("torrent-verify"), QCoreApplication::translate("MainWindow", "Verify")},
        {QStringLiteral("torrent-reannounce"), QCoreApplication::translate("MainWindow", "Reannounce")},
        {QStringLiteral("session-set"), QCoreApplication::translate("MainWindow", "Change daemon settings")},
    };
    return kNames.value(method, method);
}
} // namespace

TorrentActions::TorrentActions(QMainWindow& window, const ConnectionController& connection,
                               const TorrentModel& model, StatusBarController& status, QStringList& labels,
                               Selection selection, CurrentTorrent current, RefreshDetails refresh,
                               std::function<void()> labelsChanged)
    : m_window(window), m_connection(connection), m_model(model), m_status(status), m_labels(labels),
      m_selection(std::move(selection)), m_currentId(std::move(current)),
      m_refreshDetails(std::move(refresh)), m_labelsChanged(std::move(labelsChanged))
{
    // После переноса демон может ответить раньше, чем обновится список.
    m_moveVerifyTimer.setSingleShot(true);
    m_moveVerifyTimer.setInterval(1000);
    connect(&m_moveVerifyTimer, &QTimer::timeout, this, &TorrentActions::checkPendingMove);
}

void TorrentActions::createActions()
{
    using tgui::theme::Icon;

    m_actStart = new QAction(tr("Start"), &m_window);
    m_actStart->setShortcut(QKeySequence(Qt::Key_F3));
    m_actStart->setIcon(tgui::theme::icon(Icon::Start));
    m_actStart->setToolTip(tr("Start selected torrents (F3)"));
    connect(m_actStart, &QAction::triggered, this, &TorrentActions::onStartSelected);

    m_actStop = new QAction(tr("Pause"), &m_window);
    m_actStop->setShortcut(QKeySequence(Qt::Key_F4));
    m_actStop->setIcon(tgui::theme::icon(Icon::Pause));
    m_actStop->setToolTip(tr("Pause selected torrents (F4)"));
    connect(m_actStop, &QAction::triggered, this, &TorrentActions::onStopSelected);

    m_actStartAll = new QAction(tr("Start all torrents"), &m_window);
    m_actStartAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F3));
    m_actStartAll->setToolTip(tr("Start all torrents"));
    connect(m_actStartAll, &QAction::triggered, this, &TorrentActions::onStartAll);

    m_actStopAll = new QAction(tr("Pause all torrents"), &m_window);
    m_actStopAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F4));
    m_actStopAll->setToolTip(tr("Pause all torrents"));
    connect(m_actStopAll, &QAction::triggered, this, &TorrentActions::onStopAll);

    m_actRemove = new QAction(tr("Remove"), &m_window);
    m_actRemove->setShortcut(QKeySequence::Delete);
    m_actRemove->setIcon(tgui::theme::icon(Icon::Remove));
    m_actRemove->setToolTip(tr("Remove selected torrents (Del)"));
    connect(m_actRemove, &QAction::triggered, this, &TorrentActions::onRemoveSelected);

    m_actProperties = new QAction(tr("Properties…"), &m_window);
    m_actProperties->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    m_actProperties->setIcon(tgui::theme::icon(Icon::Properties));
    m_actProperties->setToolTip(tr("Torrent properties (Alt+Enter)"));
    m_actProperties->setEnabled(false);
    connect(m_actProperties, &QAction::triggered, this, &TorrentActions::onShowProperties);

    m_actMove = new QAction(tr("Set data location…"), &m_window);
    m_actMove->setShortcut(QKeySequence(Qt::Key_F6));
    m_actMove->setIcon(tgui::theme::icon(Icon::Move));
    m_actMove->setToolTip(tr("Move torrent data (F6)"));
    m_actMove->setEnabled(false);
    connect(m_actMove, &QAction::triggered, this, &TorrentActions::onMoveSelected);
}

void TorrentActions::setConnected(bool connected)
{
    m_actStartAll->setEnabled(connected);
    m_actStopAll->setEnabled(connected);
}

void TorrentActions::updateSelection(qsizetype count)
{
    const bool hasSelection = m_connection.isConnected() && count != 0;
    m_actStart->setEnabled(hasSelection);
    m_actStop->setEnabled(hasSelection);
    m_actRemove->setEnabled(hasSelection);
    m_actProperties->setEnabled(hasSelection);
    m_actMove->setEnabled(m_connection.isConnected() && count == 1);
}

void TorrentActions::suspend()
{
    m_moveVerifyTimer.stop();
}

void TorrentActions::clearPendingMove()
{
    m_movePendingIds.clear();
    m_movePendingTarget.clear();
    ++m_moveGeneration;
}

void TorrentActions::onReadError(const QString& what, const QString& error)
{
    m_window.statusBar()->showMessage(tr("%1: %2").arg(what, error), 8000);
}

void TorrentActions::onRequestCompleted(const QString& method, bool ok, const QString& error,
                                    const QJsonObject& arguments)
{
    Q_UNUSED(arguments);
    if (ok && method == QLatin1String("torrent-set-location") && m_connection.client())
        m_connection.client()->getTorrents();
    if (method == QLatin1String("session-set") && m_connection.client() && m_connection.isConnected())
        m_connection.client()->getSession(); // Refresh on success and roll back the toggle on failure.
    const QString name = actionLabel(method);
    if (ok)
        m_window.statusBar()->showMessage(tr("«%1» done").arg(name), 5000);
    else
        m_window.statusBar()->showMessage(tr("«%1»: %2").arg(name, error), 8000);
}

/** Обновляет список и нужные детали после успешного ответа того же соединения. */
tgui::RpcClient::RequestCallback TorrentActions::afterTorrentChange(const QString& method,
                                                                RefreshAfter refresh, int id,
                                                                const QString& successMessage)
{
    return [this, rpc = QPointer(m_connection.client()), method, refresh, id, successMessage]
           (const tgui::JsonRpcResult& result) {
        if (!rpc || rpc != m_connection.client())
            return;
        if (result.ok && !successMessage.isEmpty())
            m_status.showToast(successMessage);
        else
            onRequestCompleted(method, result.ok, result.error, result.arguments);
        if (!result.ok) {
            if (refresh == RefreshAfter::Files && id == m_currentId())
                m_refreshDetails(refresh, false);
            return;
        }
        rpc->getTorrents();
        if (id != m_currentId())
            return;
        if (refresh == RefreshAfter::Files)
            m_refreshDetails(refresh, true);
        if (refresh == RefreshAfter::Trackers)
            rpc->getTrackers(id);
        if (refresh == RefreshAfter::Properties)
            m_refreshDetails(refresh, true);
    };
}

/** Отправляет весь выбор одним RPC с обновлением списка по подтверждению. */
void TorrentActions::runTorrentAction(const QString& method, const QVector<int>& ids,
                                  const QJsonObject& params)
{
    if (m_connection.client() && m_connection.isConnected() && !ids.isEmpty())
        m_connection.client()->torrentAction(method, ids, params, afterTorrentChange(method, RefreshAfter::List));
}

/** Открывает свойства конкретного торрента на указанной вкладке. */
void TorrentActions::showProperties(int id, int page)
{
    const auto* summary = m_model.torrentById(id);
    if (!m_connection.client() || !m_connection.isConnected() || !summary)
        return;
    const QPointer rpc(m_connection.client());
    m_connection.client()->getTorrentProperties(id, [this, rpc, id, page, hash = summary->hashString](const auto& detail) {
        if (!rpc || rpc != m_connection.client() || !m_connection.isConnected())
            return;
        if (!detail || (hash && detail->hashString && hash != detail->hashString)) {
            onReadError(tr("Properties"), tr("Torrent not found."));
            return;
        }
        PropsDialog dialog(*detail, &m_window, rpc->rpcVersion(), page);
        if (dialog.exec() == QDialog::Accepted && rpc && rpc == m_connection.client() && m_connection.isConnected() &&
            !dialog.changes().isEmpty()) {
            rpc->torrentAction("torrent-set", {id}, dialog.changes(),
                               afterTorrentChange("torrent-set", RefreshAfter::Properties, id));
        }
    }, tgui::RpcClient::TorrentProperties::Settings);
}

/** Сверяет downloadDir выбранных торрентов с целью переноса после ответа демона. */
void TorrentActions::checkPendingMove()
{
    if (!m_connection.client() || !m_connection.isConnected() || m_movePendingIds.isEmpty())
        return;
    const auto normalize = [](const QString& path) {
        QString value = QDir::fromNativeSeparators(path.trimmed());
        while (value.size() > 1 && value.endsWith(QLatin1Char('/')))
            value.chop(1);
#ifdef Q_OS_WIN
        return value.toLower();
#else
        return value;
#endif
    };
    const auto target = normalize(m_movePendingTarget);
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    rpc->getTorrentLocations(m_movePendingIds,
        [this, rpc, generation = m_moveGeneration, target, normalize](const tgui::JsonRpcResult& result) {
            if (!rpc || rpc != m_connection.client() || !m_connection.isConnected() || generation != m_moveGeneration)
                return;
            QSet<int> matching;
            if (result.ok) {
                for (const auto& row : result.arguments.value("torrents").toArray()) {
                    const auto item = row.toObject();
                    if (normalize(item.value("downloadDir").toString()) == target)
                        matching.insert(item.value("id").toInt(-1));
                }
            }
            const bool confirmed = std::ranges::all_of(m_movePendingIds,
                [&](int id) { return matching.contains(id); });
            if (!confirmed && !m_moveDeadline.hasExpired()) {
                m_moveVerifyTimer.start();
                return;
            }
            if (confirmed) {
                m_window.statusBar()->showMessage(tr("Move completed: %1").arg(m_movePendingTarget), 5000);
                rpc->getTorrents();
            } else {
                m_window.statusBar()->showMessage(tr("Could not confirm the new torrent folder."), 8000);
            }
            m_movePendingIds.clear();
            m_movePendingTarget.clear();
        });
}

void TorrentActions::onStartSelected()
{
    runTorrentAction("torrent-start", m_selection());
}

void TorrentActions::onStopSelected()
{
    runTorrentAction("torrent-stop", m_selection());
}

void TorrentActions::onStartAll()
{
    if (m_connection.client())
        m_connection.client()->startAll(false, afterTorrentChange("torrent-start", RefreshAfter::List));
}

void TorrentActions::onStopAll()
{
    if (m_connection.client())
        m_connection.client()->stopAll(afterTorrentChange("torrent-stop", RefreshAfter::List));
}

void TorrentActions::onRemoveSelected()
{
    if (!m_connection.client())
        return;
    const auto ids = m_selection();
    if (ids.isEmpty())
        return;

    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    QMessageBox box(QMessageBox::Question, tr("Remove torrents"),
                 tr("Remove %n torrent(s) from the daemon's list?").arg(ids.size()), // плюрал (I18N-03)
                 QMessageBox::Cancel, &m_window);
    box.setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Warning).pixmap(QSize(32, 32), m_window.devicePixelRatioF()));
    auto* remove = box.addButton(tr("Remove"), QMessageBox::AcceptRole);
    // Фаза 6.1: разрушительное действие — красная рамка, default — отмена.
    remove->setProperty("danger", true);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (rpc && box.clickedButton() == remove)
        runTorrentAction("torrent-remove", ids);
}

void TorrentActions::onShowProperties()
{
    const auto ids = m_selection();
    if (ids.size() == 1) {
        showProperties(ids.first(), 0);
    } else if (ids.size() > 1 && m_connection.client() && m_connection.isConnected()) {
        const QPointer<tgui::RpcClient> rpc(m_connection.client());
        const auto profile = m_connection.profile();
        PropsDialog dialog(static_cast<int>(ids.size()), &m_window, rpc->rpcVersion());
        if (dialog.exec() != QDialog::Accepted || !rpc || rpc != m_connection.client() ||
            !m_connection.isConnected() || profile != m_connection.profile() || dialog.changes().isEmpty())
            return;
        rpc->torrentAction("torrent-set", ids, dialog.changes(),
            afterTorrentChange("torrent-set", RefreshAfter::Properties, m_currentId()));
    }
}

void TorrentActions::onMoveSelected()
{
    if (!m_connection.client() || !m_connection.isConnected())
        return;
    const auto ids = m_selection();
    if (ids.isEmpty())
        return;
    const tgui::Torrent* found = m_model.torrentById(ids.first());
    if (!found)
        return;

    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const auto profile = m_connection.profile();
    const auto& options = rpc->options();
    const bool localDaemon = options.proxy.type() == QNetworkProxy::NoProxy && tgui::isLocalDaemon(options.url);
    MoveDialog dlg(found->downloadDir.value_or(QString()), &m_window, profile, localDaemon);
    if (ids.size() > 1)
        dlg.setWindowTitle(tr("Move %n torrents").arg(ids.size())); // плюрал (I18N-03)
    if (dlg.exec() != QDialog::Accepted || !rpc || rpc != m_connection.client() ||
        !m_connection.isConnected() || profile != m_connection.profile())
        return;

    const QString target = dlg.targetDir();
    const bool moveData = dlg.moveData();
    if (target.isEmpty())
        return;
    rpc->setLocation(ids, target, moveData,
                     [this, rpc, ids, target, profile, moveData](const tgui::JsonRpcResult& result) {
        if (!rpc || rpc != m_connection.client())
            return;
        if (!result.ok) {
            onRequestCompleted(QStringLiteral("torrent-set-location"), false, result.error,
                               result.arguments);
            return;
        }
        ProfilePreferences(profile).rememberMove(target);
        m_window.statusBar()->showMessage(tr("Move started: %1").arg(target), 4000);
        // Демон не возвращает новый путь в ответе.
        m_moveVerifyTimer.stop();
        m_movePendingIds = ids;
        m_movePendingTarget = target;
        m_moveDeadline = QDeadlineTimer(moveData ? 120000 : 10000);
        ++m_moveGeneration;
        checkPendingMove();
    });
}

void TorrentActions::onSetLabels()
{
    const auto ids = m_selection();
    if (ids.isEmpty() || m_labels.isEmpty() || !m_connection.client() || !m_connection.isConnected() || !m_connection.client()->supports(16))
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    QStringList current;
    if (ids.size() == 1) {
        for (const auto& t : m_model.torrents()) {
            if (t.id == ids.first()) {
                current = t.labels.value_or(QStringList{});
                break;
            }
        }
    }
    QDialog dialog(&m_window);
    dialog.setWindowTitle(tr("Torrent labels"));
    dialog.resize(460, 0);
    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* note = new QLabel(ids.size() > 1
        ? tr("Selected torrents: %1. Labels will be overwritten.").arg(ids.size())
        : tr("Check labels from the catalog or enter your own separated by commas."), &dialog);
    note->setObjectName(QStringLiteral("hintLabel"));
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    root->addWidget(note);

    auto* chipsGrid = new QGridLayout;
    chipsGrid->setHorizontalSpacing(12);
    chipsGrid->setVerticalSpacing(6);
    QList<QCheckBox*> chips;
    chipsGrid->setColumnStretch(0, 1);
    chipsGrid->setColumnStretch(1, 1);
    root->addLayout(chipsGrid);

    auto* otherRow = new QHBoxLayout;
    otherRow->setSpacing(8);
    otherRow->addWidget(new QLabel(tr("Other labels:"), &dialog));
    auto* other = new QLineEdit(&dialog);
    other->setObjectName(QStringLiteral("otherLabels"));
    otherRow->addWidget(other, 1);
    root->addLayout(otherRow);

    const auto rebuild = [&](const QStringList& selected) {
        qDeleteAll(chips);
        chips.clear();
        for (const auto& label : m_labels) {
            auto* chip = new QCheckBox(label, &dialog);
            chip->setIcon(torrentlabels::labelIcon(label));
            chip->setChecked(selected.contains(label));
            chipsGrid->addWidget(chip, chips.size() / 2, chips.size() % 2);
            chips.append(chip);
        }
        QStringList others;
        for (const auto& label : selected) {
            if (!m_labels.contains(label))
                others.append(label);
        }
        other->setText(others.join(QStringLiteral(", ")));
    };
    const auto selectedLabels = [&] {
        QStringList labels;
        for (auto* chip : chips) {
            if (chip->isChecked())
                labels.append(chip->text());
        }
        for (auto label : other->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            label = label.trimmed();
            if (!label.isEmpty() && !labels.contains(label))
                labels.append(label);
        }
        return labels;
    };
    rebuild(current);

    auto* editCatalog = new QPushButton(tr("Edit label catalog…"), &dialog);
    editCatalog->setObjectName(QStringLiteral("propertyLink"));
    connect(editCatalog, &QPushButton::clicked, &dialog, [&] {
        auto selected = selectedLabels();
        OptionsDialog options(&m_window, m_connection.client() ? m_connection.client()->rpcVersion() : 0, m_connection.client(), m_connection.profile());
        options.setCurrentPage(OptionsDialog::LabelsPage);
        connect(&options, &OptionsDialog::labelRenamed, &options, [&](const QString& from, const QString& to) {
            for (auto* labels : {&selected, &current}) {
                for (auto& label : *labels) {
                    if (label == from)
                        label = to;
                }
                labels->removeDuplicates();
            }
        });
        options.exec();
        m_labels = torrentlabels::configured();
        rebuild(selected);
        m_labelsChanged();
    });
    root->addWidget(editCatalog, 0, Qt::AlignLeft);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    tgui::theme::decorateDialog(&dialog);
    if (dialog.exec() != QDialog::Accepted || !rpc)
        return;
    const auto labels = selectedLabels();
    if (ids.size() == 1 && labels == current)
        return;
    QJsonArray torrentIds;
    for (const int id : ids)
        torrentIds.append(id);
    rpc->setTorrent(ids.first(), {{"ids", torrentIds}, {"labels", QJsonArray::fromStringList(labels)}});
}

void TorrentActions::onRemoveWithDataSelected()
{
    if (!m_connection.client())
        return;
    const auto ids = m_selection();
    if (ids.isEmpty())
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    auto* box = new QMessageBox(&m_window);
    box->setWindowTitle(tr("Remove with data"));
    box->setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Warning).pixmap(QSize(32, 32), m_window.devicePixelRatioF()));
    box->setText(tr("Remove the selected torrent(s) AND delete their downloaded data files?"));
    box->setInformativeText(tr("This cannot be undone."));
    box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    // Фаза 6.1: «Удалить» — красная рамка и НЕ default: Enter не удаляет случайно.
    box->button(QMessageBox::Yes)->setProperty("danger", true);
    box->setDefaultButton(QMessageBox::No);
    const int r = box->exec();
    box->deleteLater();
    if (rpc && r == QMessageBox::Yes)
        runTorrentAction("torrent-remove", ids, {{"delete-local-data", true}});
}

void TorrentActions::onVerifySelected()
{
    const auto ids = m_selection();
    if (ids.isEmpty() || !m_connection.client() || !m_connection.isConnected())
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    QMessageBox box(QMessageBox::Question, tr("Verify"),
        ids.size() == 1
            ? tr("Torrent verification may take a long time.\nAre you sure to start verification?")
            : tr("Torrent verification may take a long time.\nAre you sure to start verification of %n torrent(s)?") // плюрал (I18N-03)
                  .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, &m_window);
    box.setIconPixmap(tgui::theme::icon(tgui::theme::Icon::Question).pixmap(QSize(32, 32), m_window.devicePixelRatioF()));
    if (box.exec() == QMessageBox::Yes && rpc)
        runTorrentAction("torrent-verify", ids);
}

void TorrentActions::onReannounceSelected()
{
    runTorrentAction("torrent-reannounce", m_selection());
}

void TorrentActions::onQueueTop()
{
    runTorrentAction("queue-move-top", m_selection());
}

void TorrentActions::onQueueUp()
{
    runTorrentAction("queue-move-up", m_selection());
}

void TorrentActions::onQueueDown()
{
    runTorrentAction("queue-move-down", m_selection());
}

void TorrentActions::onQueueBottom()
{
    runTorrentAction("queue-move-bottom", m_selection());
}

void TorrentActions::onStartForceSelected()
{
    runTorrentAction("torrent-start-now", m_selection());
}

void TorrentActions::onUpdateBlocklist()
{
    if (m_connection.client() && m_connection.isConnected())
        m_connection.client()->updateBlocklist();
}

void TorrentActions::onRenameSelected()
{
    const auto ids = m_selection();
    if (ids.size() != 1 || !m_connection.client() || !m_connection.isConnected()) {
        if (ids.size() > 1)
            QMessageBox::warning(&m_window, tr("Rename"), tr("Only one torrent can be renamed."));
        return;
    }
    const int id = ids.first();
    const tgui::Torrent* found = nullptr;
    for (const auto& t : m_model.torrents())
        if (t.id == id) {
            found = &t;
            break;
        }
    if (!found || !found->name.has_value())
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const QString currentName = found->name.value();
    if (currentName.isEmpty())
        return;
    bool ok = false;
    const QString newName = QInputDialog::getText(
        &m_window, tr("Rename"), tr("New name:"),
        QLineEdit::Normal, currentName, &ok);
    const QString trimmed = newName.trimmed();
    if (!ok || !rpc || trimmed.isEmpty() || trimmed == currentName)
        return;
    rpc->renamePath(id, currentName, trimmed,
                    afterTorrentChange("torrent-rename-path", RefreshAfter::Properties, id));
}

void TorrentActions::onPriorityHigh()
{
    runTorrentAction("torrent-set", m_selection(), {{"bandwidthPriority", 1}});
}

void TorrentActions::onPriorityNormal()
{
    runTorrentAction("torrent-set", m_selection(), {{"bandwidthPriority", 0}});
}

void TorrentActions::onPriorityLow()
{
    runTorrentAction("torrent-set", m_selection(), {{"bandwidthPriority", -1}});
}

void TorrentActions::onPortTest()
{
    if (!m_connection.client() || !m_connection.isConnected())
        return;
    m_connection.client()->portTest();
}
