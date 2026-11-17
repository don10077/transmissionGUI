// W06: проверка конвейера переводов.
//  - без переводчика — английский (source) — это и есть fallback;
//  - после загрузки .qm строка переводится.
// Не закладываем «железный» русский в тесты.
//
// I18N-01: файл translations/tgui_ru.ts обязан быть полным и согласованным:
//  - нет записей type="unfinished"/"vanished"/"obsolete";
//  - каждый перевод заполнен (включая формы множественного числа);
//  - наборы подстановок %1..%N и %n совпадают с исходной строкой;
//  - нет дублей (контекст + исходный текст + комментарий).
#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTranslator>
#include <QXmlStreamReader>

namespace {

/// Имена подстановок (%1, %n, %L1), встречающихся в тексте.
QSet<QString> placeholders(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("%(\\d+|n|L\\d+)"));
    QSet<QString> names;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        names.insert(it.next().captured(1));
    return names;
}

QString sorted(const QSet<QString>& values)
{
    QStringList list(values.begin(), values.end());
    list.sort();
    return list.join(QStringLiteral(", "));
}

void checkPlaceholders(const QString& context, const QString& source, const QString& translation,
                       QStringList& problems)
{
    const auto expected = placeholders(source);
    const auto actual = placeholders(translation);
    if (expected != actual)
        problems << QStringLiteral("[%1] %2: подстановки %3 != %4")
                        .arg(context, source, sorted(expected), sorted(actual));
}

} // namespace

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
    // Убираем переводчик, чтобы состояние не перетекало в следующие тесты.
    QApplication::instance()->removeTranslator(&translator);
}

TEST(I18n, TranslationFileIsComplete)
{
    QFile file(QStringLiteral(TGUI_TS_PATH));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << qPrintable(file.fileName());
    QXmlStreamReader xml(&file);

    QStringList problems;
    QSet<QString> seen; // контекст + исходный текст + комментарий
    int messages = 0;

    ASSERT_TRUE(xml.readNextStartElement());
    ASSERT_EQ(xml.name(), QLatin1String("TS"));

    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("context")) {
            xml.skipCurrentElement();
            continue;
        }
        QString context;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("name")) {
                context = xml.readElementText();
                continue;
            }
            if (xml.name() != QLatin1String("message")) {
                xml.skipCurrentElement();
                continue;
            }

            QString source;
            QString comment;
            QString translation;
            QStringList numerusForms;
            QString translationType;
            bool hasTranslation = false;
            bool numerus = false;

            // Qt помечает плюральные записи атрибутом на <message>
            // (формат, который понимает lrelease этой сборки Qt).
            if (xml.attributes().value(QLatin1String("numerus")).toString() == QLatin1String("yes"))
                numerus = true;

            while (xml.readNextStartElement()) {
                const auto name = xml.name();
                if (name == QLatin1String("source")) {
                    source = xml.readElementText();
                } else if (name == QLatin1String("comment")) {
                    comment = xml.readElementText();
                } else if (name == QLatin1String("numerus")) {
                    numerus = xml.readElementText() == QLatin1String("yes");
                } else if (name == QLatin1String("translation")) {
                    hasTranslation = true;
                    translationType = xml.attributes().value(QLatin1String("type")).toString();
                    // Читаем содержимое вручную: текст перевода либо формы множественного числа.
                    while (xml.readNext() != QXmlStreamReader::EndElement) {
                        if (xml.isCharacters()) {
                            translation += xml.text();
                        } else if (xml.isStartElement()) {
                            if (xml.name() == QLatin1String("numerusform"))
                                numerusForms << xml.readElementText();
                            else
                                xml.skipCurrentElement();
                        }
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }

            ++messages;
            const QString key = context + QLatin1Char('\n') + source + QLatin1Char('\n') + comment;
            if (seen.contains(key))
                problems << QStringLiteral("[%1] %2: дубль записи").arg(context, source);
            seen.insert(key);

            if (source.isEmpty())
                problems << QStringLiteral("[%1] пустой исходный текст").arg(context);

            if (!hasTranslation) {
                problems << QStringLiteral("[%1] %2: нет <translation>").arg(context, source);
                continue;
            }
            if (!translationType.isEmpty())
                problems << QStringLiteral("[%1] %2: type=\"%3\"")
                                .arg(context, source, translationType);

            if (!numerus) {
                if (translation.isEmpty()) {
                    problems << QStringLiteral("[%1] %2: пустой перевод").arg(context, source);
                    continue;
                }
                checkPlaceholders(context, source, translation, problems);
                continue;
            }

            if (numerusForms.isEmpty()) {
                problems << QStringLiteral("[%1] %2: нет <numerusform>").arg(context, source);
                continue;
            }
            for (const auto& form : numerusForms) {
                if (form.isEmpty())
                    problems << QStringLiteral("[%1] %2: пустая форма множественного числа")
                                    .arg(context, source);
                else
                    checkPlaceholders(context, source, form, problems);
            }
        }
    }

    ASSERT_FALSE(xml.hasError())
        << qPrintable(xml.errorString()) << " (строка " << xml.lineNumber() << ')';
    EXPECT_GT(messages, 100) << "файл переводов выглядит пустым";
    EXPECT_TRUE(problems.isEmpty())
        << "некорректные записи в " << TGUI_TS_PATH << ":\n"
        << problems.join(QLatin1Char('\n')).toStdString();
}
