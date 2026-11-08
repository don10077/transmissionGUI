#include "mainwindow.h"

#include <QAction>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include "adddialog.h"
#include "connectdialog.h"
#include "fileselectdialog.h"
#include "tgui/util.h"

namespace {
constexpr int kRefreshIntervalMs = 2000;
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("transmissionGUI"));
    resize(1000, 640);

    m_proxy.setSourceModel(&m_model);
    m_groupProxy.setSourceProxy(&m_proxy);
    createUi();
    createActions();
    createTray();

    m_refreshTimer.setInterval(kRefreshIntervalMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefresh);

    {
        QSettings s;
        const bool autoRefresh = s.value("ui/autoRefresh", true).toBool();
        if (m_actAutoRefresh->isChecked() != autoRefresh)
            m_actAutoRefresh->setChecked(autoRefresh);
    }

    setConnectedUi(false);
    statusBar()->showMessage(tr("Не подключено"), 5000);
    maybeAutoConnect();
}

void MainWindow::createUi()
{
    m_view = new QTreeView(this);
    m_view->setModel(&m_groupProxy);
    m_view->setSortingEnabled(true);
    m_view->setAlternatingRowColors(true);
    m_view->setRootIsDecorated(false);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setUniformRowHeights(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &MainWindow::onContextMenu);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->setColumnWidth(TorrentModel::ColName, 320);
    m_view->setColumnWidth(TorrentModel::ColStatus, 130);
    m_view->setColumnWidth(TorrentModel::ColProgress, 70);
    m_view->setColumnWidth(TorrentModel::ColSize, 90);
    m_view->setColumnWidth(TorrentModel::ColRateDown, 90);
    m_view->setColumnWidth(TorrentModel::ColRateUp, 90);
    m_view->setColumnWidth(TorrentModel::ColEta, 110);
    m_view->setColumnWidth(TorrentModel::ColPeers, 60);
    m_view->setColumnWidth(TorrentModel::ColRatio, 70);
    m_view->setColumnWidth(TorrentModel::ColLabels, 100);

    m_statusFilter = new QComboBox(this);
    m_statusFilter->addItem(tr("Все"), -1);
    m_statusFilter->addItem(tr("Загрузка"), TorrentModel::GroupDownloading);
    m_statusFilter->addItem(tr("Раздача"), TorrentModel::GroupSeeding);
    m_statusFilter->addItem(tr("Остановленные"), TorrentModel::GroupStopped);
    m_statusFilter->addItem(tr("Ожидают"), TorrentModel::GroupWaiting);
    m_statusFilter->addItem(tr("Завершено"), TorrentModel::GroupFinished);
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this](int) {
        m_proxy.setStatusGroup(m_statusFilter->currentData().toInt());
    });

    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(tr("Поиск по имени"));
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy.setSearchText(text.trimmed());
    });

    m_grouping = new QComboBox(this);
    m_grouping->addItem(tr("Без группировки"), GroupedTorrentProxy::GroupNone);
    m_grouping->addItem(tr("По папке"), GroupedTorrentProxy::GroupFolder);
    m_grouping->addItem(tr("По лейблу"), GroupedTorrentProxy::GroupLabel);
    m_grouping->addItem(tr("По трекеру"), GroupedTorrentProxy::GroupTracker);
    m_grouping->setCurrentIndex(m_grouping->findData(
        QSettings().value("ui/grouping", GroupedTorrentProxy::GroupNone).toInt()));
    connect(m_grouping, &QComboBox::currentIndexChanged, this, [this](int) {
        m_groupProxy.setGroupBy(static_cast<GroupedTorrentProxy::GroupBy>(
            m_grouping->currentData().toInt()));
        QSettings().setValue("ui/grouping", m_grouping->currentData().toInt());
    });

    m_countLabel = new QLabel(this);
    m_countLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(6, 4, 6, 4);
    filterLayout->addWidget(new QLabel(tr("Группировка:"), this));
    filterLayout->addWidget(m_grouping);
    filterLayout->addWidget(m_statusFilter);
    filterLayout->addWidget(m_search, 1);
    filterLayout->addWidget(m_countLabel);
    auto* filterPanel = new QWidget(this);
    filterPanel->setLayout(filterLayout);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(filterPanel);
    layout->addWidget(m_view, 1);
    setCentralWidget(central);

    m_daemonLabel = new QLabel(this);
    m_speedLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_daemonLabel);
    statusBar()->addPermanentWidget(m_speedLabel);
}

void MainWindow::createActions()
{
    m_actConnect = new QAction(tr("Подключить"), this);
    m_actConnect->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(m_actConnect, &QAction::triggered, this, &MainWindow::onConnect);

    m_actDisconnect = new QAction(tr("Отключить"), this);
    connect(m_actDisconnect, &QAction::triggered, this, &MainWindow::onDisconnect);

    m_actAdd = new QAction(tr("Добавить торрент…"), this);
    m_actAdd->setShortcut(QKeySequence::Open);
    m_actAdd->setEnabled(false);
    connect(m_actAdd, &QAction::triggered, this, &MainWindow::onAddTorrent);

    m_actStart = new QAction(tr("Запустить"), this);
    connect(m_actStart, &QAction::triggered, this, &MainWindow::onStartSelected);

    m_actStop = new QAction(tr("Остановить"), this);
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStopSelected);

    m_actStartAll = new QAction(tr("Запустить все"), this);
    connect(m_actStartAll, &QAction::triggered, this, &MainWindow::onStartAll);

    m_actStopAll = new QAction(tr("Остановить все"), this);
    connect(m_actStopAll, &QAction::triggered, this, &MainWindow::onStopAll);

    m_actRemove = new QAction(tr("Удалить"), this);
    m_actRemove->setShortcut(QKeySequence::Delete);
    connect(m_actRemove, &QAction::triggered, this, &MainWindow::onRemoveSelected);

    m_actRefresh = new QAction(tr("Обновить"), this);
    m_actRefresh->setShortcut(QKeySequence::Refresh);
    connect(m_actRefresh, &QAction::triggered, this, &MainWindow::onRefresh);

    m_actAutoRefresh = new QAction(tr("Автообновление"), this);
    m_actAutoRefresh->setCheckable(true);
    m_actAutoRefresh->setChecked(true);
    connect(m_actAutoRefresh, &QAction::toggled, this, [this](bool on) {
        if (on && m_connected)
            m_refreshTimer.start();
        else
            m_refreshTimer.stop();
        QSettings().setValue("ui/autoRefresh", on);
    });

    m_actAbout = new QAction(tr("О программе"), this);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::onAbout);

    auto* tb = addToolBar(tr("Главная"));
    tb->setMovable(false);
    tb->addAction(m_actConnect);
    tb->addAction(m_actDisconnect);
    tb->addSeparator();
    tb->addAction(m_actAdd);
    tb->addSeparator();
    tb->addAction(m_actStart);
    tb->addAction(m_actStop);
    tb->addAction(m_actStartAll);
    tb->addAction(m_actStopAll);
    tb->addAction(m_actRemove);
    tb->addSeparator();
    tb->addAction(m_actRefresh);
    tb->addAction(m_actAutoRefresh);
    tb->addSeparator();
    tb->addAction(m_actAbout);
}

void MainWindow::setConnectedUi(bool on)
{
    m_actConnect->setEnabled(!on);
    m_actDisconnect->setEnabled(on);
    m_actAdd->setEnabled(on);
    m_actRefresh->setEnabled(on);
    m_actAutoRefresh->setEnabled(on);
    m_actStart->setEnabled(on);
    m_actStop->setEnabled(on);
    m_actStartAll->setEnabled(on);
    m_actStopAll->setEnabled(on);
    m_actRemove->setEnabled(on);
}

void MainWindow::onConnect()
{
    tgui::RpcClient::Options opts;
    loadSettings(opts);

    ConnectDialog dlg(opts, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    opts = dlg.options();
    if (opts.url.isEmpty()) {
        QMessageBox::warning(this, tr("Подключение"), tr("Укажите адрес демона."));
        return;
    }
    saveSettings(opts);
    startConnect(opts);
}

void MainWindow::startConnect(const tgui::RpcClient::Options& opts)
{
    m_rpc = std::make_unique<tgui::RpcClient>(opts, this);
    connect(m_rpc.get(), &tgui::RpcClient::connected, this, &MainWindow::onConnected);
    connect(m_rpc.get(), &tgui::RpcClient::connectionFailed, this,
            &MainWindow::onConnectionFailed);
    connect(m_rpc.get(), &tgui::RpcClient::torrentsReceived, this,
            &MainWindow::onTorrentsReceived);
    connect(m_rpc.get(), &tgui::RpcClient::torrentDetailsReceived, this,
            &MainWindow::onTorrentDetailsReceived);
    connect(m_rpc.get(), &tgui::RpcClient::readError, this, &MainWindow::onReadError);
    connect(m_rpc.get(), &tgui::RpcClient::requestCompleted, this,
            &MainWindow::onRequestCompleted);

    statusBar()->showMessage(tr("Подключение к %1…").arg(opts.url.toString()), 5000);
    m_rpc->connectToDaemon();
}

void MainWindow::setAutoConnect(const tgui::RpcClient::Options& opts)
{
    if (opts.url.isEmpty())
        return;
    m_autoConnectOpts = opts;
    // Вызывается после конструктора — подключаемся сразу.
    if (!m_rpc && !m_connected) {
        saveSettings(opts);
        startConnect(opts);
    }
}

void MainWindow::maybeAutoConnect()
{
    if (m_autoConnectOpts.url.isEmpty())
        return;
    saveSettings(m_autoConnectOpts);
    startConnect(m_autoConnectOpts);
}

void MainWindow::onDisconnect()
{
    m_refreshTimer.stop();
    m_connected = false;
    m_rpc.reset();
    m_model.setTorrents({});
    setConnectedUi(false);
    m_daemonLabel->clear();
    m_speedLabel->clear();
    statusBar()->showMessage(tr("Не подключено"), 5000);
}

void MainWindow::onConnected(const tgui::SessionInfo& info)
{
    m_connected = true;
    m_daemonLabel->setText(tr("Transmission %1, RPC %2").arg(info.version).arg(info.rpcVersion));
    setConnectedUi(true);
    if (m_actAutoRefresh->isChecked())
        m_refreshTimer.start();
    m_rpc->getTorrents();
    statusBar()->showMessage(tr("Подключено"), 3000);
}

void MainWindow::onConnectionFailed(const QString& error)
{
    m_refreshTimer.stop();
    m_connected = false;
    setConnectedUi(false);
    m_daemonLabel->clear();
    const QString url = m_rpc ? m_rpc->options().url.toString() : QString();
    // Отложенная очистка: destroy-им клиента после завершения цепочки сигналов.
    QTimer::singleShot(0, this, [this, error, url] {
        m_rpc.reset();
        m_model.setTorrents({});
        QMessageBox::critical(this, tr("Не удалось подключиться"),
                              tr("Адрес: %1\n\nПричина: %2").arg(url, error));
        statusBar()->showMessage(tr("Не подключено"), 5000);
    });
}

void MainWindow::onRefresh()
{
    if (m_rpc && m_connected)
        m_rpc->getTorrents();
}

void MainWindow::onTorrentsReceived(const QVector<tgui::Torrent>& torrents)
{
    m_model.setTorrents(torrents);
    m_countLabel->setText(tr("%1 элементов").arg(torrents.size()));
    updateStatusBarTotals();
}

void MainWindow::onReadError(const QString& what, const QString& error)
{
    statusBar()->showMessage(tr("%1: %2").arg(what, error), 8000);
}

void MainWindow::onRequestCompleted(const QString& method, bool ok, const QString& error,
                                    const QJsonObject& arguments)
{
    if (method == QLatin1String("torrent-add")) {
        if (!ok) {
            QMessageBox::warning(this, tr("Добавление торрента"),
                                 error.isEmpty() ? tr("Неизвестная ошибка.") : error);
            m_pendingAddId = -1;
            return;
        }
        int id = -1;
        const QJsonObject added = arguments.value("torrent-added").toObject();
        if (added.contains("id"))
            id = added.value("id").toInt();
        if (id < 0)
            id = arguments.value("id").toInt();
        if (id < 0)
            id = arguments.value("torrent-id").toInt();
        if (id >= 0) {
            m_pendingAddId = id;
            m_pendingStart = m_pendingAdd.start;
            if (m_pendingAdd.source == tgui::RpcClient::AddTorrent::Source::Url)
                finishAdd(); // magnet/URL — списка файлов нет
            else if (m_rpc)
                m_rpc->getTorrentDetails(id); // файлы → диалог want/skip
        }
        return;
    }
    Q_UNUSED(arguments);
    if (ok)
        statusBar()->showMessage(tr("«%1» — выполнено").arg(method), 5000);
    else
        statusBar()->showMessage(tr("«%1»: %2").arg(method, error), 8000);
}

QVector<int> MainWindow::selectedTorrentIds() const
{
    QVector<int> ids;
    if (!m_view->selectionModel())
        return ids;
    const auto indexes = m_view->selectionModel()->selectedIndexes();
    for (const QModelIndex& vi : indexes) {
        const QModelIndex fi = m_groupProxy.mapToSource(vi);
        if (!fi.isValid())
            continue;
        const QModelIndex src = m_proxy.mapToSource(fi);
        if (!src.isValid())
            continue;
        const auto& t = m_model.torrentAt(src.row());
        if (!ids.contains(t.id))
            ids.push_back(t.id);
    }
    return ids;
}

void MainWindow::onStartSelected()
{
    if (!m_rpc)
        return;
    for (int id : selectedTorrentIds())
        m_rpc->startTorrent(id);
}

void MainWindow::onStopSelected()
{
    if (!m_rpc)
        return;
    for (int id : selectedTorrentIds())
        m_rpc->stopTorrent(id);
}

void MainWindow::onStartAll()
{
    if (m_rpc)
        m_rpc->startAll();
}

void MainWindow::onStopAll()
{
    if (m_rpc)
        m_rpc->stopAll();
}

void MainWindow::onRemoveSelected()
{
    if (!m_rpc)
        return;
    const auto ids = selectedTorrentIds();
    if (ids.isEmpty())
        return;

    auto* box = new QMessageBox(this);
    box->setWindowTitle(tr("Удаление торрентов"));
    box->setIcon(QMessageBox::Question);
    box->setText(ids.size() == 1
                     ? tr("Удалить торрент из списка демона?")
                     : tr("Удалить %1 торрентов из списка демона?").arg(ids.size()));
    box->setInformativeText(tr("Можно также удалить скачанные данные на диске."));
    box->setStandardButtons(QMessageBox::Cancel);
    QAbstractButton* btnRemove = box->addButton(tr("Удалить"), QMessageBox::AcceptRole);
    QAbstractButton* btnRemoveData =
        box->addButton(tr("Удалить с данными"), QMessageBox::DestructiveRole);
    box->exec();
    const auto clicked = box->clickedButton();
    box->deleteLater();
    if (clicked == btnRemove || clicked == btnRemoveData) {
        const bool deleteData = (clicked == btnRemoveData);
        for (int id : ids)
            m_rpc->removeTorrent(id, deleteData);
    }
}

void MainWindow::updateStatusBarTotals()
{
    double down = 0.0;
    double up = 0.0;
    for (const auto& t : m_model.torrents()) {
        down += t.rateDownload.value_or(0);
        up += t.rateUpload.value_or(0);
    }
    m_speedLabel->setText(tr("↓ %1  ↑ %2")
                              .arg(QString::fromStdString(tgui::formatRate(down)))
                              .arg(QString::fromStdString(tgui::formatRate(up))));
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this, tr("О программе"),
        tr("<b>transmissionGUI</b> %1<br/>"
           "GUI-клиент демона Transmission (C++/Qt 6).<br/><br/>"
           "Лицензия: GPL v3<br/>"
           "Почта: <a href=\"mailto:don10077@mail.ru\">don10077@mail.ru</a>")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::onContextMenu(const QPoint& pos)
{
    Q_UNUSED(pos);
    auto* menu = new QMenu(this);
    auto* actStart = menu->addAction(tr("Запустить"));
    auto* actStop = menu->addAction(tr("Остановить"));
    menu->addSeparator();
    auto* actRemove = menu->addAction(tr("Удалить…"));
    menu->addSeparator();
    auto* actRefresh = menu->addAction(tr("Обновить"));
    auto* actSelectAll = menu->addAction(tr("Выбрать все"));

    const bool on = m_connected;
    actStart->setEnabled(on);
    actStop->setEnabled(on);
    actRemove->setEnabled(on);
    actRefresh->setEnabled(on);

    const QPoint global = m_view->mapToGlobal(pos);
    QObject::connect(actSelectAll, &QAction::triggered, this, [this] { m_view->selectAll(); });
    QObject::connect(actStart, &QAction::triggered, this, [this] { onStartSelected(); });
    QObject::connect(actStop, &QAction::triggered, this, [this] { onStopSelected(); });
    QObject::connect(actRemove, &QAction::triggered, this, [this] { onRemoveSelected(); });
    QObject::connect(actRefresh, &QAction::triggered, this, [this] { onRefresh(); });
    menu->exec(global);
    menu->deleteLater();
}

void MainWindow::onAddTorrent()
{
    if (!m_rpc)
        return;
    const auto& s = m_rpc->sessionInfo();
    AddTorrentDialog dlg(QString(), s.downloadDir, m_rpc->rpcVersion(),
                         s.startAddedTorrents, m_rpc.get(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    startAdd(dlg.params(), dlg.saveEnabled(), dlg.savePath());
}

void MainWindow::startAdd(const tgui::RpcClient::AddTorrent& params, bool doSave,
                          const QString& savePath)
{
    if (!m_rpc)
        return;
    m_pendingAdd = params;
    m_pendingAddId = -1;

    if (doSave && !savePath.isEmpty()) {
        if (params.source == tgui::RpcClient::AddTorrent::Source::File) {
            if (!QFile::copy(params.file, savePath)) {
                QMessageBox::warning(this, tr("Сохранение"),
                                     tr("Не удалось сохранить файл: %1").arg(savePath));
                return;
            }
        } else if (params.source == tgui::RpcClient::AddTorrent::Source::Url &&
                   (params.url.startsWith(QLatin1String("http://")) ||
                    params.url.startsWith(QLatin1String("https://")))) {
            downloadAndAdd(params, savePath);
            return;
        }
        // magnet — сохранять нечего.
    }
    m_rpc->addTorrent(params);
}

void MainWindow::downloadAndAdd(const tgui::RpcClient::AddTorrent& params,
                                const QString& dst)
{
    statusBar()->showMessage(tr("Скачивание %1…").arg(params.url), 5000);
    auto* reply = m_nam.get(QNetworkRequest(QUrl(params.url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, params, dst] {
        const bool netOk = reply->error() == QNetworkReply::NoError;
        const QByteArray payload = reply->readAll();
        if (netOk) {
            QFile f(dst);
            if (!f.open(QIODevice::WriteOnly) || f.write(payload) < 0)
                QMessageBox::warning(this, tr("Сохранение"),
                                     tr("Не удалось записать файл: %1").arg(dst));
        } else {
            QMessageBox::warning(this, tr("Скачивание"), reply->errorString());
        }
        reply->deleteLater();
        if (m_rpc)
            m_rpc->addTorrent(params); // демон сам загрузит торрент
    });
}

void MainWindow::onTorrentDetailsReceived(const tgui::TorrentDetails& details)
{
    if (!m_rpc || details.id != m_pendingAddId)
        return;
    if (!details.files.isEmpty()) {
        FileSelectDialog dlg(details.files, this);
        if (dlg.exec() == QDialog::Accepted)
            m_rpc->setFilesWanted(details.id, dlg.wanted(), dlg.unwanted());
    }
    finishAdd();
}

void MainWindow::finishAdd()
{
    if (m_rpc) {
        if (m_pendingAddId >= 0 && m_pendingStart)
            m_rpc->startTorrent(m_pendingAddId);
        m_rpc->getTorrents();
        statusBar()->showMessage(tr("Торрент добавлен"), 5000);
    }
    m_pendingAddId = -1;
    m_pendingStart = false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!m_rpc || !m_connected)
        return;

    QStringList items;
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& u : event->mimeData()->urls())
            items << u.toString();
    } else if (event->mimeData()->hasText()) {
        for (const QString& line : event->mimeData()->text().split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty())
                items << line.trimmed();
    }

    const auto& s = m_rpc->sessionInfo();
    for (const QString& sItem : items) {
        if (sItem.startsWith(QLatin1String("magnet:")) ||
            sItem.startsWith(QLatin1String("http://")) ||
            sItem.startsWith(QLatin1String("https://"))) {
            tgui::RpcClient::AddTorrent p;
            p.source = tgui::RpcClient::AddTorrent::Source::Url;
            p.url = sItem;
            p.downloadDir = s.downloadDir;
            p.start = s.startAddedTorrents;
            startAdd(p, false, QString());
        } else {
            QString path = sItem;
            if (sItem.startsWith(QLatin1String("file:")))
                path = QUrl(sItem).toLocalFile();
            if (QFile::exists(path)) {
                tgui::RpcClient::AddTorrent p;
                p.source = tgui::RpcClient::AddTorrent::Source::File;
                p.file = path;
                p.downloadDir = s.downloadDir;
                p.start = s.startAddedTorrents;
                startAdd(p, false, QString());
            }
        }
    }
    event->acceptProposedAction();
}

void MainWindow::createTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeTrayIcon());
    setWindowIcon(makeTrayIcon());
    m_tray->setToolTip(QStringLiteral("transmissionGUI"));

    auto* menu = new QMenu(this);
    auto* actShow = menu->addAction(tr("Показать окно"));
    auto* actStartAll = menu->addAction(tr("Запустить все"));
    auto* actStopAll = menu->addAction(tr("Остановить все"));
    menu->addSeparator();
    auto* actRefresh = menu->addAction(tr("Обновить"));
    menu->addSeparator();
    auto* actExit = menu->addAction(tr("Выход"));

    QObject::connect(actShow, &QAction::triggered, this, [this] {
        show();
        activateWindow();
    });
    QObject::connect(actStartAll, &QAction::triggered, this, [this] { onStartAll(); });
    QObject::connect(actStopAll, &QAction::triggered, this, [this] { onStopAll(); });
    QObject::connect(actRefresh, &QAction::triggered, this, [this] { onRefresh(); });
    QObject::connect(actExit, &QAction::triggered, qApp, &QApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick ||
                    reason == QSystemTrayIcon::Trigger) {
                    show();
                    activateWindow();
                }
            });
    m_tray->show();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Закрытие = сворачивание в трей; выход — по пункту меню.
    if (m_tray && m_tray->isVisible()) {
        event->ignore();
        hide();
        return;
    }
    QMainWindow::closeEvent(event);
}

QPixmap MainWindow::makeTrayIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(63, 143, 216));
    p.drawRoundedRect(2, 2, 60, 60, 14, 14);
    QPen pen(Qt::white, 7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawLine(22, 16, 22, 44); // стрелка вниз
    p.drawLine(22, 44, 12, 32);
    p.drawLine(22, 44, 32, 32);
    p.drawLine(42, 48, 42, 20); // стрелка вверх
    p.drawLine(42, 20, 32, 32);
    p.drawLine(42, 20, 52, 32);
    return pm;
}

void MainWindow::loadSettings(tgui::RpcClient::Options& opts)
{
    QSettings s;
    const QString url = s.value("connection/url").toString();
    if (!url.isEmpty())
        opts.url = QUrl(url);
    opts.rpcPath = s.value("connection/rpcPath").toString();
    opts.username = s.value("connection/user").toString();
    opts.password = s.value("connection/password").toString();
    opts.ignoreSslErrors = s.value("connection/ignoreSsl", false).toBool();
}

void MainWindow::saveSettings(const tgui::RpcClient::Options& opts)
{
    QSettings s;
    s.setValue("connection/url", opts.url.toString());
    s.setValue("connection/rpcPath", opts.rpcPath);
    s.setValue("connection/user", opts.username);
    s.setValue("connection/password", opts.password);
    s.setValue("connection/ignoreSsl", opts.ignoreSslErrors);
}
