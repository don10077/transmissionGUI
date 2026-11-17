#include "statusbarcontroller.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QPropertyAnimation>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeView>

#include <algorithm>
#include <limits>
#include <utility>

#include "daemonprofile.h"
#include "theme.h"
#include "torrentmodel.h"
#include "tgui/util.h"

namespace {
constexpr int kToastShowMs = 2700;       // время показа уведомления
constexpr int kToastFadeInMs = 120;      // плавное появление
constexpr int kToastFadeOutMs = 180;     // плавное исчезновение
constexpr int kToastBottomOffset = 60;   // отступ от нижнего края рабочей области
}

StatusBarController::StatusBarController(QMainWindow& window, const TorrentModel& model,
                                         const tgui::SessionInfo& session, Client client, Profile profile)
    : m_window(window), m_model(model), m_sessionInfo(session),
      m_client(std::move(client)), m_profile(std::move(profile))
{
}

void StatusBarController::initialize(QTreeView& view)
{
    m_view = &view;
    m_toast = new QLabel(&m_window);
    m_toast->setObjectName(QStringLiteral("toast"));
    m_toast->setTextFormat(Qt::PlainText);
    m_toast->setWordWrap(true);
    m_toast->setAlignment(Qt::AlignCenter);
    m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_toast->setFocusPolicy(Qt::NoFocus);
    m_toast->hide();
    m_toastTimer.setSingleShot(true);
    connect(&m_toastTimer, &QTimer::timeout, this, [this] {
        if (m_toast->isHidden())
            return;
        auto* effect = static_cast<QGraphicsOpacityEffect*>(m_toast->graphicsEffect());
        auto* fade = new QPropertyAnimation(effect, "opacity", effect);
        fade->setDuration(kToastFadeOutMs);
        fade->setStartValue(effect->opacity());
        fade->setEndValue(0.0);
        connect(fade, &QPropertyAnimation::finished, m_toast, &QWidget::hide);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    });

    auto* bar = m_window.statusBar();
    m_statusStrip = new QWidget(bar);
    m_statusStrip->setObjectName(QStringLiteral("statusStrip"));
    m_statusStrip->setAttribute(Qt::WA_StyledBackground);
    m_statusStrip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* strip = new QHBoxLayout(m_statusStrip);
    strip->setSizeConstraint(QLayout::SetNoConstraint);
    strip->setContentsMargins(0, 0, 0, 0);
    strip->setSpacing(0);

    const auto addGroup = [&] {
        auto* group = new QWidget(m_statusStrip);
        group->setObjectName(QStringLiteral("statusSegmentGroup"));
        group->setAttribute(Qt::WA_StyledBackground);
        auto* layout = new QHBoxLayout(group);
        layout->setContentsMargins(10, 0, 11, 0);
        layout->setSpacing(6);
        strip->addWidget(group);
        return qMakePair(group, layout);
    };

    const auto [connection, connectionLayout] = addGroup();
    m_statusConnection = connection;
    m_statusDot = new QLabel(connection);
    m_statusDot->setFixedSize(8, 8);
    connectionLayout->addWidget(m_statusDot);
    m_statusText = new QLabel(connection);
    m_statusText->setTextFormat(Qt::PlainText);
    m_statusText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connectionLayout->addWidget(m_statusText, 1);

    const auto addRate = [&](bool download) {
        auto* button = new QToolButton(m_statusStrip);
        button->setObjectName(QStringLiteral("statusRate"));
        button->setAutoRaise(true);
        button->setIconSize(QSize(14, 14));
        button->setIcon(tgui::theme::icon(download ? tgui::theme::Icon::StatusDown
                                                  : tgui::theme::Icon::StatusSeed));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setPopupMode(QToolButton::InstantPopup);
        auto* menu = createSpeedLimitMenu(button, download);
        menu->setObjectName(download ? QStringLiteral("statusDownloadSpeed") : QStringLiteral("statusUploadSpeed"));
        button->setMenu(menu);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, button, &QToolButton::showMenu);
        strip->addWidget(button);
        return button;
    };
    m_statusDownload = addRate(true);
    m_statusUpload = addRate(false);

    const auto [alternative, alternativeLayout] = addGroup();
    m_statusAlternative = alternative;
    auto* alternativeIcon = new QLabel(alternative);
    alternativeIcon->setPixmap(tgui::theme::icon(tgui::theme::Icon::AltSpeed)
                                  .pixmap(QSize(14, 14), bar->devicePixelRatioF()));
    alternativeLayout->addWidget(alternativeIcon);
    alternativeLayout->addWidget(new QLabel(tr("Alt speeds"), alternative));

    const auto addLabel = [&] {
        auto* label = new QLabel(m_statusStrip);
        label->setObjectName(QStringLiteral("statusSegment"));
        label->setTextFormat(Qt::PlainText);
        strip->addWidget(label);
        return label;
    };
    m_statusFree = addLabel();
    strip->addStretch(1);
    m_countLabel = addLabel();

    bar->setSizeGripEnabled(true);
    bar->addPermanentWidget(m_statusStrip, 1);
    connect(bar, &QStatusBar::messageChanged, this, &StatusBarController::updateStatusBarPresentation);
    m_statusStrip->installEventFilter(this);
    m_window.centralWidget()->installEventFilter(this);
    setConnectionState(tr("Disconnected"), tgui::theme::palette().statusOfflineDot);
}

void StatusBarController::setConnected(bool on)
{
    m_connected = on;
    for (auto* menu : m_speedLimitMenus)
        menu->setEnabled(on);
    m_statusDownload->setEnabled(on);
    m_statusUpload->setEnabled(on);
}

bool StatusBarController::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        if (watched == m_window.centralWidget())
            positionToast();
        else if (watched == m_statusStrip)
            updateStatusBarPresentation();
    }
    return QObject::eventFilter(watched, event);
}

void StatusBarController::setConnectionState(const QString& text, const QColor& color)
{
    m_connectionStateText = text;
    if (m_statusDot)
        m_statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(color.name()));
    updateStatusBarPresentation();
}

void StatusBarController::showToast(const QString& message)
{
    m_window.statusBar()->showMessage(message, 3000);
    if (message.isEmpty() || !m_window.isVisible() || m_window.isMinimized())
        return;

    m_toast->setText(message);
    auto* effect = new QGraphicsOpacityEffect;
    effect->setOpacity(0.0);
    m_toast->setGraphicsEffect(effect);
    m_toast->show();
    positionToast();
    m_toast->raise();
    auto* fade = new QPropertyAnimation(effect, "opacity", effect);
    fade->setDuration(kToastFadeInMs);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
    m_toastTimer.start(kToastShowMs);
}

/** Центрирует уведомление в рабочей области над строкой состояния. */
void StatusBarController::positionToast()
{
    if (!m_toast || m_toast->isHidden())
        return;
    const QRect area = m_window.centralWidget()->geometry();
    const int width = qMax(1, qMin(m_toast->sizeHint().width(), area.width() * 4 / 5));
    const int height = m_toast->heightForWidth(width);
    m_toast->resize(width, height > 0 ? height : m_toast->sizeHint().height());
    m_toast->move(area.left() + (area.width() - width) / 2,
                  qMax(area.top(), area.bottom() + 1 - kToastBottomOffset - m_toast->height()));
}

/** Считает скорости, свободное место и число торрентов для строки состояния. */
void StatusBarController::updateStatusBarTotals()
{
    if (!m_statusStrip)
        return;

    m_statusDownRate = 0.0;
    m_statusUpRate = 0.0;
    for (const auto& torrent : m_model.torrents()) {
        const auto rates = m_model.displayedRates(torrent);
        m_statusDownRate += rates.first;
        m_statusUpRate += rates.second;
    }

    const auto format = [](double bytes) { return QString::fromStdString(tgui::formatSize(bytes)); };
    if (const auto free = m_sessionInfo.downloadDirFreeSpace; free && *free >= 0) {
        m_statusFree->setText(tr("Free: %1").arg(format(static_cast<double>(*free))));
    } else {
        m_statusFree->clear();
    }

    const auto selected = m_view->selectionModel()->selectedRows();
    const int count = m_model.rowCount();
    QString countText = tr("%n torrent(s)", nullptr, count);
    if (selected.size() > 1)
        countText += tr(", %n selected", nullptr, static_cast<int>(selected.size()));
    m_countLabel->setText(countText);
    updateStatusBarPresentation();
}

/** Уплотняет сегменты при сужении окна, сохраняя доступ к обеим скоростям. */
void StatusBarController::updateStatusBarPresentation()
{
    if (!m_statusStrip)
        return;

    constexpr int connectionPadding = 35; // края, точка и зазор
    constexpr int ratePadding = 42;       // края, иконка и зазор
    const int width = m_statusStrip->width();
    QString connection = m_connectionStateText;
    if (m_connected)
        connection = tr("Transmission %1 (RPC %2)").arg(m_sessionInfo.version).arg(m_sessionInfo.rpcVersion);

    const QString message = m_window.statusBar()->currentMessage();
    const bool showMessage = !message.isEmpty() && message != tr("Connected") && message != tr("Disconnected");
    if (showMessage)
        connection = message;

    const auto limitText = [this](bool download) {
        if (m_sessionInfo.altSpeedEnabled)
            return tr("%1 KB/s").arg(download ? m_sessionInfo.altSpeedDown : m_sessionInfo.altSpeedUp);
        const bool enabled = download ? m_sessionInfo.speedLimitDownEnabled : m_sessionInfo.speedLimitUpEnabled;
        return enabled ? tr("%1 KB/s").arg(download ? m_sessionInfo.speedLimitDown : m_sessionInfo.speedLimitUp)
                       : QString();
    };

    const QString downLimit = limitText(true);
    const QString upLimit = limitText(false);
    const auto withLimit = [](QString text, const QString& limit) {
        if (!limit.isEmpty())
            text += QStringLiteral(" (%1)").arg(limit);
        return text;
    };
    const auto rateText = [this, &withLimit](bool download, const QString& rate, const QString& limit) {
        return withLimit(download ? tr("DL: %1").arg(rate) : tr("UL: %1").arg(rate), limit);
    };
    const QString sample = QStringLiteral("999.9 MB/s");
    const auto metrics = m_statusDownload->fontMetrics();
    const int wideDown = metrics.horizontalAdvance(rateText(true, sample, downLimit)) + ratePadding;
    const int wideUp = metrics.horizontalAdvance(rateText(false, sample, upLimit)) + ratePadding;
    const int narrowRate = qMax(metrics.horizontalAdvance(withLimit(sample, downLimit)),
                                metrics.horizontalAdvance(withLimit(sample, upLimit))) + ratePadding;
    const bool compact = width < wideDown + wideUp + 250;
    const int compactWidth = qMax(0, qMin(narrowRate, (width - connectionPadding - 24) / 2));
    const int downWidth = compact ? compactWidth : wideDown;
    const int upWidth = compact ? compactWidth : wideUp;
    const auto showRate = [&](QToolButton* button, bool download, double rate, const QString& limit, int segmentWidth) {
        button->setVisible(m_connected);
        if (!m_connected)
            return;
        const QString current = QString::fromStdString(tgui::formatRate(rate));
        const QString full = rateText(download, current, limit);
        button->setFixedWidth(segmentWidth);
        const int textRoom = qMax(0, segmentWidth - ratePadding);
        const QString compactText = withLimit(current, limit);
        const QString shown = !compact ? full
            : metrics.horizontalAdvance(compactText) <= textRoom ? compactText : current;
        button->setText(metrics.elidedText(shown, Qt::ElideRight, textRoom));
        button->setToolTip(full + QLatin1Char('\n') +
            (download ? tr("Max download speed") : tr("Max upload speed")));
        button->setAccessibleName(full);
    };
    showRate(m_statusDownload, true, m_statusDownRate, downLimit, downWidth);
    showRate(m_statusUpload, false, m_statusUpRate, upLimit, upWidth);

    int budget = qMax(0, width - (m_connected ? downWidth + upWidth : 0));
    const auto textMetrics = m_statusText->fontMetrics();
    const int maxConnection = m_connected ? qMin(budget, showMessage ? 480 : 360) : budget;
    const int textWidth = qMax(0, maxConnection - connectionPadding);
    QString shown = textMetrics.elidedText(connection, Qt::ElideRight, textWidth);
    if (m_connected && compact && !showMessage && shown != connection)
        shown = textMetrics.elidedText(m_connectionStateText, Qt::ElideRight, textWidth);
    m_statusText->setText(shown);
    m_statusText->setToolTip(shown == connection ? QString() : connection);
    const int connectionWidth = qMin(budget, textMetrics.horizontalAdvance(shown) + connectionPadding);
    m_statusConnection->setFixedWidth(connectionWidth);
    budget -= connectionWidth;

    const auto showSegment = [&](QWidget* segment, bool wanted) {
        const int segmentWidth = segment->sizeHint().width();
        const bool visible = m_connected && wanted && segmentWidth <= budget;
        segment->setVisible(visible);
        if (visible)
            budget -= segmentWidth;
    };
    // Приоритет не зависит от визуального порядка: счётчик находится справа.
    showSegment(m_countLabel, true);
    showSegment(m_statusAlternative, m_sessionInfo.altSpeedEnabled);
    showSegment(m_statusFree, !m_statusFree->text().isEmpty());
}

QMenu* StatusBarController::createSpeedLimitMenu(QWidget* parent, bool download)
{
    auto* menu = new QMenu(download ? tr("Max download speed") : tr("Max upload speed"), parent);
    menu->setObjectName(download ? QStringLiteral("quickDownloadSpeed") : QStringLiteral("quickUploadSpeed"));
    menu->setEnabled(m_connected);
    m_speedLimitMenus.append(menu);
    connect(menu, &QMenu::aboutToShow, this, [this, menu, download] {
        menu->clear();
        if (!m_client() || !m_connected)
            return;
        const QPointer<tgui::RpcClient> rpc(m_client());
        const auto profile = m_profile();
        const auto& session = rpc->sessionInfo();
        const bool enabled = download ? session.speedLimitDownEnabled : session.speedLimitUpEnabled;
        const int current = enabled ? (download ? session.speedLimitDown : session.speedLimitUp) : -1;
        auto speeds = ProfilePreferences(profile).speedPresets(download);
        if (current >= 0 && !speeds.contains(current)) {
            speeds.append(current);
            std::sort(speeds.begin(), speeds.end());
        }
        const auto setLimit = [this, rpc, profile, download](int speed) {
            if (!rpc || rpc != m_client() || !m_connected || profile != m_profile())
                return;
            const auto key = download ? QStringLiteral("speed-limit-down") : QStringLiteral("speed-limit-up");
            QJsonObject params{{key + "-enabled", speed >= 0}};
            if (speed >= 0)
                params.insert(key, speed);
            if (rpc->sessionInfo().altSpeedEnabled)
                params.insert("alt-speed-enabled", false);
            rpc->setSession(params);
        };
        const auto add = [this, menu, setLimit, current, alternate = session.altSpeedEnabled](
                             const QString& caption, int speed) {
            auto* action = menu->addAction(caption);
            action->setCheckable(true);
            action->setChecked(!alternate && speed == current);
            action->setData(speed);
            connect(action, &QAction::triggered, this, [setLimit, speed] { setLimit(speed); });
        };
        add(tr("Unlimited"), -1);
        if (!speeds.isEmpty())
            menu->addSeparator();
        for (auto it = speeds.crbegin(); it != speeds.crend(); ++it)
            add(tr("%1 KB/s").arg(*it), *it);
        menu->addSeparator();
        auto* custom = menu->addAction(tr("Other…"));
        connect(custom, &QAction::triggered, this, [this, rpc, profile, setLimit, download, current] {
            if (!rpc || rpc != m_client() || !m_connected || profile != m_profile())
                return;
            bool accepted = false;
            const int speed = QInputDialog::getInt(&m_window, download ? tr("Max download speed") : tr("Max upload speed"),
                                                   tr("Limit (KB/s):"), qMax(0, current), 0,
                                                   std::numeric_limits<int>::max(), 1, &accepted);
            if (accepted)
                setLimit(speed);
        });
    });
    return menu;
}

void StatusBarController::showMessage(const QString& message, int durationMs)
{
    m_window.statusBar()->showMessage(message, durationMs);
}
