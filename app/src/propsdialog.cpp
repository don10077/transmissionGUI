#include "propsdialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QUrl>
#include <QMessageBox>
#include <functional>

PropsDialog::PropsDialog(const tgui::Torrent& t, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Properties — %1").arg(t.name.value_or(tr("(no name)"))));
    resize(600, 430);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);
    auto* tabs = new QTabWidget(this);
    auto* general = new QWidget(tabs);
    auto* form = new QFormLayout(general);
    form->setContentsMargins(16, 16, 16, 16);
    form->setVerticalSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* name = new QLineEdit(t.name.value_or(QString()), general);
    name->setReadOnly(true);
    form->addRow(tr("Name"), name);
    const auto initial = t.editableSettings;
    auto* peers = new QSpinBox(general);
    peers->setObjectName(QStringLiteral("peerLimit"));
    peers->setRange(1, 65535);
    peers->setMaximumWidth(160);
    peers->setValue(t.maxConnectedPeers.value_or(50));
    peers->setEnabled(t.maxConnectedPeers.has_value());
    form->addRow(tr("Peer limit"), peers);
    QList<std::function<void(QJsonObject&)>> readers;
    auto limit = [&](const QString& title, const char* enabled, const char* amount) {
        auto* row = new QWidget(general);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* check = new QCheckBox(tr("Limit"), row);
        auto* spin = new QSpinBox(row);
        spin->setRange(0, 1000000000);
        spin->setSuffix(tr(" KB/s"));
        spin->setValue(initial.value(amount).toInt());
        check->setChecked(initial.value(enabled).toBool());
        const bool supported = initial.contains(enabled) && initial.contains(amount);
        row->setEnabled(supported);
        spin->setEnabled(check->isChecked());
        connect(check, &QCheckBox::toggled, spin, &QWidget::setEnabled);
        layout->addWidget(check); layout->addWidget(spin, 1);
        form->addRow(title, row);
        readers.append([=](QJsonObject& result) {
            if (!supported) return;
            if (check->isChecked() != initial.value(enabled).toBool()) result[enabled] = check->isChecked();
            if (spin->value() != initial.value(amount).toInt()) result[amount] = spin->value();
        });
    };
    limit(tr("Maximum download speed"), "downloadLimited", "downloadLimit");
    limit(tr("Maximum upload speed"), "uploadLimited", "uploadLimit");
    auto mode = [&](const QString& title, const char* modeKey, const char* valueKey, bool ratio) {
        auto* row = new QWidget(general);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* choice = new QComboBox(row);
        choice->addItems({tr("Use daemon settings"), tr("Custom limit"), tr("Unlimited")});
        choice->setCurrentIndex(initial.value(modeKey).toInt());
        auto* number = new QDoubleSpinBox(row);
        number->setDecimals(ratio ? 2 : 0);
        number->setRange(0, 1000000);
        if (!ratio) number->setSuffix(tr(" minutes"));
        number->setValue(initial.value(valueKey).toDouble());
        number->setEnabled(choice->currentIndex() == 1);
        connect(choice, &QComboBox::currentIndexChanged, number, [number](int index) { number->setEnabled(index == 1); });
        const bool supported = initial.contains(modeKey) && initial.contains(valueKey);
        row->setEnabled(supported);
        layout->addWidget(choice, 1); layout->addWidget(number);
        form->addRow(title, row);
        readers.append([=](QJsonObject& result) {
            if (!supported) return;
            if (choice->currentIndex() != initial.value(modeKey).toInt()) result[modeKey] = choice->currentIndex();
            if (number->value() != initial.value(valueKey).toDouble()) result[valueKey] = number->value();
        });
    };
    mode(tr("Seed ratio"), "seedRatioMode", "seedRatioLimit", true);
    mode(tr("Stop seeding when inactive"), "seedIdleMode", "seedIdleLimit", false);
    tabs->addTab(general, tr("General"));
    auto* advanced = new QWidget(tabs);
    auto* advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(16, 16, 16, 16);
    advancedLayout->addWidget(new QLabel(tr("Trackers (one URL per line)"), advanced));
    auto* trackers = new QPlainTextEdit(advanced);
    QStringList original;
    for (const auto& tracker : t.trackerStats) original.append(tracker.announce);
    trackers->setPlainText(original.join('\n'));
    advancedLayout->addWidget(trackers);
    tabs->addTab(advanced, tr("Advanced"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(tabs, 1); root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, this] {
        QJsonObject changes;
        for (const auto& read : readers) read(changes);
        if (t.maxConnectedPeers && peers->value() != *t.maxConnectedPeers) changes["peer-limit"] = peers->value();
        QStringList urls;
        for (const auto& line : trackers->toPlainText().split('\n')) {
            const QString text = line.trimmed();
            if (text.isEmpty()) continue;
            const QUrl url(text, QUrl::StrictMode);
            if (!url.isValid() || url.host().isEmpty() ||
                (url.scheme() != "http" && url.scheme() != "https" && url.scheme() != "udp")) {
                QMessageBox::warning(this, tr("Trackers"), tr("Invalid tracker URL: %1").arg(text)); return;
            }
            if (!urls.contains(text)) urls.append(text);
        }
        QJsonArray add, remove;
        for (const auto& url : urls) if (!original.contains(url)) add.append(url);
        for (const auto& tracker : t.trackerStats)
            if (!urls.contains(tracker.announce) && tracker.id >= 0) remove.append(tracker.id);
        if (!add.isEmpty()) changes["trackerAdd"] = add;
        if (!remove.isEmpty()) changes["trackerRemove"] = remove;
        m_changes = changes;
        accept();
    });
}
