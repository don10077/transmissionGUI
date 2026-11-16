#pragma once

#include "theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QPair>
#include <QSettings>
#include <QStringList>

namespace torrentlabels {

inline constexpr char settingsKey[] = "downloads/labels";

/** Стандартные названия меток для нового каталога. */
inline const QStringList& presets()
{
    static const QStringList names = {"Video", "Serials", "Pictures", "Soft", "Music", "Books", "Games"};
    return names;
}

/** Возвращает список меток, заданных в настройках приложения. */
inline QStringList configured()
{
    return QSettings().value(settingsKey, presets()).toStringList();
}

/**
 * Возвращает значок стандартной или произвольной метки.
 * @param label Название метки.
 */
inline QIcon labelIcon(const QString& label)
{
    using tgui::theme::Icon;
    static const QPair<QString, Icon> categories[] = {
        {QStringLiteral("Video"), Icon::LabelVideo},
        {QStringLiteral("Serials"), Icon::LabelSerials},
        {QStringLiteral("Pictures"), Icon::LabelPictures},
        {QStringLiteral("Soft"), Icon::LabelSoft},
        {QStringLiteral("Music"), Icon::LabelMusic},
        {QStringLiteral("Books"), Icon::LabelBooks},
        {QStringLiteral("Games"), Icon::LabelGames},
    };
    for (const auto& [name, icon] : categories) {
        if (label.compare(name, Qt::CaseInsensitive) == 0)
            return tgui::theme::icon(icon);
    }
    return tgui::theme::icon(Icon::Label);
}

/** Создаёт выбор метки, сохраняя исходное значение при открытии свойств. */
inline QComboBox* createPicker(QWidget* parent, const QStringList& labels,
                              const QStringList& current = {})
{
    auto* picker = new QComboBox(parent);
    picker->setObjectName(QStringLiteral("torrentLabel"));
    picker->addItem(QCoreApplication::translate("TorrentLabels", "No label"), QStringList{});
    for (const auto& label : labels)
        picker->addItem(labelIcon(label), label, QStringList{label});
    if (current.size() == 1 && labels.contains(current.first())) {
        picker->setCurrentIndex(static_cast<int>(labels.indexOf(current.first())) + 1);
    } else if (!current.isEmpty()) {
        picker->addItem(current.join(QStringLiteral(", ")), current);
        picker->setCurrentIndex(picker->count() - 1);
    }
    return picker;
}

} // namespace torrentlabels
