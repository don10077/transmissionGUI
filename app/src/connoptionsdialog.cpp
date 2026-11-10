#include "connoptionsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>

#include "tgui/rpcclient.h"

namespace {
struct ConnTest {
    bool ok = false;
    QString message;
};

// Синхронная проверка соединения (session-get) с таймаутом.
ConnTest runTest(const tgui::RpcClient::Options& opts)
{
    ConnTest r;
    auto* client = new tgui::RpcClient(opts);
    QEventLoop loop;
    bool done = false;
    QObject::connect(client, &tgui::RpcClient::connected, [&](const tgui::SessionInfo& info) {
        if (done) return;
        done = true;
        r.ok = true;
        r.message = QObject::tr("Connected: Transmission %1 (RPC %2)").arg(info.version).arg(info.rpcVersion);
        loop.quit();
    });
    QObject::connect(client, &tgui::RpcClient::authenticationFailed, [&](const QString& e) {
        if (done) return;
        done = true;
        r.message = QObject::tr("Authentication failed: %1").arg(e);
        loop.quit();
    });
    QObject::connect(client, &tgui::RpcClient::connectionFailed, [&](const QString& e) {
        if (done) return;
        done = true;
        r.message = e;
        loop.quit();
    });
    QObject::connect(client, &tgui::RpcClient::readError, [&](const QString&, const QString& e) {
        if (done) return;
        done = true;
        r.message = e;
        loop.quit();
    });
    auto* timer = new QTimer(&loop);
    QObject::connect(timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer->start(8000);
    client->connectToDaemon();
    loop.exec();
    timer->deleteLater();
    client->deleteLater();
    if (!r.ok && r.message.isEmpty())
        r.message = QObject::tr("No response (timeout).");
    return r;
}
}  // namespace

ConnOptionsDialog::ConnOptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Connection options"));
    resize(740, 480);

    // --- Левая колонка: список профилей + New/Del/Rename ---
    m_list = new QListWidget(this);
    m_list->setMinimumWidth(150);
    m_list->setMaximumWidth(220);
    auto* btnNew = new QPushButton(tr("New…"), this);
    auto* btnDel = new QPushButton(tr("Delete"), this);
    auto* btnRename = new QPushButton(tr("Rename…"), this);
    auto* left = new QVBoxLayout;
    left->addWidget(m_list, 1);
    left->addWidget(btnNew);
    left->addWidget(btnDel);
    left->addWidget(btnRename);

    // --- Правая колонка: форма выбранного профиля ---
    auto* connection = new QWidget(this);
    auto* form = new QFormLayout(connection);
    form->setContentsMargins(16, 16, 16, 16);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_host = new QLineEdit(this);
    form->addRow(tr("Host"), m_host);
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    form->addRow(tr("Port"), m_port);
    m_protocol = new QComboBox(this);
    m_protocol->addItem(tr("HTTP"), QStringLiteral("http"));
    m_protocol->addItem(tr("HTTPS (TLS)"), QStringLiteral("https"));
    form->addRow(tr("Protocol"), m_protocol);
    m_rpcPath = new QLineEdit(this);
    form->addRow(tr("RPC path"), m_rpcPath);
    m_user = new QLineEdit(this);
    form->addRow(tr("Username"), m_user);
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password"), m_pass);
    m_ignoreSsl = new QCheckBox(tr("Ignore SSL certificate errors"), this);
    form->addRow(QString(), m_ignoreSsl);

    // Прокси (расширенные).
    auto* proxy = new QGroupBox(tr("Proxy"), this);
    auto* pform = new QFormLayout(proxy);
    m_useProxy = new QCheckBox(tr("Use proxy"), proxy);
    m_useSocks = new QCheckBox(tr("SOCKS5"), proxy);
    m_proxyHost = new QLineEdit(proxy);
    m_proxyPort = new QSpinBox(proxy);
    m_proxyPort->setRange(1, 65535);
    m_proxyUser = new QLineEdit(proxy);
    m_proxyPass = new QLineEdit(proxy);
    m_proxyPass->setEchoMode(QLineEdit::Password);
    pform->addRow(QString(), m_useProxy);
    pform->addRow(QString(), m_useSocks);
    pform->addRow(tr("Host"), m_proxyHost);
    pform->addRow(tr("Port"), m_proxyPort);
    pform->addRow(tr("Username"), m_proxyUser);
    pform->addRow(tr("Password"), m_proxyPass);


    auto* btnTest = new QPushButton(tr("Test connection"), this);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto* right = new QVBoxLayout;
    auto* tabs = new QTabWidget(this);
    tabs->addTab(connection, tr("Transmission"));
    auto* proxyPage = new QWidget(tabs);
    auto* proxyLayout = new QVBoxLayout(proxyPage);
    proxyLayout->setContentsMargins(12, 12, 12, 12);
    proxyLayout->addWidget(proxy); proxyLayout->addStretch();
    tabs->addTab(proxyPage, tr("Proxy"));
    right->addWidget(tabs, 1);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    body->addLayout(left, 1); body->addLayout(right, 3);
    root->addLayout(body, 1);
    auto* footer = new QHBoxLayout;
    footer->addWidget(btnTest); footer->addStretch(); footer->addWidget(buttons);
    root->addLayout(footer);

    connect(m_list, &QListWidget::currentItemChanged, this, &ConnOptionsDialog::onProfileSelected);
    connect(btnNew, &QPushButton::clicked, this, &ConnOptionsDialog::onNew);
    connect(btnDel, &QPushButton::clicked, this, &ConnOptionsDialog::onDelete);
    connect(btnRename, &QPushButton::clicked, this, &ConnOptionsDialog::onRename);
    connect(btnTest, &QPushButton::clicked, this, &ConnOptionsDialog::onTest);
    connect(m_useProxy, &QCheckBox::toggled, this, [this](bool on) {
        m_useSocks->setEnabled(on);
        m_proxyHost->setEnabled(on);
        m_proxyPort->setEnabled(on);
        m_proxyUser->setEnabled(on);
        m_proxyPass->setEnabled(on);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveCurrent();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setProfile(DaemonProfile{});
    connect(m_protocol, &QComboBox::currentIndexChanged, this, [this] {
        m_ignoreSsl->setEnabled(m_protocol->currentData().toString() == "https");
    });
    refreshList();
    const QString cur = m_store.currentName();
    const auto names = m_store.names();
    selectProfile(cur.isEmpty() ? (names.isEmpty() ? QString() : names.first()) : cur);
}

void ConnOptionsDialog::refreshList()
{
    m_list->blockSignals(true);
    m_list->clear();
    for (const QString& n : m_store.names())
        m_list->addItem(n);
    m_list->blockSignals(false);
}

void ConnOptionsDialog::selectProfile(const QString& name)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->text() == name) {
            m_list->setCurrentRow(i);
            return;
        }
    }
    m_list->setCurrentRow(-1);
}

void ConnOptionsDialog::onProfileSelected()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    setProfile(m_store.profile(m_list->item(row)->text()));
}

void ConnOptionsDialog::setProfile(const DaemonProfile& p)
{
    m_host->setText(p.host);
    m_port->setValue(p.port > 0 ? p.port : 9091);
    const int pi = m_protocol->findData(p.useSsl ? QStringLiteral("https") : QStringLiteral("http"));
    if (pi >= 0)
        m_protocol->setCurrentIndex(pi);
    m_rpcPath->setText(p.rpcPath);
    m_user->setText(p.username);
    m_pass->setText(p.password);
    m_ignoreSsl->setChecked(p.ignoreSslErrors);
    m_useProxy->setChecked(p.useProxy);
    m_useSocks->setChecked(p.useSocks);
    m_proxyHost->setText(p.proxyHost);
    m_proxyPort->setValue(p.proxyPort > 0 ? p.proxyPort : 8080);
    m_proxyUser->setText(p.proxyUser);
    m_proxyPass->setText(p.proxyPass);
    for (QWidget* widget : QList<QWidget*>{m_useSocks, m_proxyHost, m_proxyPort,
                             m_proxyUser, m_proxyPass}) widget->setEnabled(p.useProxy);
    m_ignoreSsl->setEnabled(p.useSsl);
}

DaemonProfile ConnOptionsDialog::current() const
{
    DaemonProfile p;
    p.name = m_list->currentItem() ? m_list->currentItem()->text() : QString();
    p.host = m_host->text().trimmed();
    p.port = m_port->value();
    p.useSsl = (m_protocol->currentData().toString() == "https");
    p.rpcPath = m_rpcPath->text().trimmed();
    p.username = m_user->text();
    p.password = m_pass->text();
    p.ignoreSslErrors = m_ignoreSsl->isChecked();
    p.useProxy = m_useProxy->isChecked();
    p.useSocks = m_useProxy->isChecked() && m_useSocks->isChecked();
    p.proxyHost = m_useProxy->isChecked() ? m_proxyHost->text().trimmed() : QString();
    p.proxyPort = m_proxyPort->value();
    p.proxyUser = m_useProxy->isChecked() ? m_proxyUser->text() : QString();
    p.proxyPass = m_useProxy->isChecked() ? m_proxyPass->text() : QString();
    return p;
}

void ConnOptionsDialog::saveCurrent()
{
    DaemonProfile p = current();
    if (p.name.isEmpty())
        return;
    m_store.save(p);
    // Профиль, который редактируем, становится активным (аналог OLD: выбранный
    // в списке = активное соединение). Иначе switchToProfile не узнал бы о нём.
    m_store.setCurrent(p.name);
}

void ConnOptionsDialog::onNew()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New daemon"), tr("Name"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    const QString n = name.trimmed();
    if (m_store.exists(n)) {
        QMessageBox::warning(this, tr("New daemon"), tr("A daemon named '%1' already exists.").arg(n));
        return;
    }
    m_store.add(n);
    refreshList();
    selectProfile(n);
}

void ConnOptionsDialog::onDelete()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    const QString name = m_list->item(row)->text();
    if (QMessageBox::question(this, tr("Delete daemon"), tr("Delete daemon '%1'?").arg(name)) != QMessageBox::Yes)
        return;
    m_store.remove(name);
    refreshList();
    const auto names = m_store.names();
    selectProfile(names.isEmpty() ? QString() : names.first());
}

void ConnOptionsDialog::onRename()
{
    const int row = m_list->currentRow();
    if (row < 0)
        return;
    const QString oldName = m_list->item(row)->text();
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename daemon"), tr("New name"), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName)
        return;
    if (m_store.exists(newName.trimmed())) {
        QMessageBox::warning(this, tr("Rename daemon"), tr("A daemon named '%1' already exists.").arg(newName.trimmed()));
        return;
    }
    saveCurrent();
    m_store.rename(oldName, newName.trimmed());
    refreshList();
    selectProfile(newName.trimmed());
}

void ConnOptionsDialog::onTest()
{
    DaemonProfile p = current();
    if (p.host.isEmpty()) {
        QMessageBox::warning(this, tr("Test connection"), tr("Host is empty."));
        return;
    }
    setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const ConnTest r = runTest(p.toOptions());
    QApplication::restoreOverrideCursor();
    setEnabled(true);
    if (r.ok)
        QMessageBox::information(this, tr("Test connection"), r.message);
    else
        QMessageBox::warning(this, tr("Test connection"), r.message);
}