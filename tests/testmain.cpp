// Точка входа тестов: QApplication нужен QNetworkAccessManager/QTcpServer/QtWidgets.
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Без имени организации QSettings на Windows возвращает AccessError,
    // поэтому профили не сохранялись бы в изолированном прогоне.
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui_tests"));
    const QTemporaryDir settings;
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings.path());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
