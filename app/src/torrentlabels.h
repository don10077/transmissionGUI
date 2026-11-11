#pragma once

#include <QComboBox>
#include <QCoreApplication>
#include <QSettings>
#include <QStringList>

namespace torrentlabels {

inline constexpr char settingsKey[] = "downloads/labels";

/** Возвращает список меток, заданных в настройках приложения. */
inline QStringList configured()
{
    return QSettings().value(settingsKey).toStringList();
}

/** Создаёт выбор метки, сохраняя исходное значение при открытии свойств. */
inline QComboBox* createPicker(QWidget* parent, const QStringList& labels,
                              const QStringList& current = {})
{
    auto* picker = new QComboBox(parent);
    picker->setObjectName(QStringLiteral("torrentLabel"));
    picker->addItem(QCoreApplication::translate("TorrentLabels", "No label"), QStringList{});
    for (const auto& label : labels)
        picker->addItem(label, QStringList{label});
    if (current.size() == 1 && labels.contains(current.first())) {
        picker->setCurrentIndex(static_cast<int>(labels.indexOf(current.first())) + 1);
    } else if (!current.isEmpty()) {
        picker->addItem(current.join(QStringLiteral(", ")), current);
        picker->setCurrentIndex(picker->count() - 1);
    }
    return picker;
}

} // namespace torrentlabels
