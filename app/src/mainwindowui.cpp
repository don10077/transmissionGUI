#include "mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include "connectbutton.h"
#include "daemonprofile.h"
#include "filterpanel.h"
#include "propsform.h"
#include "theme.h"

namespace {
/** Показывает поле в тулбаре и обычную команду поиска в меню переполнения. */
class ToolbarSearchAction : public QWidgetAction {
public:
    /** Привязывает уже созданное поле поиска.
     * @param edit Поле в панели инструментов.
     */
    explicit ToolbarSearchAction(QLineEdit* edit)
        : QWidgetAction(edit->parentWidget()), m_edit(edit)
    {}

protected:
    /** Возвращает поле только для тулбара. В меню используется текст команды.
     * @param parent Контейнер действия.
     */
    QWidget* createWidget(QWidget* parent) override
    {
        return m_edit && parent == m_edit->parentWidget() ? m_edit.data() : nullptr;
    }

private:
    QPointer<QLineEdit> m_edit;
};
} // namespace

void MainWindow::createActions()
{
    using tgui::theme::Icon;

    m_actConnect = new QAction(tr("Connect"), this);
    m_actConnect->setIcon(tgui::theme::icon(Icon::Connect));
    m_actConnect->setToolTip(tr("Connect to daemon (Ctrl+K)"));
    connect(m_actConnect, &QAction::triggered, this, &MainWindow::onConnect);

    m_actDisconnect = new QAction(tr("Disconnect from Transmission"), this);
    m_actDisconnect->setObjectName(QStringLiteral("disconnectAction"));
    m_actDisconnect->setToolTip(tr("Disconnect from Transmission"));
    connect(m_actDisconnect, &QAction::triggered, this, &MainWindow::onDisconnect);

    // Сочетания — как в transgui/main.lfm (Insert, Ctrl+Insert, F3/F4, F7, F8, F9,
    // Ctrl+F9, F11/F12, Shift+F11/Shift+F12).
    m_actAdd = new QAction(tr("Add torrent…"), this);
    m_actAdd->setShortcut(QKeySequence(Qt::Key_Insert));
    m_actAdd->setIcon(tgui::theme::icon(Icon::Add));
    m_actAdd->setToolTip(tr("Add torrent (Insert)"));
    m_actAdd->setEnabled(false);
    connect(m_actAdd, &QAction::triggered, &m_add, &AddTorrentCoordinator::onAddTorrent);

    m_actAddLink = new QAction(tr("Add torrent link…"), this);
    m_actAddLink->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Insert));
    m_actAddLink->setIcon(tgui::theme::icon(Icon::AddLink));
    m_actAddLink->setToolTip(tr("Add torrent link (Ctrl+Insert)"));
    m_actAddLink->setEnabled(false);
    connect(m_actAddLink, &QAction::triggered, &m_add, &AddTorrentCoordinator::onAddFromLink);

    m_actions.createActions();

    m_actRefresh = new QAction(tr("Refresh"), this);
    m_actRefresh->setShortcut(QKeySequence::Refresh);
    m_actRefresh->setIcon(tgui::theme::icon(Icon::Refresh));
    m_actRefresh->setToolTip(tr("Refresh torrent list (F5)"));
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);

    m_actAutoRefresh = new QAction(tr("Auto refresh"), this);
    m_actAutoRefresh->setObjectName(QStringLiteral("autoRefreshAction"));
    m_actAutoRefresh->setCheckable(true);
    m_actAutoRefresh->setChecked(true);
    m_actAutoRefresh->setToolTip(tr("Automatically refresh the torrent list"));
    connect(m_actAutoRefresh, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("ui/autoRefresh", on);
        updateRefreshSchedule();
    });

    m_actAltSpeed = new QAction(tr("Use alternate bandwidth settings"), this);
    m_actAltSpeed->setShortcut(QKeySequence(Qt::Key_F8));
    m_actAltSpeed->setCheckable(true);
    m_actAltSpeed->setChecked(QSettings().value("ui/altSpeed", false).toBool());
    m_actAltSpeed->setIcon(tgui::theme::icon(Icon::AltSpeed));
    m_actAltSpeed->setToolTip(tr("Toggle the daemon's alternative speed limit (F8)"));
    connect(m_actAltSpeed, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("ui/altSpeed", on);
        if (m_connection.client() && m_connection.isConnected())
            m_connection.client()->setAltSpeed(on);
    });

    m_actDaemonOptions = new QAction(tr("Transmission options…"), this);
    m_actDaemonOptions->setShortcut(QKeySequence(Qt::Key_F9));
    m_actDaemonOptions->setIcon(tgui::theme::icon(Icon::DaemonOptions));
    m_actDaemonOptions->setToolTip(tr("Transmission options (F9)"));
    connect(m_actDaemonOptions, &QAction::triggered, this, &MainWindow::onDaemonOptions);

    m_actOptions = new QAction(tr("Application options…"), this);
    m_actOptions->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F9));
    m_actOptions->setIcon(tgui::theme::icon(Icon::AppOptions));
    m_actOptions->setToolTip(tr("Application options (Ctrl+F9)"));
    connect(m_actOptions, &QAction::triggered, this, &MainWindow::onOptions);

    m_actAbout = new QAction(tr("About"), this);
    m_actAbout->setIcon(tgui::theme::icon(Icon::About));
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);

    // Панель инструментов по design/mockup.html.
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setAllowedAreas(Qt::TopToolBarArea);
    m_toolBar->setIconSize(QSize(22, 22));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    const auto addToolbarButton = [this](QAction* action) {
        m_toolBar->addAction(action);
        auto* button = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(action));
        button->setProperty("toolbarAction", true);
        button->setFocusPolicy(Qt::StrongFocus);
        return button;
    };
    m_actAdd->setIconText(tr("Add"));
    m_actAddLink->setIconText(tr("Link"));
    m_actions.startAction()->setIconText(tr("Start"));
    m_actions.stopAction()->setIconText(tr("Pause"));
    m_actions.removeAction()->setIconText(tr("Remove"));
    m_actions.moveAction()->setIconText(tr("Move"));
    m_actAltSpeed->setIconText(tr("Limit"));

    m_actDaemonOptions->setIconText(QStringLiteral("Transmission"));
    m_actOptions->setIconText(tr("Options"));

    m_connectButton = new ConnectButton(m_toolBar);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    m_connectButton->setToolTip(tr("Connection profiles (Ctrl+K)"));
    updateConnectionButton();
    auto* profilesMenu = new QMenu(m_connectButton);
    connect(profilesMenu, &QMenu::aboutToShow, this, [this, profilesMenu] {
        profilesMenu->clear();
        auto* aConnect =
            profilesMenu->addAction(tr("Connect to Transmission"));
        aConnect->setToolTip(tr("Connect to the current connection (Ctrl+K)"));
        connect(aConnect, &QAction::triggered, this, &MainWindow::onConnect);
        ProfileStore store;
        const QStringList names = store.names();
        if (!names.isEmpty())
            profilesMenu->addSeparator();
        for (const QString& name : names) {
            auto* a = profilesMenu->addAction(name);
            a->setCheckable(true);
            a->setChecked(name == store.currentName());
            connect(a, &QAction::triggered, this, [this, name] { m_connection.switchToProfile(name); });
        }
        profilesMenu->addSeparator();
        profilesMenu->addAction(m_actDisconnect);
        profilesMenu->addSeparator();
        auto* manage = profilesMenu->addAction(tr("Manage connections…"));
        connect(manage, &QAction::triggered, this, &MainWindow::onConnectionOptions);
    });
    m_connectButton->setMenu(profilesMenu);
    auto* profilesShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(profilesShortcut, &QShortcut::activated, this, [this, profilesMenu] {
        if (m_connectButton->isVisible())
            m_connectButton->showMenu();
        else
            profilesMenu->popup(mapToGlobal(QPoint(0, menuBar()->height())));
    });
    m_toolBar->addWidget(m_connectButton);
    m_toolBar->addSeparator();

    addToolbarButton(m_actAdd);
    addToolbarButton(m_actAddLink);
    m_toolBar->addSeparator();
    addToolbarButton(m_actions.startAction());
    addToolbarButton(m_actions.stopAction());
    addToolbarButton(m_actions.removeAction());
    m_toolBar->addSeparator();

    m_actQueue = new QAction(tgui::theme::icon(Icon::Queue), tr("Queue"), this);
    m_actQueue->setToolTip(tr("Change queue position"));
    m_actQueue->setObjectName(QStringLiteral("queueAction"));
    auto* queueMenu = new QMenu(m_toolBar);
    auto* first = queueMenu->addAction(tr("Move top"));
    auto* up = queueMenu->addAction(tgui::theme::icon(Icon::QueueUp), tr("Move up"));
    auto* down = queueMenu->addAction(tgui::theme::icon(Icon::QueueDown), tr("Move down"));
    auto* last = queueMenu->addAction(tr("Move bottom"));
    connect(first, &QAction::triggered, &m_actions, &TorrentActions::onQueueTop);
    connect(up, &QAction::triggered, this, &MainWindow::onQueueUp);
    connect(down, &QAction::triggered, this, &MainWindow::onQueueDown);
    connect(last, &QAction::triggered, &m_actions, &TorrentActions::onQueueBottom);
    m_actQueue->setMenu(queueMenu);
    auto* queueButton = addToolbarButton(m_actQueue);
    queueButton->setObjectName(QStringLiteral("queueButton"));
    queueButton->setPopupMode(QToolButton::InstantPopup);
    addToolbarButton(m_actions.moveAction());
    addToolbarButton(m_actAltSpeed);
    m_toolBar->addSeparator();
    addToolbarButton(m_actDaemonOptions);
    addToolbarButton(m_actOptions);

    // Поиск — в тулбаре, как edSearch в transgui.
    auto* spacer = new QWidget(m_toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);
    m_searchEdit = new QLineEdit(m_toolBar);
    m_searchEdit->setObjectName(QStringLiteral("searchEdit"));
    const QKeySequence searchShortcut(QKeySequence::Find);
    const QString searchHint = searchShortcut.toString(QKeySequence::NativeText);
    m_searchEdit->setPlaceholderText(tr("Search torrents") + QStringLiteral("  ") + searchHint);
    m_searchEdit->setAccessibleName(tr("Search torrents"));
    m_searchEdit->setToolTip(m_searchEdit->placeholderText());
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->addAction(tgui::theme::icon(Icon::Search), QLineEdit::LeadingPosition);
    m_searchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_searchEdit->setMinimumWidth(120);
    m_searchEdit->setMaximumWidth(250);
    m_list.bindSearch(*m_searchEdit);
    auto* find = new ToolbarSearchAction(m_searchEdit);
    find->setText(tr("Search torrents"));
    find->setIcon(tgui::theme::icon(Icon::Search));
    find->setShortcut(searchShortcut);
    m_toolBar->addAction(find);
    addAction(find);
    connect(find, &QAction::triggered, m_searchEdit, [this] {
        if (m_searchEdit->isVisible()) {
            m_searchEdit->setFocus();
            m_searchEdit->selectAll();
            return;
        }
        bool accepted = false;
        const QString text = QInputDialog::getText(this, tr("Search torrents"), tr("Search torrents"),
                                                   QLineEdit::Normal, m_searchEdit->text(), &accepted);
        if (accepted)
            m_searchEdit->setText(text);
    });
    applyToolbarStyle(QSettings().value("ui/toolbarButtonStyle", 2).toInt());
    m_toolBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_toolBar, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        QMenu menu(this);
        QActionGroup group(&menu);
        const QStringList names{tr("Icons only"), tr("Text beside icons"), tr("Text under icons")};
        for (int value = 0; value < names.size(); ++value) {
            auto* action = menu.addAction(names.at(value));
            action->setCheckable(true); group.addAction(action);
            const Qt::ToolButtonStyle modes[]{Qt::ToolButtonIconOnly, Qt::ToolButtonTextBesideIcon, Qt::ToolButtonTextUnderIcon};
            action->setChecked(m_toolBar->toolButtonStyle() == modes[value]);
            connect(action, &QAction::triggered, &menu, [this, value] {
                applyToolbarStyle(value);
                QSettings().setValue("ui/toolbarButtonStyle", value);
            });
        }
        menu.addSeparator();
        auto* hide = menu.addAction(tr("Hide toolbar"));
        connect(hide, &QAction::triggered, this, [this] { m_actToolbar->setChecked(false); });
        menu.exec(m_toolBar->mapToGlobal(point));
    });

}

/** Применяет режим кнопок, размеры значков и отступы панели инструментов. */
void MainWindow::applyToolbarStyle(int value)
{
    if (value < 0 || value > 2)
        value = 2;
    constexpr Qt::ToolButtonStyle modes[]{Qt::ToolButtonIconOnly, Qt::ToolButtonTextBesideIcon,
                                          Qt::ToolButtonTextUnderIcon};
    m_toolBar->setToolButtonStyle(modes[value]);
    m_toolBar->setIconSize(value == 2 ? QSize(22, 22) : QSize(18, 18));
    if (value == 0) {
        m_toolBar->setStyleSheet(QStringLiteral(
            "QToolBar > QToolButton[toolbarAction=\"true\"] { min-width: 18px; padding: 5px; }"
            "QToolBar > QToolButton#queueButton { padding-right: 16px; }"));
    } else if (value == 1) {
        m_toolBar->setStyleSheet(QStringLiteral(
            "QToolBar > QToolButton[toolbarAction=\"true\"] { padding: 4px 7px; }"
            "QToolBar > QToolButton#queueButton { padding-right: 17px; }"));
    } else {
        m_toolBar->setStyleSheet(QString());
    }
}

void MainWindow::createMenus()
{
    using tgui::theme::Icon;
    // --- Torrent (пункты и порядок — по референсу transgui) ---
    auto* mTorrent = menuBar()->addMenu(tr("&Torrent"));

    // Connect to Transmission (подменю: New / Manage — как в OLD miConnect)
    auto* mConnect = mTorrent->addMenu(tr("Connect to Transmission"));
    connect(mConnect, &QMenu::aboutToShow, this, [this, mConnect]() {
        mConnect->clear();
        auto* aManage = mConnect->addAction(tr("Manage connections…"));
        connect(aManage, &QAction::triggered, this, &MainWindow::onConnectionOptions);
    });
    // Disconnect — сестринский пункт (как в OLD, не внутри подменю)
    mTorrent->addAction(m_actDisconnect);

    mTorrent->addSeparator();
    mTorrent->addAction(m_actAdd);
    mTorrent->addAction(m_actAddLink);
    mTorrent->addAction(m_actions.startAction());
    auto* actForceStart = mTorrent->addAction(tgui::theme::icon(Icon::ForceStart), tr("Force start"));
    actForceStart->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(actForceStart, &QAction::triggered, &m_actions, &TorrentActions::onStartForceSelected);
    mTorrent->addAction(m_actions.startAllAction());
    mTorrent->addAction(m_actions.stopAction());
    mTorrent->addAction(m_actions.stopAllAction());
    mTorrent->addAction(m_actions.removeAction());
    auto* actRemoveData = mTorrent->addAction(tr("Remove torrent and Data"));
    actRemoveData->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    connect(actRemoveData, &QAction::triggered, this, &MainWindow::onRemoveWithDataSelected);
    mTorrent->addSeparator();

    auto* mPriority = mTorrent->addMenu(tr("Priority"));
    auto* priHigh = mPriority->addAction(tr("High priority"));
    priHigh->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    auto* priNormal = mPriority->addAction(tr("Normal priority"));
    priNormal->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    auto* priLow = mPriority->addAction(tr("Low priority"));
    priLow->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(priHigh, &QAction::triggered, &m_actions, &TorrentActions::onPriorityHigh);
    connect(priNormal, &QAction::triggered, &m_actions, &TorrentActions::onPriorityNormal);
    connect(priLow, &QAction::triggered, &m_actions, &TorrentActions::onPriorityLow);

    auto* mQueue = mTorrent->addMenu(tr("Queue"));
    auto* qTop = mQueue->addAction(tr("Move top"));
    qTop->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1));
    auto* qUp = mQueue->addAction(tgui::theme::icon(Icon::QueueUp), tr("Move up"));
    qUp->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    auto* qDown = mQueue->addAction(tgui::theme::icon(Icon::QueueDown), tr("Move down"));
    qDown->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    auto* qBottom = mQueue->addAction(tr("Move bottom"));
    qBottom->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2));
    connect(qTop, &QAction::triggered, &m_actions, &TorrentActions::onQueueTop);
    connect(qUp, &QAction::triggered, this, &MainWindow::onQueueUp);
    connect(qDown, &QAction::triggered, this, &MainWindow::onQueueDown);
    connect(qBottom, &QAction::triggered, &m_actions, &TorrentActions::onQueueBottom);

    auto* actReannounce = mTorrent->addAction(tr("Reannounce (get more peers)"));
    actReannounce->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    auto* actVerify = mTorrent->addAction(tr("Verify"));
    connect(actReannounce, &QAction::triggered, &m_actions, &TorrentActions::onReannounceSelected);
    connect(actVerify, &QAction::triggered, &m_actions, &TorrentActions::onVerifySelected);
    mTorrent->addAction(m_actions.moveAction());  // "Set data location..."
    m_actChooseLabel = mTorrent->addAction(tgui::theme::icon(Icon::Label), tr("Choose label…"));
    m_details.generalForm()->setEditLabelsAction(m_actChooseLabel);
    m_actChooseLabel->setShortcut(QKeySequence(Qt::Key_F7));
    connect(m_actChooseLabel, &QAction::triggered, &m_actions, &TorrentActions::onSetLabels);
    auto* actRename = mTorrent->addAction(tr("Rename"));
    actRename->setShortcut(QKeySequence(Qt::Key_F2));
    connect(actRename, &QAction::triggered, &m_actions, &TorrentActions::onRenameSelected);
    mTorrent->addAction(m_actions.propertiesAction());
    mTorrent->addSeparator();
    auto* actExport = mTorrent->addAction(tr("Export settings…"));
    connect(actExport, &QAction::triggered, this, &MainWindow::onExportSettings);
    auto* actImport = mTorrent->addAction(tr("Import settings…"));
    connect(actImport, &QAction::triggered, this, &MainWindow::onImportSettings);
    auto* exportTorrents = mTorrent->addAction(tr("Export torrent list…"));
    connect(exportTorrents, &QAction::triggered, &m_list, &TorrentListController::onExportTorrentList);
    auto* importTorrents = mTorrent->addAction(tr("Import torrent list…"));
    connect(importTorrents, &QAction::triggered, &m_add, &AddTorrentCoordinator::onImportTorrentList);
    mTorrent->addSeparator();
    auto* actExit = mTorrent->addAction(tr("E&xit"));
    actExit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
    connect(actExit, &QAction::triggered, this, &MainWindow::onExit);

    // --- View (порядок — по референсу; Big Toolbar / Status bar - Sizes /
    // Menu в списке не показываем, как просили) ---
    auto* mView = menuBar()->addMenu(tr("&View"));
    mView->addAction(m_actAutoRefresh);
    auto* actSelectAll = mView->addAction(tr("Select all"));
    actSelectAll->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_A));
    connect(actSelectAll, &QAction::triggered, &m_list, &TorrentListController::onSelectAll);
    auto* actColumns = mView->addAction(tr("Setup columns…"));
    connect(actColumns, &QAction::triggered, &m_list, &TorrentListController::onColumns);
    mView->addSeparator();

    m_actFolderGrouping = mView->addAction(tr("Folder grouping"));
    m_actFolderGrouping->setCheckable(true);
    m_actFolderGrouping->setChecked(QSettings().value("ui/folderGrouping", true).toBool());
    connect(m_actFolderGrouping, &QAction::toggled, this, [this](bool on) {
        m_list.filterPanel()->setFolderGroupingEnabled(on);
        QSettings().setValue("ui/folderGrouping", on);
        if (!on && !m_list.filterPanel()->selectedFolder().isEmpty())
            m_list.filterPanel()->selectAllTorrents();
    });

    m_actLabelGrouping = mView->addAction(tr("Label grouping"));
    m_actLabelGrouping->setCheckable(true);
    m_actLabelGrouping->setChecked(QSettings().value("ui/labelGrouping", true).toBool());
    connect(m_actLabelGrouping, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue("ui/labelGrouping", on);
        updateLabelUi();
    });

    m_actTrackerGrouping = mView->addAction(tr("Tracker grouping"));
    m_actTrackerGrouping->setCheckable(true);
    m_actTrackerGrouping->setChecked(QSettings().value("ui/trackerGrouping", true).toBool());
    connect(m_actTrackerGrouping, &QAction::toggled, this, [this](bool on) {
        m_list.filterPanel()->setTrackerGroupingEnabled(on);
        QSettings().setValue("ui/trackerGrouping", on);
        if (!on && !m_list.filterPanel()->selectedTracker().isEmpty())
            m_list.filterPanel()->selectAllTorrents();
    });
    mView->addSeparator();

    // Видимость панелей. F11/F12 и Shift+F11/Shift+F12 работают и тогда,
    // когда меню/тулбар скрыты — действия добавлены в окно.
    // Ключи QSettings переименованы (ui/filterPane→ui/filterPanel и т.д.);
    // старое имя считывается как fallback, чтобы не потерять сохранённое состояние.
    auto uiBool = [](const char* key, const char* legacy, bool def) {
        QSettings s;
        if (s.contains(key))
            return s.value(key, def).toBool();
        if (s.contains(legacy))
            return s.value(legacy, def).toBool();
        return def;
    };

    m_actFilterPanel = mView->addAction(tr("Filter panel"));
    m_actFilterPanel->setCheckable(true);
    m_actFilterPanel->setShortcut(QKeySequence(Qt::Key_F11));
    m_actFilterPanel->setChecked(uiBool("ui/filterPanel", "ui/filterPane", true));
    connect(m_actFilterPanel, &QAction::toggled, this, [this](bool on) {
        m_list.filterPanel()->setVisible(on);
        QSettings().setValue("ui/filterPanel", on);
    });
    connect(m_list.filterPanel(), &FilterPanel::contextMenuRequested, this, [this](const QPoint& position) {
        QMenu menu(this);
        menu.addAction(m_actFolderGrouping);
        menu.addAction(m_actLabelGrouping);
        menu.addAction(m_actTrackerGrouping);
        menu.addSeparator();
        auto* expand = menu.addAction(tr("Expand all sections"));
        connect(expand, &QAction::triggered, m_list.filterPanel(), &FilterPanel::expandSections);
        auto* hide = menu.addAction(tr("Hide filter panel"));
        connect(hide, &QAction::triggered, this, [this] { m_actFilterPanel->setChecked(false); });
        menu.exec(position);
    });

    m_actInfoPanel = mView->addAction(tr("Info panel"));
    m_actInfoPanel->setCheckable(true);
    m_actInfoPanel->setShortcut(QKeySequence(Qt::Key_F12));
    m_actInfoPanel->setChecked(uiBool("ui/infoPanel", "ui/infoPane", true));
    connect(m_actInfoPanel, &QAction::toggled, this, [this](bool on) {
        m_details.setVisible(on);
        QSettings().setValue("ui/infoPanel", on);
        if (on)
            m_details.refreshFilesDetails();
    });

    m_actToolbar = mView->addAction(tr("Toolbar"));
    m_actToolbar->setCheckable(true);
    m_actToolbar->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
    m_actToolbar->setChecked(QSettings().value("ui/showToolbar", true).toBool());
    connect(m_actToolbar, &QAction::toggled, this, [this](bool on) {
        m_toolBar->setVisible(on);
        QSettings().setValue("ui/showToolbar", on);
    });

    m_actStatusBar = mView->addAction(tr("Status bar"));
    m_actStatusBar->setCheckable(true);
    m_actStatusBar->setChecked(QSettings().value("ui/statusBar", true).toBool());
    connect(m_actStatusBar, &QAction::toggled, this, [this](bool on) {
        statusBar()->setVisible(on);
        QSettings().setValue("ui/statusBar", on);
    });

    // «Menu» убран из видимого меню, но остаётся действием с Shift+F12,
    // чтобы скрытое меню можно было вернуть.
    m_actMenu = new QAction(tr("Menu"), this);
    m_actMenu->setCheckable(true);
    m_actMenu->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F12));
    m_actMenu->setChecked(QSettings().value("ui/showMenu", true).toBool());
    connect(m_actMenu, &QAction::toggled, this, [this](bool on) {
        menuBar()->setVisible(on);
        QSettings().setValue("ui/showMenu", on);
    });
    addAction(m_actMenu);

    applyViewVisibility();

    // --- Tools (порядок — точное соответствие OLD) ---
    auto* mTools = menuBar()->addMenu(tr("T&ools"));
    addSpeedLimitMenu(mTools, true);
    addSpeedLimitMenu(mTools, false);
    mTools->addAction(m_actDaemonOptions);
    mTools->addAction(m_actOptions);
    mTools->addSeparator();
    mTools->addAction(m_actAltSpeed);  // "Use alternate bandwidth settings"
    auto* actBlocklist = mTools->addAction(tr("Update blocklist"));
    connect(actBlocklist, &QAction::triggered, &m_actions, &TorrentActions::onUpdateBlocklist);
    auto* actGeoIp = mTools->addAction(tr("Update GeoIP database"));
    connect(actGeoIp, &QAction::triggered, this, [this] { m_geoIp.onUpdateGeoIp(); });
    // --- Help (порядок — по референсу transgui) ---
    auto* mHelp = menuBar()->addMenu(tr("&Help"));
    auto* actCheck = mHelp->addAction(tr("Check for updates"));
    connect(actCheck, &QAction::triggered, this, &MainWindow::onCheckUpdate);
    auto* actHome = mHelp->addAction(tr("Visit home page"));
    connect(actHome, &QAction::triggered, this, &MainWindow::onHome);
    auto* actDev = mHelp->addAction(tr("Development site"));
    connect(actDev, &QAction::triggered, this, &MainWindow::onDevSite);
    mHelp->addSeparator();
    mHelp->addAction(m_actAbout);
}

/** Применяет сохранённую видимость панелей. */
void MainWindow::applyViewVisibility()
{
    if (m_actFilterPanel && m_list.filterPanel())
        m_list.filterPanel()->setVisible(m_actFilterPanel->isChecked());
    updateLabelUi();
    if (m_actInfoPanel)
        m_details.setVisible(m_actInfoPanel->isChecked());
    if (m_actToolbar && m_toolBar)
        m_toolBar->setVisible(m_actToolbar->isChecked());
    if (m_actStatusBar)
        statusBar()->setVisible(m_actStatusBar->isChecked());
    if (m_actMenu)
        menuBar()->setVisible(m_actMenu->isChecked());
}
