#include "torrentreplacement.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>

#include "tgui/bencode.h"
#include "tgui/util.h"

namespace {

using tgui::TorrentDetails;
using tgui::TorrentStatus;
using tgui::JsonRpcResult;
using tgui::bencode::Value;

constexpr auto pollIntervalMs = 500;

/** Читает безопасный компонент пути из метаданных BitTorrent. */
QString pathComponent(const Value* value)
{
    if (!value || value->type() != Value::Type::String)
        return {};
    const auto name = QString::fromStdString(value->toString());
    static const QRegularExpression unsafe(QStringLiteral(R"([\\/:"<>|?*\x00-\x1f])"));
    if (name.toUtf8().toStdString() != value->toString() || name == "." || name == ".." ||
        unsafe.match(name).hasMatch() ||
        name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        return {};
    return name;
}

} // namespace

std::optional<tgui::TorrentDetails> previewTorrentMetainfo(QByteArray& bytes)
{
    const std::string input = bytes.toStdString();
    Value root;
    if (!tgui::bencode::decode(input, root)) {
        tgui::bencode::RepairResult repair;
        if (!tgui::bencode::repair(input, repair) ||
            !tgui::bencode::decode(repair.fixedBytes, root))
            return {};
        bytes = QByteArray::fromStdString(repair.fixedBytes);
    }
    const auto* info = root.find("info");
    if (!info || !info->find("pieces"))
        return {};
    const auto* utf8Name = info->find("name.utf-8");
    const auto name = pathComponent(utf8Name ? utf8Name : info->find("name"));
    if (name.isEmpty())
        return {};
    const auto* files = info->find("files");
    tgui::TorrentDetails details;
    details.name = name;
    const auto size = [](const Value* length) -> qint64 {
        return length && length->type() == Value::Type::Integer ? length->toInt() : -1;
    };
    if (!files) {
        const auto length = size(info->find("length"));
        if (length < 0)
            return {};
        tgui::FileStat file;
        file.name = name;
        file.size = static_cast<double>(length);
        details.files.append(file);
        return details;
    }
    if (files->asList().empty())
        return {};
    QSet<QString> foldedPaths;
    for (const auto& file : files->asList()) {
        const auto* path = file.find("path.utf-8");
        if (!path)
            path = file.find("path");
        const auto length = size(file.find("length"));
        if (!path || path->asList().empty() || length < 0)
            return {};
        QString full = name;
        for (const auto& component : path->asList()) {
            const auto part = pathComponent(&component);
            if (part.isEmpty())
                return {};
            full += '/' + part;
        }
        const auto folded = full.toCaseFolded();
        if (foldedPaths.contains(folded))
            return {};
        foldedPaths.insert(folded);
        tgui::FileStat entry;
        entry.index = static_cast<int>(details.files.size());
        entry.name = full;
        entry.size = static_cast<double>(length);
        details.files.append(entry);
    }
    return details;
}

namespace {

bool checking(int status)
{
    return status == static_cast<int>(TorrentStatus::CheckWait) ||
           status == static_cast<int>(TorrentStatus::Check);
}

/** Управляет одной заменой. Все изменяющие вызовы идентифицируют раздачу по хешу. */
class TorrentReplacement final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(TorrentReplacement)
public:
    TorrentReplacement(QWidget* window, tgui::RpcClient* rpc, QString oldHash,
                       std::function<void(int)> callback)
        : QObject(window), m_window(window), m_rpc(rpc), m_oldHash(std::move(oldHash)),
          m_callback(std::move(callback))
    {
        connect(rpc, &QObject::destroyed, this, [this] {
            // Не вызываем интерфейс из деструктора окна/клиента.
            QTimer::singleShot(0, this, [this] {
                finish(-1, tr("Connection lost during replacement. Check both torrents on the server before resuming them."));
            });
        });
    }

    /** Проверяет метаданные и загружает актуальные настройки старой раздачи. */
    void begin(QByteArray metainfo, const tgui::RpcClient::AddTorrent& options, QSet<QString> skippedPaths)
    {
        m_bytes = std::move(metainfo);
        m_skippedPaths = std::move(skippedPaths);
        m_startNew = options.start;
        if (options.peerLimit > 0)
            m_settings.insert("peer-limit", options.peerLimit);
        if (m_rpc->supports(16))
            m_settings.insert("labels", QJsonArray::fromStringList(options.labels));
        if (m_rpc->supports(18))
            m_settings.insert("sequential_download", options.sequential);
        const auto preview = previewTorrentMetainfo(m_bytes);
        if (!preview) {
            finish(-1, tr("Could not read the file list from the new .torrent."));
            return;
        }
        for (const auto& file : preview->files)
            m_files.insert(file.name, file.size);

        m_progress = new QProgressDialog(tr("Comparing torrent contents…"), tr("Cancel"), 0, 0, m_window);
        m_progress->setWindowTitle(tr("Replace torrent"));
        m_progress->setWindowModality(Qt::WindowModal);
        m_progress->setMinimumDuration(0);
        connect(m_progress, &QProgressDialog::canceled, this, [this] { m_cancelled = true; });
        m_progress->show();
        m_rpc->getTorrentDetailsByHash(m_oldHash, [this](const auto& details) {
            if (m_cancelled) {
                finish(-1);
                return;
            }
            if (!details || details->status < 0 || checking(details->status) ||
                details->downloadDir.isEmpty() || details->files.isEmpty()) {
                finish(-1, tr("The old torrent must have metadata and must not be verifying."));
                return;
            }
            prepare(*details);
        });
    }

private:
    /** Проверяет совпадение файлов и переводит старую раздачу на паузу. */
    void prepare(const TorrentDetails& old)
    {
        bool hasData = false;
        for (const auto& file : old.files) {
            const auto match = m_files.constFind(file.name);
            if (match == m_files.cend() || match.value() != file.size) {
                finish(-1, tr("File paths or sizes differ. The old torrent was left unchanged."));
                return;
            }
            m_oldFiles.insert(file.name, file);
            hasData |= file.size > 0 && file.completion > 0;
        }
        if (!hasData || m_files.size() <= m_oldFiles.size()) {
            finish(-1, tr("The new .torrent must contain the old files and at least one new file. The old torrent must have some downloaded data."));
            return;
        }

        m_restartOld = old.status != static_cast<int>(TorrentStatus::Stopped);
        m_directory = old.downloadDir;
        m_name = old.name;

        m_progress->setLabelText(tr("Pausing the old torrent…"));
        m_rpc->torrentActionByHash("torrent-stop", m_oldHash, {}, [this](const JsonRpcResult& result) {
            if (!result.ok || m_cancelled) {
                restoreOld(result.ok ? QString() : result.error);
                return;
            }
            waitForOldToStop();
        });
    }

    /** Дожидается остановки демона, прежде чем новая запись сможет проверить те же данные. */
    void waitForOldToStop()
    {
        m_rpc->getTorrentDetailsByHash(m_oldHash, [this](const auto& details) {
            if (!details || details->status < 0 || m_cancelled) {
                restoreOld(m_cancelled ? QString() : tr("Could not confirm that the old torrent stopped."));
                return;
            }
            if (details->status == static_cast<int>(TorrentStatus::Stopped)) {
                if (!matchesOld(*details)) {
                    finish(-1, tr("The old torrent changed on the server. The new entry was not added."));
                    return;
                }
                for (const auto& file : details->files)
                    m_oldFiles.insert(file.name, file);
                addNew();
                return;
            }
            QTimer::singleShot(pollIntervalMs, this, [this] {
                if (m_rpc)
                    waitForOldToStop();
            });
        });
    }

    /** Добавляет неизменённые метаданные на паузе в каталог старой раздачи. */
    void addNew()
    {
        m_progress->setLabelText(tr("Adding the replacement torrent…"));
        tgui::RpcClient::AddTorrent request;
        request.source = tgui::RpcClient::AddTorrent::Source::Base64;
        request.metainfoBase64 = QString::fromLatin1(m_bytes.toBase64());
        request.downloadDir = m_directory;
        m_rpc->addTorrent(request, [this](const JsonRpcResult& result, const QByteArray&) {
            const auto added = result.arguments.value("torrent-added").toObject();
            m_newHash = added.value("hashString").toString(added.value("hash_string").toString());
            if (!result.ok || m_newHash.isEmpty() || m_newHash.compare(m_oldHash, Qt::CaseInsensitive) == 0) {
                if (result.arguments.value("torrent-duplicate").isObject() || result.status == "duplicate torrent") {
                    restoreOld(tr("This .torrent is already on the server."));
                } else {
                    finish(-1, tr("Could not confirm the new torrent. The old torrent is still listed. Check the server before resuming it."));
                }
                return;
            }
            m_verificationMayChangePaths = true;
            if (m_cancelled) {
                rollback();
                return;
            }
            m_progress->setLabelText(tr("Verifying downloaded files…"));
            m_rpc->torrentActionByHash("torrent-verify", m_newHash, {}, [this](const JsonRpcResult& verified) {
                if (verified.ok)
                    waitForVerification();
                else
                    rollback(verified.error);
            });
        });
    }

    /** Читает итог проверки с сервера, позволяя пользователю её отменить. */
    void waitForVerification()
    {
        if (m_cancelled) {
            rollback();
            return;
        }
        m_rpc->getTorrentDetailsByHash(m_newHash, [this](const auto& details) {
            if (!details || m_cancelled) {
                rollback(m_cancelled ? QString() : tr("Could not read the verification result."));
                return;
            }
            if (checking(details->status)) {
                if (details->recheckProgress)
                    m_progress->setLabelText(tr("Verifying downloaded files… %1%")
                        .arg(qRound(std::clamp(*details->recheckProgress, 0.0, 1.0) * 100)));
                QTimer::singleShot(pollIntervalMs, this, [this] {
                    if (m_rpc)
                        waitForVerification();
                });
                return;
            }
            if (details->status != static_cast<int>(TorrentStatus::Stopped) ||
                details->downloadDir != m_directory || details->files.size() != m_files.size()) {
                finish(-1, tr("The replacement torrent changed on the server. Both entries were kept."));
                return;
            }
            review(*details);
        });
    }

    /** Сопоставляет настройки по пути, а объём — по результату проверки на демоне. */
    void review(const TorrentDetails& details)
    {
        QJsonArray skipped, selected;
        double reused = 0, remaining = 0;
        int newFiles = 0, recoveredFiles = 0;
        for (const auto& file : details.files) {
            const auto expected = m_files.constFind(file.name);
            if (expected == m_files.cend() || expected.value() != file.size) {
                rollback(tr("Transmission returned different file paths or sizes."));
                return;
            }
            const auto old = m_oldFiles.constFind(file.name);
            const double verified = file.size * std::clamp(file.completion, 0.0, 1.0);
            if (m_skippedPaths.contains(file.name)) {
                skipped.append(file.index);
            } else {
                selected.append(file.index);
                remaining += file.size - verified;
            }
            if (old == m_oldFiles.cend()) {
                ++newFiles;
                continue;
            }
            if (old->completion > 0 && old->size > 0 && verified <= 0) {
                rollback(tr("An existing file failed verification: %1").arg(file.name));
                return;
            }
            recoveredFiles += verified > 0;
            reused += verified;
        }
        if (reused <= 0) {
            rollback(tr("Transmission did not verify any of the old data."));
            return;
        }

        if (!skipped.isEmpty())
            m_settings.insert("files-unwanted", skipped);
        if (!selected.isEmpty())
            m_settings.insert("files-wanted", selected);
        m_newId = details.id;
        m_verifiedFiles = details.files;
        m_progress->hide();
        const auto format = [](double bytes) { return QString::fromStdString(tgui::formatSize(bytes)); };
        auto* box = new QMessageBox(QMessageBox::Question, tr("Replace torrent"),
            tr("Old torrent: %1\nNew torrent: %2\nFolder: %3\n\nVerified data from %4 existing files: %5\nNew files: %6\nRemaining to download: %7\n\nReplace the old entry and keep its files?")
                .arg(m_name, details.name, m_directory, QString::number(recoveredFiles), format(reused),
                     QString::number(newFiles), format(remaining)),
            QMessageBox::Yes | QMessageBox::Cancel, m_window);
        box->setTextFormat(Qt::PlainText);
        box->setDefaultButton(QMessageBox::Cancel);
        box->setAttribute(Qt::WA_DeleteOnClose);
        m_confirmation = box;
        connect(box, &QMessageBox::finished, this, [this](int button) {
            if (!m_rpc)
                return;
            if (button == QMessageBox::Yes)
                commit();
            else
                rollback();
        });
        box->open();
    }

    /** Сохраняет настройки, убирает старую запись и запускает новую. */
    void commit()
    {
        m_rpc->getTorrentDetailsByHash(m_newHash, [this](const auto& details) {
            if (!details || details->status != static_cast<int>(TorrentStatus::Stopped) ||
                details->downloadDir != m_directory ||
                !std::ranges::equal(details->files, m_verifiedFiles, [](const auto& file, const auto& verified) {
                    return file.name == verified.name && file.size == verified.size &&
                           file.index == verified.index && file.completion == verified.completion;
                })) {
                finish(-1, tr("The replacement torrent changed on the server. Both entries were kept."));
                return;
            }
            applySettings();
        });
    }

    /** Сверяет старые пути перед добавлением и удалением. */
    bool matchesOld(const TorrentDetails& old) const
    {
        return old.downloadDir == m_directory && old.files.size() == m_oldFiles.size() &&
            std::all_of(old.files.cbegin(), old.files.cend(), [this](const auto& file) {
                const auto expected = m_oldFiles.constFind(file.name);
                return expected != m_oldFiles.cend() && expected->size == file.size;
            });
    }

    void applySettings()
    {
        m_rpc->torrentActionByHash("torrent-set", m_newHash, m_settings, [this](const JsonRpcResult& settings) {
            if (!settings.ok) {
                rollback(settings.error);
                return;
            }
            m_rpc->getTorrentDetailsByHash(m_oldHash, [this](const auto& old) {
                if (!old || old->status != static_cast<int>(TorrentStatus::Stopped) || !matchesOld(*old)) {
                    finish(-1, tr("The old torrent changed on the server. The new entry was kept."));
                    return;
                }
                m_rpc->torrentActionByHash("torrent-remove", m_oldHash, {{"delete-local-data", false}},
                    [this](const JsonRpcResult& removed) {
                        if (!removed.ok) {
                            finish(-1, tr("Could not confirm removal of the old entry. The new torrent was left paused."));
                            return;
                        }
                        if (!m_startNew) {
                            finish(m_newId);
                            return;
                        }
                        m_rpc->torrentActionByHash("torrent-start", m_newHash, {}, [this](const JsonRpcResult& started) {
                            finish(m_newId, started.ok ? QString() :
                                tr("The torrent was replaced, but automatic start failed: %1").arg(started.error));
                        });
                    });
            });
        });
    }

    /** Убирает только созданную операцией запись, сохраняя данные. */
    void rollback(const QString& error = {})
    {
        m_progress->hide();
        m_rpc->torrentActionByHash("torrent-remove", m_newHash, {{"delete-local-data", false}},
            [this, error](const JsonRpcResult& removed) {
                if (!removed.ok) {
                    finish(-1, tr("Could not remove the new entry. Check both torrents on the server before resuming them."));
                    return;
                }
                restoreOld(error);
            });
    }

    /** Перепроверяет старые пути после возможного переименования Transmission в .part. */
    void restoreOld(const QString& error)
    {
        if (!m_verificationMayChangePaths) {
            resumeOld(error);
            return;
        }
        m_rpc->torrentActionByHash("torrent-verify", m_oldHash, {}, [this, error](const JsonRpcResult& result) {
            if (result.ok)
                resumeOld(error);
            else
                finish(-1, tr("Could not verify the old torrent after cancellation: %1").arg(result.error));
        });
    }

    /** Возвращает старой раздаче исходное состояние запуска. */
    void resumeOld(const QString& error)
    {
        if (!m_restartOld) {
            finish(-1, error);
            return;
        }
        m_rpc->torrentActionByHash("torrent-start", m_oldHash, {}, [this, error](const JsonRpcResult& result) {
            finish(-1, result.ok ? error : tr("Could not resume the old torrent: %1").arg(result.error));
        });
    }

    /** Закрывает диалоги и возвращает управление очереди добавления. */
    void finish(int id, const QString& error = {})
    {
        if (m_confirmation) {
            m_confirmation->disconnect(this);
            m_confirmation->close();
        }
        if (m_progress) {
            m_progress->hide();
            m_progress->deleteLater();
        }
        if (!error.isEmpty()) {
            auto* box = new QMessageBox(QMessageBox::Warning, tr("Replace torrent"), error,
                                        QMessageBox::Ok, m_window);
            box->setTextFormat(Qt::PlainText);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        }
        if (m_rpc)
            disconnect(m_rpc, nullptr, this, nullptr);
        m_callback(id);
        deleteLater();
    }

    QWidget* m_window;
    QPointer<tgui::RpcClient> m_rpc;
    QPointer<QProgressDialog> m_progress;
    QPointer<QMessageBox> m_confirmation;
    QString m_oldHash;
    QString m_newHash;
    QString m_directory;
    QString m_name;
    QByteArray m_bytes;
    QHash<QString, double> m_files;
    QSet<QString> m_skippedPaths;
    QHash<QString, tgui::FileStat> m_oldFiles;
    QVector<tgui::FileStat> m_verifiedFiles;
    QJsonObject m_settings;
    std::function<void(int)> m_callback;
    int m_newId = -1;
    bool m_restartOld = false;
    bool m_startNew = false;
    bool m_cancelled = false;
    bool m_verificationMayChangePaths = false;
};

} // namespace

void replaceTorrent(QWidget* window, tgui::RpcClient* rpc, const QString& oldHash,
                    QByteArray metainfo, const tgui::RpcClient::AddTorrent& options,
                    QSet<QString> skippedPaths, std::function<void(int)> callback)
{
    auto* operation = new TorrentReplacement(window, rpc, oldHash, std::move(callback));
    operation->begin(std::move(metainfo), options, std::move(skippedPaths));
}
