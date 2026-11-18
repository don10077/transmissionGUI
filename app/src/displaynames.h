#pragma once

#include <QString>

#include "tgui/models.h"

/** Локальные отображаемые имена торрентов (не часть RPC Transmission).
 *
 * Имя привязано к info-hash и хранится только в настройках клиента: файлы на
 * диске, .torrent и данные демона не меняются. Пустое значение означает
 * «показывать имя из метаданных».
 */
namespace displaynames {

inline constexpr int maxLength = 255;
inline constexpr char settingsPrefix[] = "displayNames/";

/** Убирает пробелы по краям и ограничивает длину. */
QString normalize(const QString& name);

/** Возвращает сохранённое имя для hash или пустую строку. */
QString storedName(const QString& hash);

/** Сохраняет имя; пустое значение удаляет запись. Возвращает true при изменении. */
bool set(const QString& hash, const QString& name);

/** Удаляет сохранённое имя. Возвращает true, если запись была. */
bool remove(const QString& hash);

/** Заполняет displayName торрента из настроек. */
void apply(tgui::Torrent& torrent);

/** Имя для показа: displayName, иначе имя из метаданных. */
QString effectiveName(const tgui::Torrent& torrent);

} // namespace displaynames
