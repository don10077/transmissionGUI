#pragma once

#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include "tgui/models.h"

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

/** Return the configured default for a torrent with exactly one known file. */
inline bool automaticSequential(const QVector<FileStat>& files, int rpcVersion)
{
    const QSettings settings;
    if (rpcVersion < 18 || files.size() != 1 ||
        !settings.value("downloads/autoSequential", false).toBool())
        return false;
    const auto extensions = normalizedSequentialExtensions(settings.value(
        "downloads/sequentialExtensions", defaultSequentialExtensions()).toString());
    return extensions.contains(QFileInfo(files.constFirst().name).suffix().toLower());
}
} // namespace tgui
