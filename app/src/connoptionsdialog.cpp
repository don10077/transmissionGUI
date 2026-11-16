#include "connoptionsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include "theme.h"

#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "tgui/rpcclient.h"

namespace {
struct ConnTest {
    bool ok = false;
    bool needsPassphrase = false;
    QString message;
};

// Синхронная проверка соединения (session-get) с таймаутом.
ConnTest runTest(const tgui::RpcClient::Options& opts)
{
    ConnTest r;
    auto* client = new tgui::RpcClient(opts);
    QEventLoop loop;
    bool done = false;
    QObject::connect(client, &tgui::RpcClient::connected, &loop, [&](const tgui::SessionInfo& info) {
        if (done) return;
        done = true;
        r.ok = true;
        r.message = QObject::tr("Connected: Transmission %1 (RPC %2)").arg(info.version).arg(info.rpcVersion);
        loop.quit();
    });
    QObject::connect(client, &tgui::RpcClient::authenticationFailed, &loop, [&](const QString& e) {
        if (done) return;
        done = true;
        r.message = QObject::tr("Authentication failed: %1").arg(e);
        loop.quit();
    });
    const auto fail = [&](const QString& e) {
        if (done) return;
        done = true;
        r.message = e;
        loop.quit();
    };
    QObject::connect(client, &tgui::RpcClient::connectionFailed, &loop, fail);
    QObject::connect(client, &tgui::RpcClient::connectionInterrupted, &loop, fail);
    QObject::connect(client, &tgui::RpcClient::clientTlsPassphraseRequired, &loop, [&] {
        if (done) return;
        done = true;
        r.needsPassphrase = true;
        loop.quit();
    });
    QObject::connect(client, &tgui::RpcClient::readError, &loop, [&](const QString&, const QString& e) {
        if (done) return;
        done = true;
        r.message = e;
        loop.quit();
    });
    auto* timer = new QTimer(&loop);
    QObject::connect(timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer->start(8000);
    client->connectToDaemon();
    if (!done)
        loop.exec();
    QObject::disconnect(client, nullptr, nullptr, nullptr);
    client->deleteLater();
    if (!r.ok && !r.needsPassphrase && r.message.isEmpty())
        r.message = QObject::tr("No response (timeout).");
    return r;
}

QLabel* noteLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("hintLabel"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}
}  // namespace

ConnOptionsDialog::ConnOptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    // design/mockup.html, DLG.conn: профиль выбирается сверху, параметры — во вкладках.
    setWindowTitle(tr("Manage connections to Transmission"));
    resize(660, 480);

    m_profiles = new QComboBox(this);
    m_profiles->setObjectName(QStringLiteral("profileCombo"));
    m_profiles->setMinimumWidth(160);
    auto* btnNew = new QPushButton(tr("New…"), this);
    auto* btnRename = new QPushButton(tr("Rename…"), this);
    auto* btnDelete = new QPushButton(tr("Delete"), this);
    // Фаза 6.1: разрушительное действие выделяется красной рамкой.
    btnDelete->setProperty("danger", true);

    auto* profileBox = new QWidget(this);
    auto* profileForm = tgui::theme::formGrid(profileBox);
    profileForm->setContentsMargins(0, 0, 0, 0);
    auto* profileRow = new QHBoxLayout;
    profileRow->setSpacing(8);
    profileRow->addWidget(m_profiles, 1);
    profileRow->addWidget(btnNew);
    profileRow->addWidget(btnRename);
    profileRow->addWidget(btnDelete);
    tgui::theme::addFormRow(profileForm, tr("Connection:"), profileRow);

    auto* tabs = new QTabWidget(this);

    // --- Transmission ----------------------------------------------------
    auto* connection = new QWidget(tabs);
    auto* form = tgui::theme::formGrid(connection);
    form->setContentsMargins(14, 12, 14, 12);
    tgui::theme::addFullRow(form, noteLabel(
        tr("Specify how to connect to the remote host running the Transmission daemon."),
        connection));
    m_host = new QLineEdit(connection);
    tgui::theme::addFormRow(form, tr("Remote host:"), m_host);
    m_port = new QSpinBox(connection);
    m_port->setRange(1, 65535);
    tgui::theme::addFormRow(form, tr("Port:"), m_port);
    m_protocol = new QComboBox(connection);
    m_protocol->addItem(tr("HTTP"), QStringLiteral("http"));
    m_protocol->addItem(tr("HTTPS (TLS)"), QStringLiteral("https"));
    tgui::theme::addFormRow(form, tr("Protocol:"), m_protocol);
    m_rpcPath = new QLineEdit(connection);
    tgui::theme::addFormRow(form, tr("RPC path:"), m_rpcPath);

    m_requireAuth = new QCheckBox(tr("Authentication required"), connection);
    m_requireAuth->setObjectName(QStringLiteral("requireAuth"));
    tgui::theme::addFormRow(form, QString(), m_requireAuth);
    m_user = new QLineEdit(connection);
    tgui::theme::addFormRow(form, tr("Username:"), m_user);
    m_pass = new QLineEdit(connection);
    m_pass->setEchoMode(QLineEdit::Password);
    tgui::theme::addFormRow(form, tr("Password:"), m_pass);
    m_ignoreSsl = new QCheckBox(tr("Ignore SSL certificate errors"), connection);
    tgui::theme::addFormRow(form, QString(), m_ignoreSsl);
    m_alwaysReconnect = new QCheckBox(tr("Always reconnect automatically"), connection);
    m_alwaysReconnect->setObjectName(QStringLiteral("alwaysReconnect"));
    tgui::theme::addFormRow(form, QString(), m_alwaysReconnect);

    m_testStatus = new QLabel(connection);
    m_testStatus->setTextFormat(Qt::PlainText);
    auto* btnTest = new QPushButton(tr("Test connection"), connection);
    auto* testRow = new QHBoxLayout;
    testRow->setSpacing(8);
    testRow->addWidget(btnTest);
    testRow->addWidget(m_testStatus, 1);
    tgui::theme::addFormRow(form, QString(), testRow);

    // В макете клиентский сертификат — раскрывающийся блок, а не вкладка.
    m_certToggle = new QToolButton(connection);
    m_certToggle->setObjectName(QStringLiteral("clientCertificateToggle"));
    m_certToggle->setText(tr("Client certificate"));
    m_certToggle->setCheckable(true);
    m_certToggle->setArrowType(Qt::RightArrow);
    m_certToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_certToggle->setAutoRaise(true);
    tgui::theme::addFullRow(form, m_certToggle);

    m_certPanel = new QWidget(connection);
    auto* tlsForm = tgui::theme::formGrid(m_certPanel);
    tlsForm->setContentsMargins(0, 0, 0, 0);
    tgui::theme::addFullRow(tlsForm, noteLabel(
        tr("Choose a certificate and PEM private key, or choose a PKCS#12 file as the certificate and leave the key field empty."),
        m_certPanel));
    const auto fileRow = [this, tlsForm](const QString& label, const QString& filter,
                                       const QString& objectName) {
        auto* edit = new QLineEdit(m_certPanel);
        edit->setObjectName(objectName);
        auto* browse = new QPushButton(tr("Browse…"), m_certPanel);
        connect(browse, &QPushButton::clicked, this, [this, edit, label, filter] {
            const auto path = QFileDialog::getOpenFileName(this, label, edit->text(), filter);
            if (!path.isEmpty())
                edit->setText(path);
        });
        auto* row = new QHBoxLayout;
        row->addWidget(edit, 1);
        row->addWidget(browse);
        tgui::theme::addFormRow(tlsForm, label, row);
        return edit;
    };
    m_clientCertificatePath = fileRow(tr("Client certificate:"), tr("Certificates (*.pem *.crt *.cer *.p12 *.pfx);;All files (*)"),
                                     QStringLiteral("clientCertificatePath"));
    m_clientKeyPath = fileRow(tr("Client private key:"), tr("PEM private keys (*.pem *.key);;All files (*)"),
                             QStringLiteral("clientKeyPath"));
    m_certPanel->setVisible(false);
    tgui::theme::addFullRow(form, m_certPanel);
    form->setRowStretch(form->rowCount(), 1);
    tabs->addTab(connection, tr("Transmission"));

    // --- Proxy -----------------------------------------------------------
    auto* proxyPage = new QWidget(tabs);
    auto* pform = tgui::theme::formGrid(proxyPage);
    pform->setContentsMargins(14, 12, 14, 12);
    m_useProxy = new QCheckBox(tr("Connect through a proxy server"), proxyPage);
    tgui::theme::addFullRow(pform, m_useProxy);
    m_proxyHost = new QLineEdit(proxyPage);
    tgui::theme::addFormRow(pform, tr("Proxy server:"), m_proxyHost);
    m_proxyPort = new QSpinBox(proxyPage);
    m_proxyPort->setRange(1, 65535);
    tgui::theme::addFormRow(pform, tr("Port:"), m_proxyPort);
    m_useSocks = new QCheckBox(tr("SOCKS5"), proxyPage);
    tgui::theme::addFormRow(pform, QString(), m_useSocks);
    m_proxyAuth = new QCheckBox(tr("Authentication required"), proxyPage);
    m_proxyAuth->setObjectName(QStringLiteral("proxyAuth"));
    tgui::theme::addFormRow(pform, QString(), m_proxyAuth);
    m_proxyUser = new QLineEdit(proxyPage);
    tgui::theme::addFormRow(pform, tr("Username:"), m_proxyUser);
    m_proxyPass = new QLineEdit(proxyPage);
    m_proxyPass->setEchoMode(QLineEdit::Password);
    tgui::theme::addFormRow(pform, tr("Password:"), m_proxyPass);
    pform->setRowStretch(pform->rowCount(), 1);
    tabs->addTab(proxyPage, tr("Proxy"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(8);
    root->addWidget(profileBox);
    root->addWidget(tabs, 1);
    root->addWidget(buttons);

    connect(m_profiles, &QComboBox::currentIndexChanged, this, &ConnOptionsDialog::onProfileSelected);
    connect(btnNew, &QPushButton::clicked, this, &ConnOptionsDialog::onNew);
    connect(btnDelete, &QPushButton::clicked, this, &ConnOptionsDialog::onDelete);
    connect(btnRename, &QPushButton::clicked, this, &ConnOptionsDialog::onRename);
    connect(btnTest, &QPushButton::clicked, this, &ConnOptionsDialog::onTest);
    connect(m_requireAuth, &QCheckBox::toggled, this, [this](bool on) {
        m_user->setEnabled(on);
        m_pass->setEnabled(on);
    });
    connect(m_useProxy, &QCheckBox::toggled, this, [this](bool on) {
        m_useSocks->setEnabled(on);
        m_proxyHost->setEnabled(on);
        m_proxyPort->setEnabled(on);
        m_proxyAuth->setEnabled(on);
        const bool auth = on && m_proxyAuth->isChecked();
        m_proxyUser->setEnabled(auth);
        m_proxyPass->setEnabled(auth);
    });
    connect(m_proxyAuth, &QCheckBox::toggled, this, [this](bool on) {
        m_proxyUser->setEnabled(on && m_useProxy->isChecked());
        m_proxyPass->setEnabled(on && m_useProxy->isChecked());
    });
    connect(m_certToggle, &QToolButton::toggled, this, [this](bool on) {
        m_certToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        m_certPanel->setVisible(on);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &ConnOptionsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_protocol, &QComboBox::currentIndexChanged, this, [this] {
        m_ignoreSsl->setEnabled(m_protocol->currentData().toString() == "https");
    });

    const QString cur = m_store.currentName();
    const auto names = m_store.names();
    for (const auto& name : names)
        m_drafts.append({name, m_store.profile(name)});
    refreshList();
    selectProfile(names.contains(cur) ? cur : names.value(0));
    tgui::theme::decorateDialog(this);
}

void ConnOptionsDialog::refreshList()
{
    const QSignalBlocker blocker(m_profiles);
    m_profiles->clear();
    for (const auto& entry : m_drafts)
        m_profiles->addItem(entry.second.name);
}

void ConnOptionsDialog::selectProfile(const QString& name)
{
    const QSignalBlocker blocker(m_profiles);
    m_profiles->setCurrentIndex(m_profiles->findText(name, Qt::MatchExactly | Qt::MatchCaseSensitive));
    onProfileSelected();
}

void ConnOptionsDialog::onProfileSelected()
{
    saveDraft();
    const int index = m_profiles->currentIndex();
    setProfile(index >= 0 ? m_drafts.at(index).second : DaemonProfile{});
}

void ConnOptionsDialog::setProfile(const DaemonProfile& p)
{
    // Выбор профиля в ComboBox без рекурсивного onProfileSelected.
    const QSignalBlocker blocker(m_profiles);
    const int index = m_profiles->findText(p.name, Qt::MatchExactly | Qt::MatchCaseSensitive);
    if (index >= 0)
        m_profiles->setCurrentIndex(index);
    m_editingName = p.name;

    m_host->setText(p.host);
    m_port->setValue(p.port > 0 ? p.port : 9091);
    const int pi = m_protocol->findData(p.useSsl ? QStringLiteral("https") : QStringLiteral("http"));
    if (pi >= 0)
        m_protocol->setCurrentIndex(pi);
    m_rpcPath->setText(p.rpcPath);
    m_requireAuth->setChecked(!p.username.isEmpty() || !p.password.isEmpty());
    m_user->setText(p.username);
    m_pass->setText(p.password);
    m_user->setEnabled(m_requireAuth->isChecked());
    m_pass->setEnabled(m_requireAuth->isChecked());
    m_ignoreSsl->setChecked(p.ignoreSslErrors);
    m_alwaysReconnect->setChecked(p.alwaysReconnect);
    m_clientCertificatePath->setText(p.clientCertificatePath);
    m_clientKeyPath->setText(p.clientKeyPath);
    m_certToggle->setChecked(!p.clientCertificatePath.isEmpty() || !p.clientKeyPath.isEmpty());
    m_useProxy->setChecked(p.useProxy);
    m_useSocks->setChecked(p.useSocks);
    m_proxyHost->setText(p.proxyHost);
    m_proxyPort->setValue(p.proxyPort > 0 ? p.proxyPort : 8080);
    m_proxyAuth->setChecked(!p.proxyUser.isEmpty() || !p.proxyPass.isEmpty());
    m_proxyUser->setText(p.proxyUser);
    m_proxyPass->setText(p.proxyPass);
    for (QWidget* widget : QList<QWidget*>{m_useSocks, m_proxyHost, m_proxyPort, m_proxyAuth})
        widget->setEnabled(p.useProxy);
    m_proxyUser->setEnabled(p.useProxy && m_proxyAuth->isChecked());
    m_proxyPass->setEnabled(p.useProxy && m_proxyAuth->isChecked());
    m_ignoreSsl->setEnabled(p.useSsl);
    m_testStatus->clear();
}

DaemonProfile ConnOptionsDialog::current() const
{
    DaemonProfile p;
    p.name = m_editingName;
    p.host = m_host->text().trimmed();
    p.port = m_port->value();
    p.useSsl = (m_protocol->currentData().toString() == "https");
    p.rpcPath = m_rpcPath->text().trimmed();
    p.username = m_requireAuth->isChecked() ? m_user->text() : QString();
    p.password = m_requireAuth->isChecked() ? m_pass->text() : QString();
    p.alwaysReconnect = m_alwaysReconnect->isChecked();
    p.ignoreSslErrors = m_ignoreSsl->isChecked();
    p.clientCertificatePath = m_clientCertificatePath->text().trimmed();
    p.clientKeyPath = m_clientKeyPath->text().trimmed();
    p.useProxy = m_useProxy->isChecked();
    p.useSocks = m_useProxy->isChecked() && m_useSocks->isChecked();
    p.proxyHost = m_useProxy->isChecked() ? m_proxyHost->text().trimmed() : QString();
    p.proxyPort = m_proxyPort->value();
    p.proxyUser = m_useProxy->isChecked() && m_proxyAuth->isChecked() ? m_proxyUser->text() : QString();
    p.proxyPass = m_useProxy->isChecked() && m_proxyAuth->isChecked() ? m_proxyPass->text() : QString();
    return p;
}

/** Сохраняет заполненную форму в черновике до переключения профиля. */
void ConnOptionsDialog::saveDraft()
{
    for (auto& entry : m_drafts) {
        if (entry.second.name == m_editingName) {
            entry.second = current();
            return;
        }
    }
}

/** Записывает черновики и закрывает диалог после успешного сохранения. */
void ConnOptionsDialog::accept()
{
    saveDraft();
    if (!m_store.saveDrafts(m_drafts, m_editingName)) {
        QMessageBox::warning(this, windowTitle(), tr("Could not save the connection profiles."));
        return;
    }
    QDialog::accept();
}

/** Проверяет доступность нового @p name и показывает ошибку под заголовком @p title. */
bool ConnOptionsDialog::canUseName(const QString& name, const QString& title)
{
    if (!ProfileStore::isValidName(name)) {
        QMessageBox::warning(this, title, tr("Use a name of 1 to 64 characters without slashes or reserved names."));
        return false;
    }
    for (const auto& entry : m_drafts) {
        if (entry.second.name.compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, title, tr("A connection named '%1' already exists.").arg(name));
            return false;
        }
    }
    return true;
}

void ConnOptionsDialog::onNew()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New connection"), tr("Name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    const QString n = name.trimmed();
    if (!canUseName(n, tr("New connection")))
        return;
    DaemonProfile draft;
    draft.name = n;
    m_drafts.append({{}, std::move(draft)});
    refreshList();
    selectProfile(n);
}

void ConnOptionsDialog::onDelete()
{
    const QString name = m_profiles->currentText();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete connection"), tr("Delete connection '%1'?").arg(name)) != QMessageBox::Yes)
        return;
    m_drafts.removeAt(m_profiles->currentIndex());
    m_editingName.clear();
    refreshList();
    selectProfile(m_drafts.isEmpty() ? QString() : m_drafts.first().second.name);
}

void ConnOptionsDialog::onRename()
{
    const QString oldName = m_profiles->currentText();
    if (oldName.isEmpty())
        return;
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename connection"), tr("New name:"), QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.trimmed() == oldName)
        return;
    if (!canUseName(newName.trimmed(), tr("Rename connection")))
        return;
    saveDraft();
    m_drafts[m_profiles->currentIndex()].second.name = newName.trimmed();
    m_editingName.clear();
    refreshList();
    selectProfile(newName.trimmed());
}

void ConnOptionsDialog::onTest()
{
    DaemonProfile p = current();
    if (p.host.isEmpty()) {
        m_testStatus->setPalette(QPalette(tgui::theme::color::red));
        m_testStatus->setText(tr("Host is empty."));
        return;
    }
    auto options = p.toOptions();
    const auto test = [this, &options] {
        setEnabled(false);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto result = runTest(options);
        QApplication::restoreOverrideCursor();
        setEnabled(true);
        return result;
    };
    m_testStatus->setText(tr("Connecting…"));
    auto r = test();
    if (r.needsPassphrase) {
        bool accepted = false;
        const auto passphrase = QInputDialog::getText(this, tr("Client TLS passphrase"),
            tr("Passphrase for the client certificate or key:"), QLineEdit::Password, {}, &accepted);
        if (!accepted) {
            m_testStatus->clear();
            return;
        }
        options.clientKeyPassphrase = passphrase.toUtf8();
        r = test();
    }
    QPalette palette = m_testStatus->palette();
    palette.setColor(QPalette::WindowText,
                     r.ok ? tgui::theme::color::green : tgui::theme::color::red);
    m_testStatus->setPalette(palette);
    m_testStatus->setText(r.message);
}
