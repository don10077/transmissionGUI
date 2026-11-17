// TST-03: регистрация ассоциаций .torrent/magnet в параметризованном корне реестра.
#include <gtest/gtest.h>

// Qt-заголовок до #ifdef Q_OS_WIN: макрос определяет qglobal.
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <qt_windows.h>

#include <QCoreApplication>
#include <QScopeGuard>

#include "assoc.h"

namespace {
// Отдельный ключ вместо реального HKCU\Software\Classes.
QString isolatedRoot()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\tgui_assoc_test_")
        + QString::number(QCoreApplication::applicationPid());
}

void removeTestKey()
{
    const QString path = QStringLiteral("Software\\tgui_assoc_test_")
        + QString::number(QCoreApplication::applicationPid());
    RegDeleteTreeW(HKEY_CURRENT_USER, reinterpret_cast<const wchar_t*>(path.utf16()));
}
} // namespace

TEST(WindowsAssociations, RegisterAndUnregisterIntoIsolatedRoot)
{
    const QString root = isolatedRoot();
    const auto cleanup = qScopeGuard([] { removeTestKey(); });
    EXPECT_FALSE(torrentHandlerRegistered(root));
    EXPECT_FALSE(magnetHandlerRegistered(root));

    ASSERT_TRUE(applyTorrentHandlers(true, true, root, root));
    EXPECT_TRUE(torrentHandlerRegistered(root));
    EXPECT_TRUE(magnetHandlerRegistered(root));

    ASSERT_TRUE(applyTorrentHandlers(false, false, root, root));
    EXPECT_FALSE(torrentHandlerRegistered(root));
    EXPECT_FALSE(magnetHandlerRegistered(root));
}
#else
TEST(WindowsAssociations, NotAvailableOnThisPlatform)
{
    GTEST_SKIP() << "ассоциации регистрируются только в Windows";
}
#endif // Q_OS_WIN