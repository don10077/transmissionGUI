// Unit-тесты диалога выбора файлов: дерево папок, каскад чекбоксов,
// нижние опции «Start torrent» / «Sequential download».
#include <gtest/gtest.h>

#include <QCheckBox>
#include <QLabel>
#include <QTreeWidget>

#include "fileselectdialog.h"
#include "tgui/util.h"

namespace {

tgui::FileStat makeFile(int index, const QString& name, double size)
{
    tgui::FileStat f;
    f.index = index;
    f.name = name;
    f.size = size;
    return f;
}

tgui::TorrentDetails makeDetails()
{
    tgui::TorrentDetails details;
    details.files = {
        makeFile(0, QStringLiteral("top/a.bin"), 100.0),
        makeFile(1, QStringLiteral("top/sub/b.bin"), 300.0),
        makeFile(2, QStringLiteral("c.bin"), 50.0),
    };
    return details;
}

QTreeWidgetItem* findItem(QTreeWidgetItem* item, const QString& text)
{
    if (item->text(0) == text)
        return item;
    for (int i = 0; i < item->childCount(); ++i) {
        if (auto* found = findItem(item->child(i), text))
            return found;
    }
    return nullptr;
}

QTreeWidgetItem* findItem(QTreeWidget* tree, const QString& text)
{
    return findItem(tree->invisibleRootItem(), text);
}

} // namespace

TEST(FileSelectDialog, BuildsFolderTreeWithSizes)
{
    FileSelectDialog dlg(makeDetails(), true, 18);
    auto* tree = dlg.tree();
    ASSERT_EQ(tree->topLevelItemCount(), 2);
    EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("top"));
    EXPECT_EQ(tree->topLevelItem(1)->text(0), QStringLiteral("c.bin"));

    auto* top = findItem(tree, QStringLiteral("top"));
    ASSERT_NE(top, nullptr);
    EXPECT_FALSE(top->isExpanded());
    ASSERT_EQ(top->childCount(), 2); // a.bin + sub
    EXPECT_EQ(top->text(1), QString::fromStdString(tgui::formatSize(400.0)));

    auto* sub = findItem(tree, QStringLiteral("sub"));
    ASSERT_NE(sub, nullptr);
    EXPECT_FALSE(sub->isExpanded());
    ASSERT_EQ(sub->childCount(), 1);
    EXPECT_EQ(sub->text(1), QString::fromStdString(tgui::formatSize(300.0)));
    EXPECT_EQ(sub->child(0)->text(0), QStringLiteral("b.bin"));
}

TEST(FileSelectDialog, FolderCheckCascadesToFiles)
{
    FileSelectDialog dlg(makeDetails(), true, 18);
    auto* tree = dlg.tree();
    ASSERT_EQ(dlg.wanted(), (QVector<int>{0, 1, 2}));
    EXPECT_TRUE(dlg.unwanted().isEmpty());

    // Сняли папку — снимаются все вложенные файлы (включая подпапку).
    auto* top = findItem(tree, QStringLiteral("top"));
    ASSERT_NE(top, nullptr);
    top->setCheckState(0, Qt::Unchecked);
    EXPECT_EQ(dlg.wanted(), (QVector<int>{2}));
    EXPECT_EQ(dlg.unwanted(), (QVector<int>{0, 1}));
    EXPECT_EQ(top->checkState(0), Qt::Unchecked);
    EXPECT_EQ(findItem(tree, QStringLiteral("sub"))->checkState(0), Qt::Unchecked);
    EXPECT_EQ(findItem(tree, QStringLiteral("b.bin"))->checkState(0), Qt::Unchecked);

    // Отметили файл внутри — папки становятся частично отмеченными.
    findItem(tree, QStringLiteral("b.bin"))->setCheckState(0, Qt::Checked);
    EXPECT_EQ(top->checkState(0), Qt::PartiallyChecked);
    EXPECT_EQ(findItem(tree, QStringLiteral("sub"))->checkState(0), Qt::Checked);
    EXPECT_EQ(dlg.wanted(), (QVector<int>{1, 2}));

    // Отметили папку целиком — каскад обратно.
    top->setCheckState(0, Qt::Checked);
    EXPECT_EQ(top->checkState(0), Qt::Checked);
    EXPECT_EQ(dlg.wanted(), (QVector<int>{0, 1, 2}));
}

TEST(FileSelectDialog, HidesCommonTorrentRootAndKeepsSubfoldersCollapsed)
{
    auto details = makeDetails();
    details.name = "Release";
    for (auto& file : details.files)
        file.name.prepend("Release/");
    FileSelectDialog dialog(details, true, 18);
    EXPECT_EQ(findItem(dialog.tree(), QStringLiteral("Release")), nullptr);
    const auto* folder = findItem(dialog.tree(), QStringLiteral("top"));
    ASSERT_NE(folder, nullptr);
    EXPECT_FALSE(folder->isExpanded());
    EXPECT_EQ(dialog.wanted(), (QVector<int>{0, 1, 2}));
}

TEST(FileSelectDialog, SelectAllAndNone)
{
    FileSelectDialog dlg(makeDetails(), true, 18);
    auto* tree = dlg.tree();

    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        tree->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
    EXPECT_TRUE(dlg.wanted().isEmpty());
    EXPECT_EQ(dlg.unwanted(), (QVector<int>{0, 1, 2}));

    auto* summary = dlg.findChild<QLabel*>(QStringLiteral("selectionSummary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_TRUE(summary->text().contains(
        QString::fromStdString(tgui::formatSize(0.0))));

    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        tree->topLevelItem(i)->setCheckState(0, Qt::Checked);
    EXPECT_EQ(dlg.wanted(), (QVector<int>{0, 1, 2}));
    EXPECT_TRUE(summary->text().contains(
        QString::fromStdString(tgui::formatSize(450.0))));
}

TEST(FileSelectDialog, StartAndSequentialOptions)
{
    FileSelectDialog dlg(makeDetails(), false, 18);
    EXPECT_FALSE(dlg.startTorrent());
    EXPECT_FALSE(dlg.sequentialDownload());

    auto* start = dlg.findChild<QCheckBox*>(QStringLiteral("startTorrent"));
    auto* sequential = dlg.findChild<QCheckBox*>(QStringLiteral("sequentialDownload"));
    ASSERT_NE(start, nullptr);
    ASSERT_NE(sequential, nullptr);
    start->setChecked(true);
    sequential->setChecked(true);
    EXPECT_TRUE(dlg.startTorrent());
    EXPECT_TRUE(dlg.sequentialDownload());

    // RPC < 18: последовательная загрузка недоступна.
    FileSelectDialog old(makeDetails(), true, 15);
    auto* oldSequential = old.findChild<QCheckBox*>(QStringLiteral("sequentialDownload"));
    ASSERT_NE(oldSequential, nullptr);
    EXPECT_FALSE(oldSequential->isEnabled());
    EXPECT_FALSE(old.sequentialDownload());
}
