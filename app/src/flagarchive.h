#pragma once

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <archive.h>
#include <archive_entry.h>
#include <memory>

namespace flagarchive {

inline constexpr int maxArchiveBytes = 2 * 1024 * 1024;

/** Проверяет ZIP и публикует каталог флагов после распаковки всех изображений. */
inline bool extract(const QByteArray& zip, const QString& directory)
{
    if (zip.isEmpty() || zip.size() > maxArchiveBytes)
        return false;

    const QFileInfo destination(directory);
    QDir parent(destination.absolutePath());
    if (!QDir().mkpath(parent.absolutePath()))
        return false;
    QTemporaryDir stage(parent.filePath("country-flags-XXXXXX"));
    if (!stage.isValid())
        return false;

    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader || archive_read_support_format_zip_seekable(reader.get()) != ARCHIVE_OK ||
        archive_read_open_memory(reader.get(), zip.constData(), static_cast<size_t>(zip.size())) != ARCHIVE_OK)
        return false;

    const QRegularExpression namePattern(QStringLiteral("^[a-z0-9]{2}\\.png\\z"));
    int count = 0;
    archive_entry* entry = nullptr;
    int status = 0;
    while ((status = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const auto name = QString::fromUtf8(archive_entry_pathname(entry));
        const auto size = archive_entry_size(entry);
        if (++count > 512 || !namePattern.match(name).hasMatch() ||
            archive_entry_filetype(entry) != AE_IFREG || size <= 0 || size > 8192)
            return false;

        QByteArray png(static_cast<qsizetype>(size), Qt::Uninitialized);
        qsizetype offset = 0;
        while (offset < png.size()) {
            const auto read = archive_read_data(reader.get(), png.data() + offset,
                static_cast<size_t>(png.size() - offset));
            if (read <= 0)
                return false;
            offset += read;
        }
        // Проверяем CRC и длину записи до публикации файла.
        char extra = 0;
        if (archive_read_data(reader.get(), &extra, 1) != 0)
            return false;
        QBuffer imageData(&png);
        imageData.open(QIODevice::ReadOnly);
        QImageReader image(&imageData, "png");
        const auto dimensions = image.size();
        if (dimensions.isEmpty() || dimensions.width() > 128 || dimensions.height() > 128 ||
            image.read().isNull())
            return false;
        QFile flag(stage.filePath(name));
        if (!flag.open(QIODevice::WriteOnly | QIODevice::NewOnly) || flag.write(png) != png.size() ||
            !flag.flush())
            return false;
    }
    if (status != ARCHIVE_EOF || count == 0)
        return false;

    // Старый каталог может быть непустым (чужеродные файлы): rmdir его не
    // удалит и обновление флагов заблокируется навсегда (ARCH-08). Поэтому —
    // отодвигаем старый в .old-<stamp>, переключаемся на новый и удаляем
    // старый; при неудаче переименования восстанавливаем прежний.
    QString oldBackup;
    if (destination.exists()) {
        oldBackup = parent.filePath(destination.fileName() + "-old-"
            + QString::number(QDateTime::currentMSecsSinceEpoch()));
        if (QFileInfo(oldBackup).exists() || !parent.rename(destination.fileName(), oldBackup))
            return false;
    }
    if (!parent.rename(stage.path(), directory)) {
        if (!oldBackup.isEmpty())
            parent.rename(oldBackup, directory);
        return false;
    }
    if (!oldBackup.isEmpty())
        QDir(oldBackup).removeRecursively();
    return true;
}
}
