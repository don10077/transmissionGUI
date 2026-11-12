// W10 — раскладка колонок (ColumnLayout) + диалог ColSetupDialog.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QListWidget>
#include <QSettings>

#include "colsetup.h"
#include "torrentmodel.h"

TEST(ColumnLayout, Defaults)
{
    const auto d = ColumnLayout::defaults(26);
    EXPECT_EQ(d.order.size(), 26);
    EXPECT_EQ(d.order.first(), 0);
    EXPECT_EQ(d.order.at(1), TorrentModel::ColLabels);
    int vis = 0;
    for (int i = 0; i < 26; ++i)
        if (d.visible.at(i))
            vis++;
    EXPECT_EQ(vis, 12);
    EXPECT_EQ(d.widths.value(TorrentModel::ColName), 330);
}

TEST(ColumnLayout, MoveItem)
{
    auto d = ColumnLayout::defaults(26);
    d.moveItem(0, 1);
    EXPECT_EQ(d.order.at(0), TorrentModel::ColLabels);
    EXPECT_EQ(d.order.at(1), 0);
    d.moveItem(25, 1); // out of range — no-op
    EXPECT_EQ(d.order.at(25), TorrentModel::ColPrivate);
    d.moveItem(0, -1); // out of range — no-op
    EXPECT_EQ(d.order.at(0), TorrentModel::ColLabels);
}

TEST(ColumnLayout, EnsureAtLeastOneVisible)
{
    auto d = ColumnLayout::defaults(26);
    for (int i = 0; i < 26; ++i)
        d.visible[i] = false;
    d.ensureAtLeastOneVisible();
    EXPECT_TRUE(d.visible.at(ColumnLayout::persistentColumn()));
}

TEST(ColumnLayout, SettingsRoundTrip)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test")); // изоляция скоупа
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));
    QSettings().remove("columns");
    auto d = ColumnLayout::defaults(26);
    d.order.swapItemsAt(0, 5);
    d.visible[8] = false;
    d.saveToSettings();
    const auto loaded = ColumnLayout::fromSettings(26);
    EXPECT_EQ(loaded.order, d.order);
    EXPECT_EQ(loaded.visible, d.visible);
    EXPECT_EQ(loaded.widths, d.widths);
}

TEST(ColSetupDialog, OrderVisibleRoundTrip)
{
    const auto layout = ColumnLayout::defaults(26);
    QStringList titles;
    for (int i = 0; i < 26; ++i)
        titles << QStringLiteral("Col%1").arg(i);
    ColSetupDialog dlg(layout, titles);
    EXPECT_EQ(dlg.layout().order, layout.order);

    auto* list = dlg.findChild<QListWidget*>();
    ASSERT_NE(list, nullptr);
    list->item(5)->setCheckState(Qt::Unchecked); // скрыть колонку 5
    EXPECT_FALSE(dlg.layout().visible.at(layout.order.at(5)));
    list->item(0)->setCheckState(Qt::Unchecked); // Name (0) — всегда видима
    EXPECT_TRUE(dlg.layout().visible.at(0));
    EXPECT_EQ(dlg.layout().widths, layout.widths); // ширины сохраняются
}

// W11: раскладка колонок пиров хранится в отдельном пространстве имён (peers/columns)
// и не конфликтует с колонками списка торрентов (columns).
TEST(ColumnLayout, PeersNamespaceIsolated)
{
    QCoreApplication::setOrganizationName(QStringLiteral("tgui-test"));
    QCoreApplication::setApplicationName(QStringLiteral("tgui-test"));
    QSettings().remove("columns");
    QSettings().remove("peers/columns");
    auto a = ColumnLayout::defaults(8);
    a.order.swapItemsAt(0, 3);
    a.visible[5] = false;
    a.saveToSettings(QStringLiteral("peers/columns"));
    const auto loaded = ColumnLayout::fromSettings(8, QStringLiteral("peers/columns"));
    EXPECT_EQ(loaded.order, a.order);
    EXPECT_EQ(loaded.visible, a.visible);
    // Общий "columns" (список торрентов) не задет.
    EXPECT_TRUE(QSettings().value("columns/order").toStringList().isEmpty());
}
