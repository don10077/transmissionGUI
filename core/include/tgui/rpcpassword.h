#pragma once

#include <QFile>
#include <QString>
#include <cstdio>
#include <optional>

namespace tgui {

/** Читает пароль из явно выбранного источника или из окружения. */
inline std::optional<QString> startupRpcPassword(const QString& explicitPassword, bool readStdin)
{
    if (readStdin) {
        QFile input;
        if (!input.open(stdin, QIODevice::ReadOnly))
            return std::nullopt;
        constexpr qint64 limit = 64 * 1024;
        auto line = input.readLine(limit + 2);
        if (line.isEmpty() || line.size() > limit + 1)
            return std::nullopt;
        if (line.endsWith('\n')) {
            line.chop(1);
            if (line.endsWith('\r'))
                line.chop(1);
        }
        if (line.size() > limit)
            return std::nullopt;
        return QString::fromUtf8(line);
    }
    if (!explicitPassword.isNull())
        return explicitPassword;
    if (qEnvironmentVariableIsSet("TGUI_RPC_PASSWORD"))
        return qEnvironmentVariable("TGUI_RPC_PASSWORD");
    return std::nullopt;
}

}
