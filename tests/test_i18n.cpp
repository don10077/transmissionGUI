// W06: проверка конвейера переводов.
//  - без переводчика — английский (source) — это и есть fallback;
//  - после загрузки .qm строка переводится.
// Не закладываем «железный» русский в тесты (см. WORKFLOW.md, раздел 0.3).
#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QTranslator>

TEST(I18n, FallbackEnglishThenQmTranslates)
{
    const char* kContext = "MainWindow";
    const char* kSource = "Force start"; // есть перевод в tgui_ru.ts (контекст MainWindow)

    // 1) Fallback: без установленного переводчика translate() возвращает исходный текст.
    EXPECT_EQ(QCoreApplication::translate(kContext, kSource, nullptr),
              QStringLiteral("Force start"));

    // 2) Загрузили .qm — строка изменилась (конкретный русский здесь не фиксируем).
    QTranslator translator;
    ASSERT_TRUE(translator.load(QStringLiteral(TGUI_QM_PATH)))
        << "не удалось загрузить .qm: " << QStringLiteral(TGUI_QM_PATH).toStdString();
    QApplication::instance()->installTranslator(&translator);
    const QString translated = QCoreApplication::translate(kContext, kSource, nullptr);
    EXPECT_NE(translated, QStringLiteral("Force start"));
}