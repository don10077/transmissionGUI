#pragma once

#include <QWidget>

#include <QPair>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

// Левая панель (референс — lvFilter + edSearch, main.pas/main.lfm):
//   All torrents (N) / Downloading (N) / Completed (N) / Active (N) /
//   Inactive (N) / Stopped (N) / Error (N) / Waiting (N)
//   Folders:    <имя папки> (N) ...
//   Trackers:   <трекер> (N) ...
//   [поиск]
// Выбор строки фильтрует грид; группы папок/трекеров можно отключать.
class FilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilterPanel(QWidget* parent = nullptr);

    // Счётчики и состав групп (вызывается на каждом обновлении списка).
    void setCounts(int all, int downloading, int completed, int active,
                   int inactive, int stopped, int error, int waiting,
                   const QVector<QPair<QString, int>>& folders,
                   const QVector<QPair<QString, int>>& trackers);

    void setFolderGroupingEnabled(bool enabled);
    void setTrackerGroupingEnabled(bool enabled);
    void setEnabledUi(bool enabled);

    // Текущий выбор: группа статусов (TorrentModel::StatusGroup), папка/трекер —
    // пустая строка, если выбран статус.
    int selectedGroup() const;
    QString selectedFolder() const;
    QString selectedTracker() const;

    QLineEdit* searchEdit() const { return m_search; }

signals:
    void filterChanged(int group, const QString& folder, const QString& tracker);

private:
    void rebuild(int all, int downloading, int completed, int active,
                 int inactive, int stopped, int error, int waiting,
                 const QStringList& folders, const QStringList& trackers);
    QTreeWidgetItem* itemAt(int row) const;

    QTreeWidget* m_tree = nullptr;
    QLineEdit* m_search = nullptr;
    QString m_selFolder; // выбранная папка (для восстановления выбора)
    QString m_selTracker; // выбранный трекер
    int m_selStatus = -1; // TorrentModel::StatusGroup
};