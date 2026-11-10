#pragma once

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include "tgui/bencode.h"

namespace tgui {
inline QString defaultSequentialExtensions()
{
    return QStringLiteral("mkv mp4 avi mov m4v webm mpg mpeg ts m2ts wmv flv\n"
                          "mp3 flac m4a aac ogg opus wav wma aiff ape");
}

inline QStringList normalizedSequentialExtensions(const QString& text)
{
    QStringList result;
    for (QString extension : text.toLower().split(QRegularExpression("[\\s,;]+"), Qt::SkipEmptyParts)) {
        while (extension.startsWith('.') || extension.startsWith('*')) extension.remove(0, 1);
        if (!extension.isEmpty() && !result.contains(extension)) result.append(extension);
    }
    return result;
}

/** Read file metadata only; never infer a single file from a magnet display name. */
inline bool automaticSequential(const QString& torrentFile, int rpcVersion)
{
    if (rpcVersion < 18 || !QSettings().value("downloads/autoSequential", false).toBool()) return false;
    QFile file(torrentFile);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) return false;
    bencode::Value root;
    if (!bencode::decode(file.readAll().toStdString(), root)) return false;
    const auto* info = root.find("info");
    if (!info) return false;
    using Value = bencode::Value;
    QString name;
    const auto* files = info->find("files");
    if (files) {
        if (files->type() != Value::Type::List || files->asList().size() != 1) return false;
        const auto& entry = files->asList().front();
        const auto* path = entry.find("path.utf-8");
        if (!path) path = entry.find("path");
        if (!path || path->type() != Value::Type::List || path->asList().empty()) return false;
        const auto& leaf = path->asList().back();
        if (leaf.type() != Value::Type::String) return false;
        name = QString::fromUtf8(leaf.toString().data(), qsizetype(leaf.toString().size()));
    } else if (const auto* tree = info->find("file tree")) {
        int count = 0;
        auto visit = [&](auto&& self, const Value& node, const QString& path, int depth) -> void {
            if (depth > 64 || count > 1 || node.type() != Value::Type::Dictionary) { count = 2; return; }
            for (const auto& [key, child] : node.asDict()) {
                if (key.empty()) { ++count; name = path; }
                else self(self, child, QString::fromUtf8(key.data(), qsizetype(key.size())), depth + 1);
                if (count > 1) return;
            }
        };
        visit(visit, *tree, QString(), 0);
        if (count != 1) return false;
    } else {
        if (!info->find("length")) return false;
        const auto* value = info->find("name.utf-8");
        if (!value) value = info->find("name");
        if (!value || value->type() != Value::Type::String) return false;
        name = QString::fromUtf8(value->toString().data(), qsizetype(value->toString().size()));
    }
    const auto extensions = normalizedSequentialExtensions(QSettings().value(
        "downloads/sequentialExtensions", defaultSequentialExtensions()).toString());
    return extensions.contains(QFileInfo(name).suffix().toLower());
}
} // namespace tgui
