#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace secretstore {

/** Доступна ли защита сохранённых секретов для текущего пользователя. */
bool available();

/** Шифрует @p secret. Пустой результат означает ошибку. */
std::optional<QByteArray> protect(const QString& secret);

/** Расшифровывает @p blob под учётной записью текущего пользователя. */
std::optional<QString> unprotect(const QByteArray& blob);

}
