#pragma once

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>

#include "tgui/models.h"

namespace tgui {

/** Проверяет, может ли открытие @p file запустить программу, скрипт или ярлык. */
inline bool isPotentiallyExecutable(const QFileInfo& file)
{
    const auto suffix = file.suffix().toLower();
    static const QStringList extensions{"exe", "com", "bat", "cmd", "lnk", "scr", "hta", "msi",
                                        "ps1", "vbs", "js", "jse", "wsf", "cpl", "reg", "url"};
    if (!file.isDir() && extensions.contains(suffix))
        return true;
#ifdef Q_OS_WIN
    return !file.isDir() && !suffix.isEmpty() &&
        qEnvironmentVariable("PATHEXT").split(';').contains(QStringLiteral(".") + suffix, Qt::CaseInsensitive);
#else
    return suffix == "app" || (!file.isDir() && (file.isExecutable() || suffix == "sh" || suffix == "desktop"));
#endif
}

// Родительский каталог для пути с завершающим '/'.
// "a/b/" → "a/", "a/" → "", "/" → "".
inline QString parentFileDirectory(const QString& dir)
{
    QString path = dir;
    if (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.left(slash + 1) : QString();
}

// Каталог, общий для всех файлов торрента (аналог GetFilesCommonPath в transgui).
// Возвращает путь с завершающим '/' или пустую строку, если общего каталога нет
// (файлы лежат прямо в downloadDir).
inline QString commonFileDirectory(const QVector<FileStat>& files)
{
    QString result;
    bool first = true;
    for (const FileStat& file : files) {
        const QString name = QString(file.name).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const int slash = name.lastIndexOf(QLatin1Char('/'));
        const QString dir = slash >= 0 ? name.left(slash + 1) : QString();
        if (first) {
            first = false;
            result = dir;
            if (result.isEmpty())
                return {};
            continue;
        }
        while (!result.isEmpty() && !name.startsWith(result))
            result = parentFileDirectory(result);
        if (result.isEmpty())
            return {};
    }
    return result;
}

// Относительный путь объекта торрента для открытия (аналог OpenCurrentTorrent):
// один файл — сам файл, несколько — общий каталог файлов.
// containingFolder=true — показать файл в его каталоге (selectFile для одного файла).
inline QString torrentOpenRelativePath(const QVector<FileStat>& files, bool containingFolder,
                                       bool* selectFile)
{
    if (selectFile)
        *selectFile = false;
    if (files.isEmpty())
        return {};
    if (files.size() == 1) {
        if (selectFile && containingFolder)
            *selectFile = true;
        return QString(files.first().name).replace(QLatin1Char('\\'), QLatin1Char('/'));
    }
    return commonFileDirectory(files);
}

}  // namespace tgui
