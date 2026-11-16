#pragma once

#include <QSet>

#include <functional>

#include "tgui/rpcclient.h"

class QWidget;

/** Читает список файлов .torrent до добавления. При ремонте обновляет метаданные в памяти. */
std::optional<tgui::TorrentDetails> previewTorrentMetainfo(QByteArray& bytes);

/** Заменяет раздачу на том же демоне, пропуская выбранные пути. callback получает новый ID или -1. */
void replaceTorrent(QWidget* window, tgui::RpcClient* rpc, const QString& oldHash,
                    QByteArray metainfo, const tgui::RpcClient::AddTorrent& options,
                    QSet<QString> skippedPaths, std::function<void(int)> callback);
