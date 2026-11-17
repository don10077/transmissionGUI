#include "aboutdialog.h"

#include "theme.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QSysInfo>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr auto kDonateUrl = "https://www.donationalerts.com/r/alexshkurko";
constexpr auto kProjectUrl = "https://github.com/don10077/transmissionGUI";

/** «Windows x64» / «Linux arm64» — как в макете, без лишних подробностей ОС. */
QString platformName()
{
    QString name;
    const QString kernel = QSysInfo::kernelType();
    if (kernel == QLatin1String("winnt"))
        name = QStringLiteral("Windows");
    else if (kernel == QLatin1String("darwin"))
        name = QStringLiteral("macOS");
    else
        name = QStringLiteral("Linux");

    const QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == QLatin1String("x86_64"))
        name += QStringLiteral(" x64");
    else if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64"))
        name += QStringLiteral(" arm64");
    else if (!arch.isEmpty())
        name += QLatin1Char(' ') + arch;
    return name;
}

/** Подпись под названием: версия, версия Qt и платформа. */
QString versionText()
{
    QStringList parts;
    const QString version = QCoreApplication::applicationVersion();
    if (!version.isEmpty())
        parts << QObject::tr("Version %1").arg(version);
    parts << QStringLiteral("Qt %1").arg(QStringLiteral(QT_VERSION_STR));
    const QString platform = platformName();
    if (!platform.isEmpty())
        parts << platform;
    return parts.join(QStringLiteral(" · "));
}

/** Ссылка в тексте: акцентный цвет, открытие во внешнем браузере. */
QLabel* linkLabel(const QString& url, const QString& text, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);
    label->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text.toHtmlEscaped()));
    return label;
}

/** Превращает светлые участки QR-кода в прозрачный фон, сохраняя модули и поля. */
QImage donationQrImage()
{
    auto image = QImage(QStringLiteral(":/icons/donate-qr.png")).convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            pixels[x] = qRgba(0, 0, 0, 255 - qGray(pixels[x]));
    }
    return image;
}

} // namespace

QString AboutDialog::aboutText()
{
    return tr("Remote control for the Transmission daemon. Rewritten in C++/Qt.");
}

QString AboutDialog::licenseText()
{
    auto text = tr(
        "This program is free software under the GNU General Public License, "
        "version 3 (or any later version). It is provided \"as is\", without a "
        "warranty of any kind. See "
        "https://www.gnu.org/licenses/gpl-3.0.html for the full license text.");
    QFile iconNotice(QStringLiteral(":/icons/transmission-upstream-license.md"));
    if (iconNotice.open(QIODevice::ReadOnly))
        text += QStringLiteral("\n\n") + QString::fromUtf8(iconNotice.readAll()).trimmed();
    return text;
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("About"));
    setProperty("aboutDialog", true);
    setMinimumWidth(tgui::theme::dp(520));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(tgui::theme::dp(18), tgui::theme::dp(14),
                             tgui::theme::dp(18), tgui::theme::dp(12));
    root->setSpacing(tgui::theme::dp(12));

    // Шапка: иконка Transmission, название клиента и строка версии.
    auto* header = new QHBoxLayout;
    header->setSpacing(tgui::theme::dp(12));

    auto* icon = new QLabel(this);
    icon->setObjectName(QStringLiteral("aboutIcon"));
    icon->setFixedSize(tgui::theme::dp(40), tgui::theme::dp(40));
    icon->setPixmap(tgui::theme::icon(tgui::theme::Icon::App).pixmap(icon->size(), devicePixelRatioF()));
    header->addWidget(icon, 0, Qt::AlignTop);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(tgui::theme::dp(2));

    auto* title = new QLabel(QStringLiteral(TGUI_DISPLAY_NAME), this);
    title->setObjectName(QStringLiteral("aboutTitle"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 3.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    titles->addWidget(title);

    auto* version = new QLabel(versionText(), this);
    version->setObjectName(QStringLiteral("aboutVersion"));
    version->setTextFormat(Qt::PlainText);
    titles->addWidget(version);

    header->addLayout(titles, 1);
    root->addLayout(header);

    auto* description = new QLabel(aboutText(), this);
    description->setObjectName(QStringLiteral("aboutDescription"));
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    root->addWidget(description);

    // Поддержка проекта: QR-код и ссылка на страницу доната.
    auto* donateGroup = new QGroupBox(tr("Support the project"), this);
    donateGroup->setObjectName(QStringLiteral("donateGroup"));
    auto* donateLayout = new QHBoxLayout(donateGroup);
    donateLayout->setContentsMargins(tgui::theme::dp(12), tgui::theme::dp(10),
                                     tgui::theme::dp(12), tgui::theme::dp(12));
    donateLayout->setSpacing(tgui::theme::dp(14));

    auto* qr = new QLabel(donateGroup);
    qr->setObjectName(QStringLiteral("donateQr"));
    qr->setAlignment(Qt::AlignCenter);
    qr->setFixedSize(tgui::theme::dp(164), tgui::theme::dp(164));
    qr->setToolTip(tr("Scan the QR code with your phone to open the donation page"));
    const QImage qrSource = donationQrImage();
    if (!qrSource.isNull()) {
        const qreal dpr = devicePixelRatioF();
        const int target = tgui::theme::dp(154);
        auto scaled = QPixmap::fromImage(qrSource.scaled(qRound(target * dpr), qRound(target * dpr),
                                                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
        scaled.setDevicePixelRatio(dpr);
        qr->setPixmap(scaled);
    }
    donateLayout->addWidget(qr, 0, Qt::AlignTop);

    auto* donateText = new QVBoxLayout;
    donateText->setSpacing(tgui::theme::dp(8));

    auto* donateHint = new QLabel(
        tr("If the program is useful to you, you can support its development. "
           "Scan the QR code with your phone or open the link below."),
        donateGroup);
    donateHint->setObjectName(QStringLiteral("hintLabel"));
    donateHint->setWordWrap(true);
    donateText->addWidget(donateHint);

    auto* donateLink = linkLabel(QString::fromLatin1(kDonateUrl),
                                 QStringLiteral("donationalerts.com/r/alexshkurko"), donateGroup);
    donateLink->setObjectName(QStringLiteral("donateLink"));
    donateText->addWidget(donateLink);
    donateText->addStretch(1);

    auto* donateButton = new QPushButton(tr("Donate"), donateGroup);
    donateButton->setObjectName(QStringLiteral("donateButton"));
    donateButton->setCursor(Qt::PointingHandCursor);
    connect(donateButton, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDonateUrl)));
    });
    donateText->addWidget(donateButton, 0, Qt::AlignLeft);

    donateLayout->addLayout(donateText, 1);
    root->addWidget(donateGroup);

    auto* projectLink = linkLabel(QString::fromLatin1(kProjectUrl),
                                  tr("Project on GitHub"), this);
    projectLink->setObjectName(QStringLiteral("projectLink"));
    root->addWidget(projectLink);

    // Лицензия — раскрывающийся блок, чтобы не занимать место постоянно.
    auto* licenseToggle = new QToolButton(this);
    licenseToggle->setObjectName(QStringLiteral("licenseToggle"));
    licenseToggle->setText(tr("License (GPL v3)"));
    licenseToggle->setCheckable(true);
    licenseToggle->setArrowType(Qt::RightArrow);
    licenseToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    licenseToggle->setAutoRaise(true);
    licenseToggle->setCursor(Qt::PointingHandCursor);
    root->addWidget(licenseToggle, 0, Qt::AlignLeft);

    auto* licensePanel = new QWidget(this);
    licensePanel->setObjectName(QStringLiteral("licensePanel"));
    auto* licenseLayout = new QVBoxLayout(licensePanel);
    licenseLayout->setContentsMargins(0, 0, 0, 0);
    auto* licenseEdit = new QPlainTextEdit(licensePanel);
    licenseEdit->setObjectName(QStringLiteral("licenseText"));
    licenseEdit->setReadOnly(true);
    licenseEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    licenseEdit->setPlainText(licenseText());
    licenseEdit->setFixedHeight(tgui::theme::dp(104));
    licenseLayout->addWidget(licenseEdit);
    licensePanel->setVisible(false);
    root->addWidget(licensePanel);

    connect(licenseToggle, &QToolButton::toggled, this,
            [this, licenseToggle, licensePanel](bool on) {
        licenseToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        licensePanel->setVisible(on);
        // Высота диалога подстраивается под раскрытый текст лицензии.
        layout()->activate();
        resize(width(), sizeHint().height());
    });

    auto* buttons = new QDialogButtonBox(this);
    auto* close = buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);
    close->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    resize(minimumWidth(), sizeHint().height());
    tgui::theme::decorateDialog(this);
}
