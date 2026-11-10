#pragma once

#include <QWidget>

#include <QPair>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;

// Левая панель (референс — lvFilter + edSearch, main.pas/main.lfm):
//   All torrents (N) / Downloading (N) / Completed (N) / Active (N) /
//   Inactive (N) / Stopped (N) / Error (N) / Waiting (N)
//   Folders:    <имя папки> (N) ...
//   Labels:     <метка> (N) ...
//   Trackers:   <трекер> (N) ...
// Выбор строки фильтрует грид; группы папок/меток/трекеров можно отключать.
class FilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilterPanel(QWidget* parent = nullptr);

    // Счётчики и состав групп (вызывается на каждом обновлении списка).
    void setCounts(int all, int downloading, int completed, int active,
                   int inactive, int stopped, int error, int waiting,
                   const QVector<QPair<QString, int>>& folders,
                   const QVector<QPair<QString, int>>& labels,
                   const QVector<QPair<QString, int>>& trackers);

    void setFolderGroupingEnabled(bool enabled);
    void setLabelGroupingEnabled(bool enabled);
    void setTrackerGroupingEnabled(bool enabled);
    void setEnabledUi(bool enabled);

    // Текущий выбор: группа статусов (TorrentModel::StatusGroup), папка/метка/
    // трекер — пустая строка, если выбран статус.
    int selectedGroup() const;
    QString selectedFolder() const;
    QString selectedLabel() const;
    QString selectedTracker() const;

    // Сбросить фильтр на «All torrents» (например, когда выбранная группа
    // скрыта в меню View).
    void selectAllTorrents();

signals:
    void filterChanged(int group, const QString& folder, const QString& tracker,
                       const QString& label);

private:
    QTreeWidgetItem* itemAt(int row) const;

    QTreeWidget* m_tree = nullptr;
    QTreeWidgetItem* m_foldersSection = nullptr;
    QTreeWidgetItem* m_labelsSection = nullptr;
    QTreeWidgetItem* m_trackersSection = nullptr;
    QString m_selFolder; // выбранная папка (для восстановления выбора)
    QString m_selLabel;  // выбранная метка
    QString m_selTracker; // выбранный трекер
    int m_selStatus = -1; // TorrentModel::StatusGroup
};