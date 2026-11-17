// TST-03: делегаты списка торрентов — paint в QImage: значок статуса и ETA -1/-2.
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QStyleOptionViewItem>

#include "delegates.h"
#include "torrentmodel.h"
#include "tgui/models.h"

namespace {

QImage paintCell(const tgui::Torrent& torrent, int column, int width = 360)
{
    TorrentModel model;
    model.setTorrents({torrent});
    const QModelIndex index = model.index(0, column);
    TorrentDelegate delegate;
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, width, 34);
    option.state = QStyle::State_Enabled;
    option.palette = QGuiApplication::palette();
    QImage image(width, 34, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    delegate.paint(&painter, option, index);
    painter.end();
    return image;
}

bool hasOpaquePixels(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) != 0)
                return true;
    return false;
}

quint32 fingerprintRegion(const QImage& image, int xBegin, int xEnd)
{
    quint32 hash = 7;
    for (int y = 0; y < image.height(); ++y)
        for (int x = xBegin; x < xEnd && x < image.width(); ++x)
            hash = hash * 31 + static_cast<quint32>(image.pixel(x, y));
    return hash;
}

// Отпечаток левой 28px-зоны (значок статуса) — разные статусы рисуют разные иконки.
quint32 iconFingerprint(const QImage& image)
{
    return fingerprintRegion(image, 0, 28);
}

// Отпечаток всего изображения (для ячеек с текстом по центру).
quint32 imageFingerprint(const QImage& image)
{
    return fingerprintRegion(image, 0, image.width());
}

tgui::Torrent torrentForStatus(int status, bool finished, bool error)
{
    tgui::Torrent t;
    t.id = 1;
    t.name = "row";
    t.status = status;
    t.sizeWhenDone = 100.0;
    t.leftUntilDone = finished ? 0.0 : 50.0;
    if (error)
        t.errorString = "Missing files";
    return t;
}

} // namespace

TEST(TorrentDelegatePaint, StatusIconsDiffer)
{
    const auto downloading = paintCell(torrentForStatus(4, false, false), TorrentModel::ColName);
    const auto seeding = paintCell(torrentForStatus(6, false, false), TorrentModel::ColName);
    const auto finished = paintCell(torrentForStatus(6, true, false), TorrentModel::ColName);
    const auto stopped = paintCell(torrentForStatus(0, false, false), TorrentModel::ColName);
    const auto error = paintCell(torrentForStatus(4, false, true), TorrentModel::ColName);
    const auto verifying = paintCell(torrentForStatus(2, false, false), TorrentModel::ColName);

    const QList<QImage> cells{downloading, seeding, finished, stopped, error, verifying};
    for (const auto& cell : cells)
        EXPECT_TRUE(hasOpaquePixels(cell));

    // Значки статусов не должны сливаться в одну картинку (Seed/Finished одна иконка — см.
    // torrentStatusIcon: Seed не зависит от finished).
    EXPECT_NE(iconFingerprint(downloading), iconFingerprint(stopped));
    EXPECT_NE(iconFingerprint(downloading), iconFingerprint(seeding));
    EXPECT_NE(iconFingerprint(downloading), iconFingerprint(error));
    EXPECT_NE(iconFingerprint(verifying), iconFingerprint(stopped));
    EXPECT_NE(iconFingerprint(error), iconFingerprint(seeding));
}

TEST(TorrentDelegatePaint, EtaCellPerValue)
{
    const auto make = [](int eta) {
        tgui::Torrent t;
        t.id = 1;
        t.name = "row";
        t.status = 4;
        t.eta = eta;
        return t;
    };

    // -1 → «—», -2 → «∞», 45 → «45s» — ячейки рисуются и различаются.
    const auto unknown = paintCell(make(-1), TorrentModel::ColEta);
    const auto infinite = paintCell(make(-2), TorrentModel::ColEta);
    const auto known = paintCell(make(45), TorrentModel::ColEta);
    EXPECT_TRUE(hasOpaquePixels(unknown));
    EXPECT_TRUE(hasOpaquePixels(infinite));
    EXPECT_TRUE(hasOpaquePixels(known));
    EXPECT_NE(imageFingerprint(unknown), imageFingerprint(known));
    EXPECT_NE(imageFingerprint(infinite), imageFingerprint(known));
}