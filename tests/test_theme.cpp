// Проверяет отрисовку ключевых экранов с палитрой приложения.
#include <gtest/gtest.h>

#include <QApplication>
#include <QEventLoop>
#include <QFont>
#include <QImage>
#include <QPalette>
#include <QScopeGuard>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "aboutdialog.h"
#include "connectdialog.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "propsform.h"
#include "qostream.h"
#include "theme.h"
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
        m_styleSheet = qApp->styleSheet();
        m_palette = qApp->palette();
        m_font = qApp->font();
        tgui::theme::apply(*qApp);
    }

    void TearDown() override
    {
        qApp->setStyleSheet(m_styleSheet);
        qApp->setPalette(m_palette);
        qApp->setFont(m_font);
    }

private:
    QString m_styleSheet;
    QPalette m_palette;
    QFont m_font;
};

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
