#include "clipboardmonitor.h"

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>

#include <algorithm>
#include <utility>

#include "externalopen.h"
#include "optionsdialog.h"
#include "torrentmodel.h"

ClipboardMonitor::ClipboardMonitor(QWidget& window, const TorrentModel& model, Client client, Add add)
    : m_window(window), m_model(model), m_client(std::move(client)), m_add(std::move(add))
{
    connect(QApplication::clipboard(), &QClipboard::dataChanged,
            this, &ClipboardMonitor::onClipboardChanged);
}

/** Добавляет новую ссылку из буфера при включённой настройке и активном подключении. */
void ClipboardMonitor::onClipboardChanged()
{
    auto* client = m_client();
    if (!client || !QSettings().value(OptionsDialog::clipboardLinksKey, false).toBool())
        return;

    const auto input = torrentLinkInput(QApplication::clipboard()->text());
    if (input.isEmpty())
        return;
    QUrl url(input, QUrl::StrictMode);
    url.setFragment(QString());
    const auto magnet = magnetIdentifier(url);
    if (magnet.isEmpty() && ((url.scheme() != "http" && url.scheme() != "https") ||
                            url.host().isEmpty() || !url.path().endsWith(".torrent", Qt::CaseInsensitive)))
        return;
    const auto link = url.toString(QUrl::FullyEncoded);
    const auto key = magnet.isEmpty() ? link : magnet;
    if (m_clipboardLinksSeen.removeOne(key)) {
        m_clipboardLinksSeen.append(key);
        return;
    }
    const auto& torrents = m_model.torrents();
    if (!magnet.isEmpty() && std::any_of(torrents.cbegin(), torrents.cend(),
            [&magnet](const auto& torrent) {
                return magnet == QStringLiteral("urn:btih:") + torrent.hashString.value_or(QString()).toLower() ||
                    magnet == magnetIdentifier(QUrl(torrent.magnetLink.value_or(QString())));
            }))
        return;
    m_clipboardLinksSeen.append(key);
    if (m_clipboardLinksSeen.size() > 256)
        m_clipboardLinksSeen.removeFirst();

    tgui::RpcClient::AddTorrent params;
    params.source = tgui::RpcClient::AddTorrent::Source::Url;
    params.url = link;
    params.downloadDir = client->sessionInfo().downloadDir;
    params.start = client->sessionInfo().startAddedTorrents;
    if (!magnet.isEmpty()) {
        m_add(params);
        return;
    }
    const QPointer<tgui::RpcClient> rpc(client);
    auto* confirm = new QMessageBox(QMessageBox::Question, tr("Add torrent link"),
        tr("Add the torrent link copied from %1?").arg(url.host()), QMessageBox::Yes | QMessageBox::No, &m_window);
    confirm->setTextFormat(Qt::PlainText);
    confirm->setDefaultButton(QMessageBox::No);
    confirm->setAttribute(Qt::WA_DeleteOnClose);
    connect(rpc.data(), &QObject::destroyed, confirm, &QDialog::reject);
    connect(confirm, &QDialog::finished, this, [this, rpc, params](int result) {
        if (result == QMessageBox::Yes && rpc && rpc == m_client())
            m_add(params);
    });
    confirm->open();
}
