#include "propsdialog.h"
#include "torrentlabels.h"
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

PropsDialog::PropsDialog(const tgui::Torrent& t, QWidget* parent, int rpcVersion, int page) : QDialog(parent)
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
    // Последовательная загрузка (W04): переключается и для уже добавленных
    // торрентов (torrent-set sequential_download). Гейт RPC ≥ 18 — как в образце
    // (acSequentialDownload) и в Add-диалоге.
    const bool sequentialSupported = rpcVersion >= 18;
    const bool sequentialInitial = t.sequentialDownload.value_or(false);
    auto* sequential = new QCheckBox(tr("Sequential download"), general);
    sequential->setObjectName(QStringLiteral("sequentialDownload"));
    sequential->setChecked(sequentialInitial);
    if (!sequentialSupported) {
        sequential->setEnabled(false);
        sequential->setToolTip(tr("Requires Transmission 4.1 or newer (RPC 18)"));
    }
    form->addRow(QString(), sequential);
    QList<std::function<void(QJsonObject&)>> readers;
    const auto labels = torrentlabels::configured();
    if (rpcVersion >= 16 && !labels.isEmpty()) {
        const auto current = t.labels.value_or(QStringList{});
        auto* picker = torrentlabels::createPicker(general, labels, current);
        form->addRow(tr("Label"), picker);
        readers.append([picker, current](QJsonObject& result) {
            const auto selected = picker->currentData().toStringList();
            if (selected != current)
                result["labels"] = QJsonArray::fromStringList(selected);
        });
    }
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
        const int shown = spin->value();
        readers.append([=](QJsonObject& result) {
            if (!supported) return;
            if (check->isChecked() != initial.value(enabled).toBool()) result[enabled] = check->isChecked();
            if (spin->value() != shown) result[amount] = spin->value();
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
        const double shown = number->value();
        readers.append([=](QJsonObject& result) {
            if (!supported) return;
            if (choice->currentIndex() != initial.value(modeKey).toInt()) result[modeKey] = choice->currentIndex();
            if (number->value() != shown) result[valueKey] = number->value();
        });
    };
    mode(tr("Seed ratio"), "seedRatioMode", "seedRatioLimit", true);
    mode(tr("Stop seeding when inactive"), "seedIdleMode", "seedIdleLimit", false);
    tabs->addTab(general, tr("General"));
    auto* advanced = new QWidget(tabs);
    auto* advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(16, 16, 16, 16);
    advancedLayout->addWidget(new QLabel(rpcVersion >= 17
        ? tr("Trackers (one URL per line, blank line between tiers)")
        : tr("Trackers (one URL per line)"), advanced));
    auto* trackers = new QPlainTextEdit(advanced);
    QStringList original;
    QStringList originalLines;
    for (const auto& tracker : t.trackerStats) original.append(tracker.announce);
    for (qsizetype i = 0; i < t.trackerStats.size(); ++i) {
        if (rpcVersion >= 17 && i > 0 && t.trackerStats.at(i - 1).tier != t.trackerStats.at(i).tier)
            originalLines.append(QString());
        originalLines.append(t.trackerStats.at(i).announce);
    }
    trackers->setPlainText(originalLines.join('\n'));
    advancedLayout->addWidget(trackers);
    tabs->addTab(advanced, tr("Advanced"));
    tabs->setCurrentIndex(page);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(tabs, 1); root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, this] {
        QJsonObject changes;
        for (const auto& read : readers) read(changes);
        if (t.maxConnectedPeers && peers->value() != *t.maxConnectedPeers) changes["peer-limit"] = peers->value();
        if (sequentialSupported && sequential->isChecked() != sequentialInitial)
            changes["sequential_download"] = sequential->isChecked();
        const auto entered = trackers->toPlainText().trimmed();
        if (entered == originalLines.join('\n').trimmed()) {
            m_changes = changes;
            accept();
            return;
        }
        QStringList urls, lines;
        for (const auto& line : entered.split('\n')) {
            const QString text = line.trimmed();
            if (text.isEmpty()) {
                if (!lines.isEmpty() && !lines.last().isEmpty())
                    lines.append(QString());
                continue;
            }
            const QUrl url(text, QUrl::StrictMode);
            if (!url.isValid() || url.host().isEmpty() ||
                (url.scheme() != "http" && url.scheme() != "https" && url.scheme() != "udp")) {
                QMessageBox::warning(this, tr("Trackers"), tr("Invalid tracker URL: %1").arg(text)); return;
            }
            if (!urls.contains(text)) {
                urls.append(text);
                lines.append(text);
            }
        }
        if (rpcVersion >= 17) {
            if (!lines.isEmpty() && lines.last().isEmpty())
                lines.removeLast();
            if (lines != originalLines)
                changes["trackerList"] = lines.join('\n');
            m_changes = changes;
            accept();
            return;
        }
        QJsonArray add, remove;
        for (const auto& url : urls) if (!original.contains(url)) add.append(url);
        for (const auto& tracker : t.trackerStats)
            if (!urls.contains(tracker.announce) && tracker.id >= 0) remove.append(tracker.id);
        if (add.size() == 1 && remove.size() == 1)
            changes["trackerReplace"] = QJsonArray{remove.first(), add.first()};
        else {
            if (!add.isEmpty()) changes["trackerAdd"] = add;
            if (!remove.isEmpty()) changes["trackerRemove"] = remove;
        }
        m_changes = changes;
        accept();
    });
}

/** Создаёт диалог, в котором каждое изменение для всей группы выбирается явно. */
PropsDialog::PropsDialog(int torrentCount, QWidget* parent, int rpcVersion) : QDialog(parent)
{
    setWindowTitle(tr("Properties — %1 torrents").arg(torrentCount));
    auto* form = new QFormLayout(this);
    QList<std::function<void(QJsonObject&)>> readers;
    const auto choiceRow = [&](const QString& title, const QStringList& choices) {
        auto* row = new QWidget(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* choice = new QComboBox(row);
        choice->addItem(tr("Keep unchanged"));
        choice->addItems(choices);
        layout->addWidget(choice, 1);
        form->addRow(title, row);
        return choice;
    };
    const auto addLimit = [&](const QString& title, const char* key, const char* amount,
                              const QStringList& modes, int customIndex, int decimals, double maximum) {
        auto* choice = choiceRow(title, modes);
        choice->setObjectName(QString::fromLatin1(key));
        auto* spin = new QDoubleSpinBox(choice->parentWidget());
        spin->setDecimals(decimals);
        spin->setRange(amount ? 0 : 1, maximum);
        spin->setEnabled(false);
        choice->parentWidget()->layout()->addWidget(spin);
        connect(choice, &QComboBox::currentIndexChanged, spin,
                [spin, customIndex](int index) { spin->setEnabled(index == customIndex); });
        const bool booleanMode = modes.size() == 2;
        readers.append([=](QJsonObject& changes) {
            const int index = choice->currentIndex();
            if (index == 0)
                return;
            if (!amount) {
                changes[key] = static_cast<int>(spin->value());
            } else {
                if (booleanMode)
                    changes[key] = index == customIndex;
                else
                    changes[key] = index - 1;
                if (index == customIndex)
                    changes[amount] = decimals == 0 ? QJsonValue(static_cast<int>(spin->value()))
                                                    : QJsonValue(spin->value());
            }
        });
    };
    addLimit(tr("Peer limit"), "peer-limit", nullptr, {tr("Set limit")}, 1, 0, 65535);
    const QStringList limits{tr("Unlimited"), tr("Custom limit")};
    addLimit(tr("Maximum download speed"), "downloadLimited", "downloadLimit", limits, 2, 0, 1000000000);
    addLimit(tr("Maximum upload speed"), "uploadLimited", "uploadLimit", limits, 2, 0, 1000000000);
    const QStringList modes{tr("Use daemon settings"), tr("Custom limit"), tr("Unlimited")};
    if (rpcVersion >= 5)
        addLimit(tr("Seed ratio"), "seedRatioMode", "seedRatioLimit", modes, 2, 2, 1000000);
    if (rpcVersion >= 10)
        addLimit(tr("Stop seeding when inactive"), "seedIdleMode", "seedIdleLimit", modes, 2, 0, 1000000);
    if (rpcVersion >= 18) {
        auto* sequential = choiceRow(tr("Sequential download"), {tr("Enable"), tr("Disable")});
        readers.append([sequential](QJsonObject& changes) {
            if (sequential->currentIndex() > 0)
                changes["sequential_download"] = sequential->currentIndex() == 1;
        });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, readers] {
        for (const auto& read : readers)
            read(m_changes);
        accept();
    });
}
