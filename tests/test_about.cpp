// W10 — AboutDialog: тексты (About + License) и сборка диалога.
#include <gtest/gtest.h>

#include "aboutdialog.h"

TEST(AboutDialog, Texts)
{
    EXPECT_TRUE(AboutDialog::aboutText().contains(QStringLiteral("Transmission")));
    EXPECT_TRUE(AboutDialog::licenseText().contains(QStringLiteral("GNU General Public License")));
    EXPECT_TRUE(AboutDialog::licenseText().contains(QStringLiteral("gnu.org/licenses")));
}

TEST(AboutDialog, Construct)
{
    AboutDialog dlg;
    EXPECT_FALSE(dlg.windowTitle().isEmpty());
}