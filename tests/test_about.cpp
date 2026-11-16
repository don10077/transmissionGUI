// W10 — AboutDialog: тексты (About + License) и сборка диалога.
#include <gtest/gtest.h>

#include "aboutdialog.h"

#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

TEST(AboutDialog, Texts)
{
    EXPECT_TRUE(AboutDialog::aboutText().contains(QStringLiteral("Transmission")));
    EXPECT_TRUE(AboutDialog::licenseText().contains(QStringLiteral("GNU General Public License")));
    EXPECT_TRUE(AboutDialog::licenseText().contains(QStringLiteral("gnu.org/licenses")));
    EXPECT_TRUE(AboutDialog::licenseText().contains(QStringLiteral("github.com/transmission/transmission-icons")));
}

TEST(AboutDialog, Construct)
{
    AboutDialog dlg;
    EXPECT_FALSE(dlg.windowTitle().isEmpty());
    EXPECT_GE(dlg.minimumWidth(), 500);
    EXPECT_TRUE(dlg.windowFlags().testFlag(Qt::FramelessWindowHint));

    auto* icon = dlg.findChild<QLabel*>(QStringLiteral("aboutIcon"));
    ASSERT_NE(icon, nullptr);
    const QIcon transmissionIcon(QStringLiteral(":/icons/transmission-upstream.png"));
    ASSERT_FALSE(transmissionIcon.isNull());
    EXPECT_EQ(icon->pixmap().toImage(),
              transmissionIcon.pixmap(icon->size(), dlg.devicePixelRatioF()).toImage());

    auto* title = dlg.findChild<QLabel*>(QStringLiteral("aboutTitle"));
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Transmission Remote GUI"));

    auto* version = dlg.findChild<QLabel*>(QStringLiteral("aboutVersion"));
    ASSERT_NE(version, nullptr);
    EXPECT_TRUE(version->text().contains(QStringLiteral("Qt ")));

    auto* description = dlg.findChild<QLabel*>(QStringLiteral("aboutDescription"));
    ASSERT_NE(description, nullptr);
    EXPECT_TRUE(description->wordWrap());
    EXPECT_TRUE(description->text().contains(QStringLiteral("Transmission")));

    EXPECT_NE(dlg.findChild<QDialogButtonBox*>(), nullptr);
}

TEST(AboutDialog, Donation)
{
    AboutDialog dlg;

    auto* link = dlg.findChild<QLabel*>(QStringLiteral("donateLink"));
    ASSERT_NE(link, nullptr);
    EXPECT_TRUE(link->text().contains(QStringLiteral("https://www.donationalerts.com/r/alexshkurko")));
    EXPECT_TRUE(link->openExternalLinks());

    auto* qr = dlg.findChild<QLabel*>(QStringLiteral("donateQr"));
    ASSERT_NE(qr, nullptr);
    const auto image = qr->pixmap().toImage();
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0);
    EXPECT_EQ(image.pixelColor(image.width() / 8, image.height() / 8), QColor(Qt::black));

    auto* button = dlg.findChild<QPushButton*>(QStringLiteral("donateButton"));
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->text().isEmpty());

    auto* project = dlg.findChild<QLabel*>(QStringLiteral("projectLink"));
    ASSERT_NE(project, nullptr);
    EXPECT_TRUE(project->text().contains(QStringLiteral("github.com/don10077/transmissionGUI")));
}

TEST(AboutDialog, LicenseSectionCollapsed)
{
    AboutDialog dlg;

    auto* toggle = dlg.findChild<QToolButton*>(QStringLiteral("licenseToggle"));
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isChecked());

    auto* panel = dlg.findChild<QWidget*>(QStringLiteral("licensePanel"));
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->isHidden());

    auto* text = panel->findChild<QPlainTextEdit*>(QStringLiteral("licenseText"));
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(text->isReadOnly());
    EXPECT_TRUE(text->toPlainText().contains(QStringLiteral("GNU General Public License")));

    toggle->click();
    EXPECT_TRUE(toggle->isChecked());
    EXPECT_FALSE(panel->isHidden());
    EXPECT_EQ(toggle->arrowType(), Qt::DownArrow);

    toggle->click();
    EXPECT_TRUE(panel->isHidden());
    EXPECT_EQ(toggle->arrowType(), Qt::RightArrow);
}
