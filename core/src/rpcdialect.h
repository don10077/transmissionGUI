#pragma once

#include <QJsonObject>
#include <QString>

namespace tgui::rpcdialect {

/** Преобразует имя метода или параметра Tr4 в имя Tr5. */
QString modernKey(const QString& key);

/** Преобразует параметры запроса Tr4 в параметры Tr5. */
QJsonObject modernParams(const QString& method, const QJsonObject& params);

/** Преобразует параметры Tr5-стиля обратно в имена legacy RPC (Tr4). */
QJsonObject legacyParams(const QString& method, const QJsonObject& params);

/** Нормализует результат Tr5 для моделей, использующих имена Tr4. */
QJsonObject legacyResult(const QString& method, const QJsonObject& result);

} // namespace tgui::rpcdialect
