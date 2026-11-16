// Unit-тесты AddLinkDialog (W05): буфер, валидация пустой ссылки, отмена, приём.
#include <gtest/gtest.h>

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "addlinkdialog.h"

TEST(AddLinkDialog, BufferAndLinkGetter)
{
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:abc123");
    AddLinkDialog dlg(magnet);
    auto* edit = dlg.findChild<QLineEdit*>(QStringLiteral("linkEdit"));
    ASSERT_NE(edit, nullptr);
    EXPECT_EQ(edit->text(), magnet);
    EXPECT_EQ(dlg.link(), magnet);
}

TEST(AddLinkDialog, EmptyLinkNotAccepted)
{
    AddLinkDialog dlg{};
    auto* box = dlg.findChild<QDialogButtonBox*>();
    ASSERT_NE(box, nullptr);
    auto* ok = box->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok, nullptr);
    ok->click();
    EXPECT_NE(dlg.result(), QDialog::Accepted); // остаётся открытым
    EXPECT_TRUE(dlg.link().isEmpty());
    auto* status = dlg.findChild<QLabel*>(QStringLiteral("statusLabel"));
    ASSERT_NE(status, nullptr);
    EXPECT_FALSE(ok->isEnabled());
}

TEST(AddLinkDialog, OnlyAcceptsTorrentLinks)
{
    AddLinkDialog dlg;
    auto* edit = dlg.findChild<QLineEdit*>();
    auto* ok = dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    ASSERT_NE(edit, nullptr);
    ASSERT_NE(ok, nullptr);
    for (const auto* text : {"file:///tmp/x.torrent", "some text", "magnet:?dn=missing-hash", "http:/missing-host"}) {
        edit->setText(QString::fromLatin1(text));
        EXPECT_FALSE(ok->isEnabled());
        EXPECT_TRUE(dlg.link().isEmpty());
    }
    edit->setText("https://example.test/download");
    EXPECT_TRUE(ok->isEnabled());
}

TEST(AddLinkDialog, ValidLinkAccepted)
{
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:abc123");
    AddLinkDialog dlg(magnet);
    auto* box = dlg.findChild<QDialogButtonBox*>();
    ASSERT_NE(box, nullptr);
    box->button(QDialogButtonBox::Ok)->click();
    EXPECT_EQ(dlg.result(), QDialog::Accepted);
    EXPECT_EQ(dlg.link(), magnet);
}

TEST(AddLinkDialog, CancelRejects)
{
    AddLinkDialog dlg(QStringLiteral("magnet:?xt=urn:btih:abc123"));
    auto* box = dlg.findChild<QDialogButtonBox*>();
    ASSERT_NE(box, nullptr);
    box->button(QDialogButtonBox::Cancel)->click();
    EXPECT_EQ(dlg.result(), QDialog::Rejected);
}
