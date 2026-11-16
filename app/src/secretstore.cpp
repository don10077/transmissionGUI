#include "secretstore.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>

#include <limits>
#include <memory>
#endif

namespace secretstore {

bool available()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

std::optional<QByteArray> protect(const QString& secret)
{
#ifdef Q_OS_WIN
    auto bytes = secret.toUtf8();
    if (bytes.size() > std::numeric_limits<DWORD>::max())
        return std::nullopt;
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB output{};
    const auto success = CryptProtectData(&input, L"transmissionGUI", nullptr, nullptr, nullptr,
                                          CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(bytes.data(), static_cast<SIZE_T>(bytes.size()));
    if (!success)
        return std::nullopt;
    const std::unique_ptr<BYTE, decltype(&LocalFree)> data(output.pbData, &LocalFree);
    return QByteArray(reinterpret_cast<const char*>(data.get()), output.cbData);
#else
    Q_UNUSED(secret);
    return std::nullopt;
#endif
}

std::optional<QString> unprotect(const QByteArray& blob)
{
#ifdef Q_OS_WIN
    if (blob.isEmpty() || blob.size() > std::numeric_limits<DWORD>::max())
        return std::nullopt;
    DATA_BLOB input{static_cast<DWORD>(blob.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output))
        return std::nullopt;
    const std::unique_ptr<BYTE, decltype(&LocalFree)> data(output.pbData, &LocalFree);
    auto secret = QString::fromUtf8(reinterpret_cast<const char*>(data.get()), output.cbData);
    SecureZeroMemory(data.get(), output.cbData);
    return secret;
#else
    Q_UNUSED(blob);
    return std::nullopt;
#endif
}

}
