#pragma once

#include <QString>

#ifdef Q_OS_WIN
// Регистрация ассоциаций .torrent/magnet для текущего пользователя.
// TST-03: корни реестра параметризованы, чтобы тесты писали в отдельный ключ.

/** Корень классов по умолчанию (HKCU\Software\Classes). */
QString defaultClassesRoot();
/** Корень ключей приложения по умолчанию (HKCU\Software). */
QString defaultSoftwareRoot();
/** Зарегистрирован ли клиент как обработчик .torrent-файлов. */
bool torrentHandlerRegistered(const QString& classesRoot = defaultClassesRoot());
/** Зарегистрирован ли клиент как обработчик magnet-ссылок. */
bool magnetHandlerRegistered(const QString& classesRoot = defaultClassesRoot());
/** Регистрирует или снимает обработчики .torrent/magnet. false — ошибка реестра. */
bool applyTorrentHandlers(bool torrent, bool magnet,
                          const QString& classesRoot = defaultClassesRoot(),
                          const QString& softwareRoot = defaultSoftwareRoot());
#endif // Q_OS_WIN