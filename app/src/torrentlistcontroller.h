#pragma once

#include <QCoreApplication>
#include <QTimer>

#include <functional>

#include "torrentmodel.h"

class QAction;
class ConnectionController;
class FilterPanel;
class QLineEdit;
class QMainWindow;
class QTreeView;
class StatusBarController;
class TorrentActions;
class QWidget;

/** Ведёт список торрентов, фильтрацию, выделение и команды открытия. */
class TorrentListController final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)

public:
    struct Selection {
        int currentId;
        qsizetype count;
    };

    /** Привязывает модель списка к профилю и каталогу меток. */
    TorrentListController(QMainWindow& window, const ConnectionController& connection,
                          const QStringList& labels);
    /** Создаёт список и панель фильтров. */
    QWidget* createPanel();
    /** Подключает команды и уведомления после построения основного окна. */
    void setServices(TorrentActions& actions, StatusBarController& status, QAction& chooseLabel,
                     QAction& refresh, std::function<int()> currentId,
                     std::function<void(int)> selectionChanged, std::function<void()> filterChanged, std::function<void()> labelsChanged,
                     std::function<void(const QString&)> downloadCompleted);
    /** Привязывает строку поиска на панели инструментов. */
    void bindSearch(QLineEdit& search);

    TorrentModel& model() { return m_model; }
    const TorrentModel& model() const { return m_model; }
    FilterPanel* filterPanel() const { return m_filterPanel; }
    QTreeView* view() const { return m_view; }
    bool hasUnlabelledTorrents() const { return m_hasUnlabelledTorrents; }

    /** Запоминает добавленный торрент для выбора при следующем обновлении. */
    void selectWhenAvailable(int id) { m_addSelectionId = id; }
    /** Применяет снимок списка и возвращает выбор после обновления. */
    Selection applySnapshot(const QVector<tgui::Torrent>& torrents, int prevId);
    /** Сбрасывает список при смене профиля. */
    void clear();
    /** Прекращает сохранение ширин после импорта настроек. */
    void suspendLayoutSave();

    void updateFilterPanel(const QVector<tgui::Torrent>& torrents);
    QVector<int> selectedTorrentIds() const;
    void onColumns();
    void onSelectAll();
    void onCopyPath();
    void onOpenFolder();
    QString mappedLocalPath(const QString& remotePath) const;
    void openLocalPath(const QString& path, bool selectFile);
    void onCopyMagnet();
    void saveColumnWidths();
    void onExportTorrentList();

private:
    void onFilterChanged(int group, const QString& folder, const QString& tracker,
                         const QString& label, bool withoutLabels);
    void onContextMenu(const QPoint& pos);
    void onTorrentsDoubleClicked();
    void openTorrentObject(int id, bool containingFolder);
    void onSelectionChanged();
    void exportTorrentLinks(const QVector<tgui::Torrent>& torrents);

    QMainWindow& m_window;
    const ConnectionController& m_connection;
    const QStringList& m_labels;
    TorrentModel m_model;
    TorrentFilterProxy m_proxy;
    FilterPanel* m_filterPanel = nullptr;
    QTreeView* m_view = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    TorrentActions* m_actions = nullptr;
    StatusBarController* m_status = nullptr;
    QAction* m_actChooseLabel = nullptr;
    QAction* m_actRefresh = nullptr;
    std::function<int()> m_currentId;
    std::function<void(int)> m_selectionChanged;
    std::function<void()> m_filterChanged;
    std::function<void()> m_labelsChanged;
    std::function<void(const QString&)> m_downloadCompleted;
    QTimer m_columnWidthTimer;
    QHash<int, int> m_pendingColumnWidths;
    int m_addSelectionId = -1;
    bool m_hasUnlabelledTorrents = false;
    bool m_refreshing = false;
    bool m_saveSuspended = false;
};
