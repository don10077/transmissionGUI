// Проверяет отрисовку ключевых экранов с палитрой приложения.
#include <gtest/gtest.h>

#include <QApplication>
#include <QEventLoop>
#include <QImage>
#include <QScopeGuard>
#include <QTimer>
#include <QUrl>

#include <cstdlib>

#include "aboutdialog.h"
#include "connectdialog.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "propsform.h"
#include "qostream.h"
#include "theme.h"
#include "tgui/models.h"

namespace {

bool g_themeApplied = false;

// Тема применяется один раз ко всему прогону тестов (глобальный стиль/QSS).
void ensureTheme()
{
    if (g_themeApplied)
        return;
    g_themeApplied = true;
    tgui::theme::apply(*qApp);
}

// Страховка от зависания прогона (модалка): watchdog-таймер, останавливается в конце теста.
QTimer* startWatchdog()
{
    auto* watchdog = new QTimer(qApp);
    watchdog->setSingleShot(true);
    watchdog->setInterval(45000);
    QObject::connect(watchdog, &QTimer::timeout, [] { std::_Exit(3); });
    watchdog->start();
    return watchdog;
}

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

TEST(ThemeRendering, MainWindow)
{
    ensureTheme();
    QApplication::setQuitOnLastWindowClosed(false);
    auto* watchdog = startWatchdog();
    const auto stopWatchdog = qScopeGuard([watchdog] { watchdog->stop(); });
    MainWindow w;
    w.resize(1280, 800);
    EXPECT_FALSE(grabWidget(w).isNull());
}

TEST(ThemeRendering, PropsForm)
{
    ensureTheme();
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

TEST(ThemeRendering, OptionsDialog)
{
    ensureTheme();
    OptionsDialog dialog;
    dialog.resize(900, 650);
    EXPECT_FALSE(grabWidget(dialog).isNull());
}

TEST(ThemeRendering, ConnectDialog)
{
    ensureTheme();
    tgui::RpcClient::Options o;
    o.url = QUrl(QStringLiteral("http://localhost:9091"));
    ConnectDialog dialog(o);
    dialog.resize(520, 380);
    EXPECT_FALSE(grabWidget(dialog).isNull());
}

TEST(ThemeRendering, AboutDialog)
{
    ensureTheme();
    AboutDialog dialog;
    dialog.resize(560, 420);
    EXPECT_FALSE(grabWidget(dialog).isNull());
}
