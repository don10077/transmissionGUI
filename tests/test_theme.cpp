// Проверяет отрисовку ключевых экранов с палитрой приложения.
#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFont>
#include <QHeaderView>
#include <QImage>
#include <QLineEdit>
#include <QMenuBar>
#include <QPalette>
#include <QScopeGuard>
#include <QSettings>
#include <QStyleHints>
#include <QTableView>
#include <QTimer>
#include <QTranslator>
#include <QTreeView>
#include <QUrl>
#include <QWindow>

#include "aboutdialog.h"
#include "connectdialog.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "propsform.h"
#include "qostream.h"
#include "theme.h"

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#endif
#include "tgui/models.h"

namespace {

void spinEvents(int ms = 300)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/** Показывает виджет, обрабатывает события и возвращает его снимок. */
QImage grabWidget(QWidget& w)
{
    w.show();
    spinEvents();
    const QImage shot = w.grab().toImage();
    w.close();
    return shot;
}

} // namespace

// Тема меняет глобальные стиль/палитру/шрифт приложения, поэтому каждая
// проверка отрисовки применяет её в SetUp и возвращает исходное состояние
// в TearDown — тесты не зависят от порядка запуска.
class ThemeRendering : public ::testing::Test {
protected:
    void SetUp() override
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        m_colorScheme = qApp->styleHints()->colorScheme();
#endif
        m_styleSheet = qApp->styleSheet();
        m_palette = qApp->palette();
        m_font = qApp->font();
        tgui::theme::apply(*qApp);
    }

    void TearDown() override
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        qApp->styleHints()->setColorScheme(m_colorScheme);
#endif
        qApp->setStyleSheet(m_styleSheet);
        qApp->setPalette(m_palette);
        qApp->setFont(m_font);
    }

private:
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    Qt::ColorScheme m_colorScheme = Qt::ColorScheme::Unknown;
#endif
    QString m_styleSheet;
    QPalette m_palette;
    QFont m_font;
};

TEST_F(ThemeRendering, InitialColumnWidthsFitLocalizedHeaders)
{
    QTranslator translator;
    ASSERT_TRUE(translator.load(QStringLiteral(TGUI_QM_PATH)));
    ASSERT_TRUE(qApp->installTranslator(&translator));
    const auto restoreTranslator = qScopeGuard([&] { qApp->removeTranslator(&translator); });
    QFont font = qApp->font();
    font.setPointSize(14);
    qApp->setFont(font);

    const auto widthKey = [](int column) { return QStringLiteral("columns/width_%1").arg(column); };
    QSettings settings;
    settings.setValue(widthKey(TorrentModel::ColRateUp), 78);
    settings.setValue(widthKey(TorrentModel::ColName), 485);
    const auto restoreWidths = qScopeGuard([&] {
        settings.remove(widthKey(TorrentModel::ColRateUp));
        settings.remove(widthKey(TorrentModel::ColName));
    });

    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto* torrentHeader = window.torrentView()->header();
    auto* peers = window.findChild<QTableView*>("peersView");
    ASSERT_NE(peers, nullptr);
    EXPECT_GT(torrentHeader->sectionSizeHint(TorrentModel::ColRateDown), 78);
    EXPECT_GT(peers->horizontalHeader()->sectionSizeHint(PeerListModel::ColRateUp), 85);
    for (auto* header : {torrentHeader, peers->horizontalHeader()}) {
        for (int column = 0; column < header->count(); ++column) {
            if (!header->isSectionHidden(column)) {
                EXPECT_GE(header->sectionSize(column), header->sectionSizeHint(column)) << column;
            }
        }
    }
    EXPECT_EQ(torrentHeader->sectionSize(TorrentModel::ColName), 485);
    torrentHeader->resizeSection(TorrentModel::ColRateUp, 78);
    EXPECT_EQ(torrentHeader->sectionSize(TorrentModel::ColRateUp), 78);
}

TEST_F(ThemeRendering, IconRasterUsesRequestedSizeAndDisabledOpacity)
{
    const auto icon = tgui::theme::icon(tgui::theme::Icon::Folder);
    const QSize size(19, 19);
    const qreal scale = 1.5;
    const auto normal = icon.pixmap(size, scale, QIcon::Normal);
    const auto disabled = icon.pixmap(size, scale, QIcon::Disabled);
    ASSERT_FALSE(normal.isNull());
    ASSERT_FALSE(disabled.isNull());
    EXPECT_EQ(normal.size(), size * scale);
    EXPECT_EQ(normal.devicePixelRatio(), scale);
    EXPECT_EQ(disabled.size(), normal.size());
    const auto maxAlpha = [](const QPixmap& pixmap) {
        int alpha = 0;
        const QImage image = pixmap.toImage();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                alpha = qMax(alpha, qAlpha(image.pixel(x, y)));
        return alpha;
    };
    EXPECT_GT(maxAlpha(disabled), 0);
    EXPECT_LT(maxAlpha(disabled), maxAlpha(normal));
}

TEST_F(ThemeRendering, DarkSchemeSwitchesPaletteAndQss)
{
    const int lightWindow = tgui::theme::palette().window.lightness();
    tgui::theme::setScheme(tgui::theme::Scheme::Dark);
    EXPECT_EQ(tgui::theme::scheme(), tgui::theme::Scheme::Dark);
    const auto& dark = tgui::theme::palette();
    EXPECT_LT(dark.window.lightness(), lightWindow);
    EXPECT_GT(dark.text.lightness(), dark.window.lightness());
    EXPECT_TRUE(qApp->styleSheet().contains(dark.window.name()));
    // Нейтральные значки перерисовываются под тёмный фон без потери альфы.
    const auto icon = tgui::theme::icon(tgui::theme::Icon::Folder);
    EXPECT_FALSE(icon.pixmap(QSize(16, 16)).isNull());

    tgui::theme::setScheme(tgui::theme::Scheme::Light);
    EXPECT_EQ(tgui::theme::scheme(), tgui::theme::Scheme::Light);
    EXPECT_EQ(tgui::theme::palette().window.lightness(), lightWindow);
}

#ifdef Q_OS_WIN
TEST_F(ThemeRendering, NativeTitleBarFollowsSchemeAndWindowEvents)
{
    if (QGuiApplication::platformName() != QStringLiteral("windows"))
        GTEST_SKIP() << "Requires the native Windows platform";

    const auto restore = qScopeGuard([] {
        tgui::theme::setScheme(tgui::theme::Scheme::Light);
    });
    class NativeWindow : public QWidget {
    public:
        using QWidget::destroy;
    } window;
    window.setWindowTitle(QStringLiteral("TransmissionGUI theme check"));
    window.resize(420, 220);
    auto* edit = new QLineEdit(&window);
    edit->setGeometry(20, 40, 260, 30);
    window.show();
    window.activateWindow();
    edit->setFocus();
    spinEvents();
    ASSERT_TRUE(window.isActiveWindow());
    ASSERT_EQ(QApplication::focusWidget(), edit);
    COLORREF color = 0;
    const bool supportsCaptionColor = SUCCEEDED(DwmGetWindowAttribute(
        reinterpret_cast<HWND>(window.winId()), 35, &color, sizeof(color)));

    const auto expectCaption = [&] {
        const HWND handle = reinterpret_cast<HWND>(window.winId());
        const auto& palette = tgui::theme::palette();
        BOOL dark = FALSE;
        ASSERT_TRUE(SUCCEEDED(DwmGetWindowAttribute(handle, 20, &dark, sizeof(dark))));
        EXPECT_EQ(dark != FALSE, tgui::theme::scheme() == tgui::theme::Scheme::Dark);
        if (supportsCaptionColor) {
            COLORREF caption = 0;
            COLORREF text = 0;
            ASSERT_TRUE(SUCCEEDED(DwmGetWindowAttribute(handle, 35, &caption, sizeof(caption))));
            ASSERT_TRUE(SUCCEEDED(DwmGetWindowAttribute(handle, 36, &text, sizeof(text))));
            EXPECT_EQ(caption, RGB(palette.window.red(), palette.window.green(), palette.window.blue()));
            EXPECT_EQ(text, RGB(palette.text.red(), palette.text.green(), palette.text.blue()));
        }
    };
    for (int i = 0; i < 4; ++i) {
        const HWND active = GetActiveWindow();
        const HWND focus = GetFocus();
        tgui::theme::setScheme(i % 2 == 0 ? tgui::theme::Scheme::Dark : tgui::theme::Scheme::Light);
        expectCaption();
        EXPECT_EQ(GetActiveWindow(), active);
        EXPECT_EQ(GetFocus(), focus);
        spinEvents();
        expectCaption();
        EXPECT_EQ(GetActiveWindow(), active);
        EXPECT_EQ(GetFocus(), focus);
        EXPECT_EQ(QApplication::focusWidget(), edit);
    }

    tgui::theme::setScheme(tgui::theme::Scheme::Dark);
    // Системное обновление рамки не должно оставлять заголовок чужого цвета.
    const BOOL otherTheme = FALSE;
    ASSERT_TRUE(SUCCEEDED(DwmSetWindowAttribute(reinterpret_cast<HWND>(window.winId()),
                                               20, &otherTheme, sizeof(otherTheme))));
    if (supportsCaptionColor) {
        const COLORREF otherColor = RGB(255, 255, 255);
        ASSERT_TRUE(SUCCEEDED(DwmSetWindowAttribute(reinterpret_cast<HWND>(window.winId()),
                                                   35, &otherColor, sizeof(otherColor))));
    }
    QEvent themeChange(QEvent::ThemeChange);
    QCoreApplication::sendEvent(&window, &themeChange);
    spinEvents();
    expectCaption();

    window.hide();
    window.destroy();
    window.show();
    spinEvents();
    expectCaption();
    window.showMinimized();
    spinEvents();
    tgui::theme::setScheme(tgui::theme::Scheme::Light);
    window.showNormal();
    spinEvents();
    expectCaption();

    // Смена темы не должна создавать системное окно для ещё не показанного диалога.
    QWidget hidden;
    EXPECT_EQ(hidden.internalWinId(), WId(0));
    tgui::theme::setScheme(tgui::theme::Scheme::Dark);
    spinEvents();
    EXPECT_EQ(hidden.internalWinId(), WId(0));
}
#endif

TEST_F(ThemeRendering, DarkSchemePreview)
{
    const QString output = qEnvironmentVariable("TGUI_DARK_PREVIEW_DIR");
    if (output.isEmpty())
        GTEST_SKIP() << "TGUI_DARK_PREVIEW_DIR is not set";
    QDir().mkpath(output);

    QSettings settings;
    const QStringList keys{"daemons/current", "peers/showFlag", "ui/trayIcon", "ui/updateCheckEnabled"};
    QVariantMap saved;
    for (const auto& key : keys)
        saved.insert(key, settings.value(key));
    const bool quitOnClose = QApplication::quitOnLastWindowClosed();
    const auto restore = qScopeGuard([&] {
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
            if (it.value().isValid())
                settings.setValue(it.key(), it.value());
            else
                settings.remove(it.key());
        }
        QApplication::setQuitOnLastWindowClosed(quitOnClose);
        tgui::theme::setScheme(tgui::theme::Scheme::Light);
    });
    settings.remove(keys.first());
    for (qsizetype i = 1; i < keys.size(); ++i)
        settings.setValue(keys.at(i), false);
    QApplication::setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.resize(1280, 800);
    window.show();
    spinEvents();
    EXPECT_TRUE(window.grab().save(output + QStringLiteral("/light-main.png")));

    // Переключаем тему на живом окне: тот же экземпляр должен перерисоваться.
    tgui::theme::setScheme(tgui::theme::Scheme::Dark);
    spinEvents();
    EXPECT_TRUE(window.grab().save(output + QStringLiteral("/dark-main.png")));

    PropsForm form;
    tgui::Torrent t;
    t.id = 42;
    t.name = "movie-2160p";
    t.status = 4;
    t.totalSize = 1073741824.0;
    t.sizeWhenDone = 536870912.0;
    t.leftUntilDone = 268435456.0;
    t.downloadDir = "/Shares/Video";
    form.setTorrent(t);
    form.setSession(tgui::SessionInfo{});
    form.resize(720, 560);
    EXPECT_TRUE(grabWidget(form).save(output + QStringLiteral("/dark-props.png")));

    OptionsDialog dialog;
    dialog.resize(900, 650);
    EXPECT_TRUE(grabWidget(dialog).save(output + QStringLiteral("/dark-options.png")));
}

TEST_F(ThemeRendering, MainWindow)
{
    // Предыдущие тесты могут оставить выбранный профиль с уже остановленным мок-сервером.
    // Снимок интерфейса делаем в отключённом состоянии.
    QSettings settings;
    const QStringList keys{"daemons/current", "peers/showFlag", "ui/trayIcon", "ui/updateCheckEnabled"};
    QVariantMap saved;
    for (const auto& key : keys)
        saved.insert(key, settings.value(key));
    const bool quitOnClose = QApplication::quitOnLastWindowClosed();
    const auto restore = qScopeGuard([&] {
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) {
            if (it.value().isValid())
                settings.setValue(it.key(), it.value());
            else
                settings.remove(it.key());
        }
        QApplication::setQuitOnLastWindowClosed(quitOnClose);
    });
    settings.remove(keys.first());
    for (qsizetype i = 1; i < keys.size(); ++i)
        settings.setValue(keys.at(i), false);
    QApplication::setQuitOnLastWindowClosed(false);
    MainWindow w;
    w.resize(1280, 800);
    EXPECT_FALSE(grabWidget(w).isNull());
#if defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        w.show();
        spinEvents();
        const auto restoreScheme = qScopeGuard([] {
            tgui::theme::setScheme(tgui::theme::Scheme::Light);
        });
        const auto expectTitleBackground = [&] {
            const int titleHeight = w.windowHandle()->safeAreaMargins().top();
            ASSERT_GT(titleHeight, 0);
            EXPECT_GE(w.menuBar()->geometry().top(), titleHeight);
            const QImage shot = w.grab().toImage();
            const qreal scale = shot.devicePixelRatio();
            EXPECT_EQ(shot.pixelColor(shot.width() / 2, qRound(titleHeight * scale / 2)),
                      tgui::theme::palette().window);
        };
        for (auto scheme : {tgui::theme::Scheme::Dark, tgui::theme::Scheme::Light,
                            tgui::theme::Scheme::Dark}) {
            tgui::theme::setScheme(scheme);
            spinEvents();
            EXPECT_EQ(qApp->styleHints()->colorScheme(), scheme == tgui::theme::Scheme::Dark
                ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
            expectTitleBackground();
        }
        w.showMaximized();
        spinEvents();
        expectTitleBackground();
        w.showNormal();
        spinEvents();
        expectTitleBackground();
        w.close();
    }
#endif
}

TEST_F(ThemeRendering, PropsForm)
{
    PropsForm form;
    tgui::Torrent t;
    t.id = 42;
    t.name = "movie-2160p";
    t.status = 4; // Download
    t.totalSize = 1073741824.0;
    t.sizeWhenDone = 536870912.0;
    t.leftUntilDone = 268435456.0;
    t.downloadDir = "/Shares/Video";
    form.setTorrent(t);
    tgui::SessionInfo session;
    form.setSession(session);
    form.resize(720, 560);
    EXPECT_FALSE(grabWidget(form).isNull());
}

TEST_F(ThemeRendering, OptionsDialog)
{
    OptionsDialog dialog;
    dialog.resize(900, 650);
    EXPECT_FALSE(grabWidget(dialog).isNull());
}

TEST_F(ThemeRendering, ConnectDialog)
{
    tgui::RpcClient::Options o;
    o.url = QUrl(QStringLiteral("http://localhost:9091"));
    ConnectDialog dialog(o);
    dialog.resize(520, 380);
    EXPECT_FALSE(grabWidget(dialog).isNull());
}

TEST_F(ThemeRendering, AboutDialog)
{
    AboutDialog dialog;
    dialog.resize(560, 420);
    EXPECT_FALSE(grabWidget(dialog).isNull());
}

#include <QPainter>
#include <QSvgRenderer>
#include <cstdio>

TEST(TmpMeasure, MoonBounds)
{
    for (const char* name : {":/icons/i-theme-dark.svg", ":/icons/i-theme-light.svg"}) {
        QSvgRenderer r(QString::fromLatin1(name));
        QImage img(160, 160, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        r.render(&p);
        p.end();
        QRect bbox;
        for (int y = 0; y < 160; ++y) {
            for (int x = 0; x < 160; ++x) {
                if (qAlpha(img.pixel(x, y)) > 8)
                    bbox = bbox.isNull() ? QRect(x, y, 1, 1) : bbox.united(QRect(x, y, 1, 1));
            }
        }
        fprintf(stderr, "MEASURE %s bbox px=(%d,%d %dx%d) units=(%.2f,%.2f %.2fx%.2f)\n",
                name, bbox.x(), bbox.y(), bbox.width(), bbox.height(),
                bbox.x() / 10.0, bbox.y() / 10.0, bbox.width() / 10.0, bbox.height() / 10.0);
    }
}
