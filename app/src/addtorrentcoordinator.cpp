#include "addtorrentcoordinator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>

#include <algorithm>
#include <utility>

#include "addlinkdialog.h"
#include "connectioncontroller.h"
#include "daemonprofile.h"
#include "displaynames.h"
#include "externalopen.h"
#include "fileselectdialog.h"
#include "optionsdialog.h"
#include "pathmap.h"
#include "sequentialpolicy.h"
#include "settingsbackup.h"
#include "torrentmodel.h"
#include "torrentreplacement.h"
#include "watchfolderservice.h"
#include "tgui/util.h"
#include "tgui/bencode.h"

namespace {
/** Читает локальный .torrent, ограничивая размер даже при изменении файла. */
QByteArray readLocalTorrent(const QString& path)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) ||
        file.size() > tgui::kMaxTorrentFileBytes)
        return {};
    auto bytes = file.read(tgui::kMaxTorrentFileBytes + 1);
    return bytes.size() <= tgui::kMaxTorrentFileBytes && file.error() == QFileDevice::NoError
        ? bytes : QByteArray{};
}

/** Вычисляет хеш исходной секции info, сохраняя её байтовое представление. */
QString localTorrentHash(const QByteArray& bytes)
{
    auto input = bytes.toStdString();
    tgui::bencode::Value root;
    tgui::bencode::InfoSpan span;
    if (!tgui::bencode::decode(input, root, nullptr, &span, false)) {
        tgui::bencode::RepairResult repaired;
        if (!tgui::bencode::repair(input, repaired))
            return {};
        input = std::move(repaired.fixedBytes);
        if (!tgui::bencode::decode(input, root, nullptr, &span, false))
            return {};
    }
    const auto* info = root.find("info");
    if (!info || span.end <= span.begin)
        return {};
    const auto* version = info->find("meta version");
    const bool v2Only = version && version->toInt() == 2 && !info->find("pieces");
    const auto hash = QCryptographicHash::hash(
        QByteArrayView(input.data() + span.begin, static_cast<qsizetype>(span.end - span.begin)),
        v2Only ? QCryptographicHash::Sha256 : QCryptographicHash::Sha1);
    return QString::fromLatin1(hash.first(20).toHex());
}
} // namespace

AddTorrentCoordinator::AddTorrentCoordinator(QMainWindow& window, const ConnectionController& connection,
                                             const TorrentModel& model, WatchFolderService& watch,
                                             CurrentTorrent currentTorrent)
    : m_window(window), m_connection(connection), m_model(model), m_watchService(watch),
      m_currentId(std::move(currentTorrent))
{
}

void AddTorrentCoordinator::openPendingExternalTorrents()
{
    m_torrentListReady = true;
    openExternalTorrents(std::exchange(m_pendingExternalTorrents, {}));
    if (!m_addingTorrent)
        addNextTorrent();
}

void AddTorrentCoordinator::addWatchedFile(const QString& path)
{
    const auto& session = m_connection.client()->sessionInfo();
    tgui::RpcClient::AddTorrent params = tgui::RpcClient::makeAddParams(session);
    params.file = path;
    const QString watchDir = QSettings().value(OptionsDialog::watchDestinationKey).toString();
    if (!watchDir.isEmpty())
        params.downloadDir = watchDir;
    startAdd(params, AddOrigin::WatchFolder);
}

bool AddTorrentCoordinator::canAcceptWatchedFile() const
{
    return m_connection.client() && m_connection.isConnected() && m_torrentListReady &&
        !m_addingTorrent && m_addQueue.isEmpty();
}

void AddTorrentCoordinator::interrupt()
{
    m_torrentListReady = false;
    m_addInterrupted = m_addInterrupted || m_addingTorrent;
}

void AddTorrentCoordinator::prepareRetry()
{
    m_addingTorrent = false;
}

qsizetype AddTorrentCoordinator::clear()
{
    m_torrentListReady = false;
    const auto discarded = m_addQueue.size();
    m_addQueue.clear();
    m_addingTorrent = false;
    m_addInterrupted = false;
    m_unconfiguredTorrentId = -1;
    m_unconfiguredTorrentHash.clear();
    return discarded;
}

/** Открывает входные файлы и ссылки или ждёт подключения к демону. */
void AddTorrentCoordinator::openExternalTorrents(const QStringList& inputs)
{
    QStringList accepted;
    for (const auto& value : inputs) {
        const auto input = externalTorrentInput(value);
        if (!input.isEmpty() && !accepted.contains(input))
            accepted.append(input);
    }
    if (accepted.isEmpty())
        return;
    if (m_window.isMinimized())
        m_window.showNormal();
    else
        m_window.show();
    m_window.raise();
    m_window.activateWindow();

    if (!m_connection.client() || !m_connection.isConnected()) {
        for (const auto& input : accepted)
            if (!m_pendingExternalTorrents.contains(input))
                m_pendingExternalTorrents.append(input);
        m_window.statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Connect to a daemon to add the files or links you opened."), 8000);
        return;
    }
    const auto& session = m_connection.client()->sessionInfo();
    for (const auto& input : accepted) {
        tgui::RpcClient::AddTorrent params = tgui::RpcClient::makeAddParams(session);
        if (input.startsWith("magnet:", Qt::CaseInsensitive)) {
            params.source = tgui::RpcClient::AddTorrent::Source::Url;
            params.url = input;
        } else {
            params.file = input;
        }
        startAdd(params, AddOrigin::External);
    }
}

void AddTorrentCoordinator::onAddTorrent()
{
    if (!m_connection.client() || !m_connection.isConnected())
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const auto paths = QFileDialog::getOpenFileNames(&m_window, QCoreApplication::translate("MainWindow", "Select a .torrent file"), {},
                                                     QCoreApplication::translate("MainWindow", "Torrent files (*.torrent);;All files (*)"));
    if (!rpc || rpc != m_connection.client() || !m_connection.isConnected())
        return;
    const auto& session = rpc->sessionInfo();
    for (const auto& path : paths) {
        tgui::RpcClient::AddTorrent params = tgui::RpcClient::makeAddParams(session);
        params.file = path;
        startAdd(params);
    }
}

void AddTorrentCoordinator::onAddFromLink()
{
    if (!m_connection.client() || !m_connection.isConnected())
        return;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    AddLinkDialog dlg(QSettings().value("lastAddLink").toString(), &m_window);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!rpc || rpc != m_connection.client() || !m_connection.isConnected())
        return;
    const QString link = dlg.link();
    if (link.isEmpty())
        return;
    QSettings().setValue("lastAddLink", link);
    tgui::RpcClient::AddTorrent at = tgui::RpcClient::makeAddParams(rpc->sessionInfo());
    at.source = tgui::RpcClient::AddTorrent::Source::Url;
    at.url = link;
    at.start = dlg.startWhenAdded();
    startAdd(at);
}

/** Проверяет весь файл и ставит ссылки в обычную очередь добавления после подтверждения. */
void AddTorrentCoordinator::onImportTorrentList()
{
    if (!m_connection.client() || !m_connection.isConnected()) {
        QMessageBox::information(&m_window, QCoreApplication::translate("MainWindow", "Import torrent list"), QCoreApplication::translate("MainWindow", "Not connected to the daemon."));
        return;
    }
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const auto profile = m_connection.profile();
    const auto path = QFileDialog::getOpenFileName(&m_window, QCoreApplication::translate("MainWindow", "Import torrent list"), {},
                                                  QCoreApplication::translate("MainWindow", "Magnet lists (*.txt)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    const auto invalidList = [this] {
        QMessageBox::warning(&m_window, QCoreApplication::translate("MainWindow", "Import torrent list"), QCoreApplication::translate("MainWindow", "The file must contain one magnet link per line."));
    };
    if (!file.open(QIODevice::ReadOnly) || file.size() > settingsbackup::maxBytes) {
        invalidList();
        return;
    }
    QStringList links;
    QSet<QString> identifiers;
    auto contents = QString::fromUtf8(file.read(settingsbackup::maxBytes + 1));
    if (file.error() != QFileDevice::NoError || !file.atEnd()) {
        invalidList();
        return;
    }
    if (contents.startsWith(QChar::ByteOrderMark))
        contents.remove(0, 1);
    for (const auto& line : contents.split('\n')) {
        const auto text = line.trimmed();
        if (text.isEmpty())
            continue;
        const QUrl url(text, QUrl::StrictMode);
        const auto key = magnetIdentifier(url);
        if (!url.isValid() || key.isEmpty()) {
            invalidList();
            return;
        }
        if (!identifiers.contains(key)) {
            identifiers.insert(key);
            links.append(url.toString(QUrl::FullyEncoded));
        }
    }
    if (links.isEmpty()) {
        invalidList();
        return;
    }
    if (QMessageBox::question(&m_window, QCoreApplication::translate("MainWindow", "Import torrent list"),
            QCoreApplication::translate("MainWindow", "Add %n torrent link(s) to the connected daemon?",
                                        nullptr, static_cast<int>(links.size()))) != QMessageBox::Yes)
        return;
    if (rpc && rpc == m_connection.client() && m_connection.isConnected() && profile == m_connection.profile())
        openExternalTorrents(links);
}

void AddTorrentCoordinator::startAdd(const tgui::RpcClient::AddTorrent& params, AddOrigin origin)
{
    m_addQueue.enqueue({params, origin, m_connection.profile()});
    if (!m_addingTorrent && !m_connection.isReconnecting())
        addNextTorrent();
}

void AddTorrentCoordinator::addNextTorrent()
{
    if (!m_connection.client() || !m_connection.isConnected() || !m_torrentListReady || m_addQueue.isEmpty())
        return;

    m_addingTorrent = true;
    auto pending = m_addQueue.dequeue();
    const auto& params = pending.params;
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    if (pending.origin == AddOrigin::WatchFolder || params.source != tgui::RpcClient::AddTorrent::Source::File) {
        submitTorrent(std::move(pending));
        return;
    }

    auto original = readLocalTorrent(params.file);
    if (original.isEmpty()) {
        emit readError(QCoreApplication::translate("MainWindow", "Add torrent"), QFileInfo(params.file).size() > tgui::kMaxTorrentFileBytes
            ? QCoreApplication::translate("MainWindow", "Torrent metadata exceeds the 32 MiB limit.")
            : QCoreApplication::translate("MainWindow", "Could not read the local torrent file: %1").arg(params.file));
        finishAdd(rpc);
        return;
    }
    const auto hash = localTorrentHash(original);
    const auto& torrents = m_model.torrents();
    const auto duplicate = std::find_if(torrents.cbegin(), torrents.cend(), [&](const auto& torrent) {
        return !hash.isEmpty() && torrent.hashString &&
            torrent.hashString->compare(hash, Qt::CaseInsensitive) == 0;
    });
    if (duplicate != torrents.cend()) {
        showDuplicate(duplicate->name.value_or(QString{}));
        return;
    }
    if (QSettings().value(OptionsDialog::skipAddDialogKey, false).toBool()) {
        submitTorrent(std::move(pending), std::move(original));
        return;
    }
    auto metainfo = original;
    auto details = previewTorrentMetainfo(metainfo);
    if (!details) {
        submitTorrent(std::move(pending), std::move(original));
        return;
    }

    // Transmission может начать проверку при torrent-add даже на паузе.
    // Для замены сначала показываем параметры, используя метаданные локального файла.
    details->downloadDir = params.downloadDir;
    details->peerLimit = rpc->sessionInfo().peerLimitPerTorrent;
    auto* dialog = new FileSelectDialog(*details, params.start, rpc->rpcVersion(), rpc, &m_window, true, pending.profile);
    dialog->offerMetainfoCopy(metainfo);
    if (rpc->supports(tgui::kRpcTorrentReplace))
        dialog->offerReplacement(m_model.torrents(), m_currentId());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(rpc.data(), &QObject::destroyed, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this,
            [this, rpc, dialog, pending = std::move(pending), files = std::move(details->files),
             original = std::move(original), metainfo = std::move(metainfo)](int result) mutable {
        if (!rpc || rpc != m_connection.client() || !m_connection.isConnected())
            return;
        if (result != QDialog::Accepted) {
            finishAdd(rpc);
            return;
        }
        const auto oldHash = dialog->replacementHash();
        if (!oldHash.isEmpty()) {
            QSet<QString> skippedPaths;
            for (const int index : dialog->unwanted())
                skippedPaths.insert(files.at(index).name);
            replaceTorrent(&m_window, rpc, oldHash, std::move(metainfo), dialog->options(), std::move(skippedPaths),
                           [this, rpc](int id) {
                if (id >= 0) {
                    emit displayNamesChanged();
                    m_window.statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Torrent replaced"), 5000);
                }
                finishAdd(rpc, id);
            });
            return;
        }
        auto choices = AddChoices{dialog->options(), dialog->saveAs(), dialog->unwanted(), std::move(files),
                                  dialog->displayName(), dialog->displayNameEdited()};
        submitTorrent(std::move(pending), std::move(original), std::move(choices));
    });
    dialog->open();
}

/** Добавляет торрент на паузе и применяет подтверждённые параметры к файлам демона. */
void AddTorrentCoordinator::submitTorrent(PendingAdd pending, QByteArray sourceBytes, std::optional<AddChoices> choices)
{
    auto [params, origin, profile] = std::move(pending);
    const bool fromWatchFolder = origin == AddOrigin::WatchFolder;
    const bool requireLocalFile = params.source == tgui::RpcClient::AddTorrent::Source::File;
    const bool start = params.start;
    params.start = false;

    const QSettings settings;
    const bool skipDialog = fromWatchFolder ||
        (!choices && settings.value(OptionsDialog::skipAddDialogKey, false).toBool());
    auto request = params;
    if (choices)
        request.downloadDir = choices->options.downloadDir;
    // Имя сохраняем только при осознанном вводе, чтобы не плодить записи,
    // совпадающие с именем из метаданных.
    const auto requestedName = choices && choices->displayNameEdited
        ? displaynames::normalize(choices->displayName)
        : QString{};
    const bool deleteSource = params.source == tgui::RpcClient::AddTorrent::Source::File &&
        params.file.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive) &&
        settings.value(OptionsDialog::deleteSourceTorrentKey, false).toBool();
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const auto watched = fromWatchFolder ? m_watchService.candidate(params.file)
                                         : WatchFolderService::Candidate{};
    const auto finishWatch = [this, watched](bool completed, bool invalid = false) {
        m_watchService.finishWatch(watched, completed, invalid);
    };
    if (requireLocalFile) {
        params.file = QFileInfo(params.file).absoluteFilePath();
        request.file = params.file;
        if (sourceBytes.isEmpty())
            sourceBytes = readLocalTorrent(params.file);
        if (sourceBytes.isEmpty()) {
            const bool oversized = QFileInfo(params.file).size() > tgui::kMaxTorrentFileBytes;
            finishWatch(false, oversized);
            emit readError(QCoreApplication::translate("MainWindow", "Add torrent"), oversized ? QCoreApplication::translate("MainWindow", "Torrent metadata exceeds the 32 MiB limit.")
                : QCoreApplication::translate("MainWindow", "Could not read the local torrent file: %1").arg(params.file));
            finishAdd(rpc);
            return;
        }
        request.source = tgui::RpcClient::AddTorrent::Source::Metainfo;
        request.metainfo = sourceBytes;
        if (!deleteSource)
            sourceBytes.clear();
    }

    // Берём список файлов у демона, пока торрент на паузе.
    rpc->addTorrent(request, [this, rpc, start, params, skipDialog, sourceBytes, fromWatchFolder, profile, finishWatch,
                              requestedName, choices = std::move(choices)](
                                const tgui::JsonRpcResult& addedResult, const QByteArray& metainfo) {
        if (!rpc || rpc != m_connection.client())
            return;
        const auto duplicate = addedResult.arguments.value("torrent-duplicate");
        if ((addedResult.ok && duplicate.isObject()) ||
            addedResult.status == "duplicate torrent" || addedResult.error == "duplicate torrent") {
            finishWatch(true);
            if (fromWatchFolder) {
                m_window.statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Torrent already exists: %1").arg(QFileInfo(params.file).fileName()), 5000);
                finishAdd(rpc);
            } else {
                showDuplicate(duplicate.toObject().value("name").toString());
            }
            return;
        }
        const auto added = addedResult.arguments.value("torrent-added").toObject();
        const int id = added.value("id").toInt(-1);
        if (!addedResult.ok || id < 0) {
            const bool invalid = addedResult.status == "invalid or corrupt torrent file" ||
                addedResult.status == "unrecognized info";
            finishWatch(false, invalid);
            const QString message = !addedResult.ok ? addedResult.error : QCoreApplication::translate("MainWindow", "Unable to add torrent.");
            emit readError(QCoreApplication::translate("MainWindow", "Add torrent"), message);
            finishAdd(rpc);
            return;
        }
        m_unconfiguredTorrentId = id;
        m_unconfiguredTorrentHash = added.value("hashString").toString();
        if (!m_connection.isConnected())
            return;
        rpc->getTorrentDetails(id, [this, rpc, id, start, params, skipDialog, sourceBytes, fromWatchFolder, profile,
                                   finishWatch, choices, metainfo, requestedName,
                                   hash = added.value("hashString").toString()](const auto& details) {
            if (!rpc || rpc != m_connection.client() || !m_connection.isConnected())
                return;
            const auto remove = [this, rpc, id, finishWatch](bool cancelled) {
                rpc->removeTorrent(id, false, [this, rpc, finishWatch, cancelled](const tgui::JsonRpcResult& result) {
                    if (!rpc || rpc != m_connection.client())
                        return;
                    finishWatch(result.ok && cancelled);
                    if (!result.ok)
                        emit readError(QCoreApplication::translate("MainWindow", "Remove torrent"), result.error);
                    finishAdd(rpc);
                });
            };
            if (!details) {
                remove(false);
                return;
            }
            const auto configured = [this, rpc, id, path = params.file, sourceBytes, finishWatch, hash, requestedName](
                                        const tgui::JsonRpcResult& result) {
                if (!rpc || rpc != m_connection.client())
                    return;
                finishWatch(result.ok);
                if (!result.ok) {
                    emit readError(QCoreApplication::translate("MainWindow", "Torrent added with incomplete settings"), result.error);
                } else {
                    if (!requestedName.isEmpty() && displaynames::set(hash, requestedName))
                        emit displayNamesChanged();
                    if (!sourceBytes.isEmpty()) {
                        QFile original(path);
                        const bool unchanged = original.open(QIODevice::ReadOnly) &&
                            original.read(sourceBytes.size() + 1) == sourceBytes &&
                            original.error() == QFileDevice::NoError;
                        original.close();
                        if (!unchanged || !original.remove())
                            emit readError(QCoreApplication::translate("MainWindow", "Torrent added"), QCoreApplication::translate("MainWindow", "Could not remove the original file: %1").arg(path));
                        else
                            m_window.statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Torrent added"), 5000);
                    } else {
                        m_window.statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Torrent added"), 5000);
                    }
                }
                finishAdd(rpc, id);
            };
            if (choices && std::ranges::equal(choices->files, details->files, [](const auto& preview, const auto& actual) {
                    return preview.index == actual.index && preview.name == actual.name && preview.size == actual.size;
                })) {
                m_unconfiguredTorrentHash.clear();
                rpc->configureAddedTorrent(*details, choices->options, choices->saveAs, choices->unwanted, configured);
                return;
            }
            if (skipDialog) {
                auto options = params;
                options.downloadDir = details->downloadDir;
                options.start = start;
                options.sequential = options.sequential ||
                    tgui::automaticSequential(details->files, rpc->rpcVersion());
                m_unconfiguredTorrentHash.clear();
                rpc->configureAddedTorrent(*details, options, {}, {}, configured);
                return;
            }
            auto* dialog = new FileSelectDialog(*details, start, rpc->rpcVersion(), rpc, &m_window, !fromWatchFolder, profile);
            dialog->offerMetainfoCopy(metainfo);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            connect(rpc.data(), &QObject::destroyed, dialog, &QDialog::reject);
            connect(dialog, &QDialog::finished, this, [this, rpc, dialog, remove, details, configured](int dialogResult) {
                if (!rpc || rpc != m_connection.client())
                    return;
                if (!m_connection.isConnected())
                    return;
                if (dialogResult != QDialog::Accepted) {
                    remove(true);
                    return;
                }
                m_unconfiguredTorrentHash.clear();
                rpc->configureAddedTorrent(*details, dialog->options(), dialog->saveAs(),
                                           dialog->unwanted(), configured);
            });
            dialog->open();
        });
    });
}

/** Сообщает о дубликате без открытия настроек и изменения существующего торрента. */
void AddTorrentCoordinator::showDuplicate(const QString& name)
{
    const QPointer<tgui::RpcClient> rpc(m_connection.client());
    const QString message = name.isEmpty() ? QCoreApplication::translate("MainWindow", "Torrent already exists.") :
        QCoreApplication::translate("MainWindow", "The torrent “%1” already exists.").arg(name);
    auto* box = new QMessageBox(QMessageBox::Information, QCoreApplication::translate("MainWindow", "Add torrent"),
                                message, QMessageBox::Ok, &m_window);
    box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(rpc.data(), &QObject::destroyed, box, &QDialog::reject);
    connect(box, &QDialog::finished, this, [this, rpc] { finishAdd(rpc); });
    box->open();
}

/** Завершает элемент очереди и перечитывает список на том же подключении. */
void AddTorrentCoordinator::finishAdd(const QPointer<tgui::RpcClient>& rpc, int selectedId)
{
    if (!rpc || rpc != m_connection.client())
        return;
    m_addingTorrent = false;
    if (!m_connection.isConnected())
        return;
    m_unconfiguredTorrentId = -1;
    m_unconfiguredTorrentHash.clear();
    if (selectedId >= 0)
        emit torrentAdded(selectedId);
    rpc->getTorrents();
    addNextTorrent();
    if (!m_addingTorrent)
        m_watchService.scheduleScan();
}

/** Продолжает очередь после получения свежего списка от того же демона. */
void AddTorrentCoordinator::resumeAddQueue(const QVector<tgui::Torrent>& torrents)
{
    const bool interrupted = std::exchange(m_addInterrupted, false);
    const int id = std::exchange(m_unconfiguredTorrentId, -1);
    const auto hash = std::exchange(m_unconfiguredTorrentHash, {});
    const auto torrent = std::find_if(torrents.cbegin(), torrents.cend(), [&](const auto& item) {
        return item.id == id && !hash.isEmpty() && item.hashString &&
            item.hashString->compare(hash, Qt::CaseInsensitive) == 0;
    });
    if (torrent != torrents.cend() &&
        torrent->status == static_cast<int>(tgui::TorrentStatus::Stopped)) {
        m_addingTorrent = true;
        m_unconfiguredTorrentId = id;
        m_unconfiguredTorrentHash = hash;
        const QPointer<tgui::RpcClient> rpc(m_connection.client());
        rpc->torrentActionByHash("torrent-remove", hash, {{"delete-local-data", false}},
            [this, rpc](const tgui::JsonRpcResult& result) {
                if (!rpc || rpc != m_connection.client() || !m_connection.isConnected())
                    return;
                if (!result.ok)
                    emit readError(QCoreApplication::translate("MainWindow", "Interrupted torrent add"), result.error);
                finishAdd(rpc);
            });
        return;
    }
    if (interrupted)
        m_window.statusBar()->showMessage(QCoreApplication::translate("MainWindow", "A torrent add was interrupted. Check the list before adding it again."), 10000);
    addNextTorrent();
}
