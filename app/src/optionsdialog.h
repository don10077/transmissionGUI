#pragma once

#include <QDialog>
#include "daemonprofile.h"
#include "theme.h"

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
namespace tgui { class RpcClient; }

// W07: диалог «Параметры приложения» (design/mockup.html, DLG.opt).
// Вкладки: Общие / Интерфейс / Последовательная загрузка / Метки, затем
// дополнительные: Пути / Ограничения скорости / Добавление торрентов.
// Значения ui/* и downloads/* хранятся в QSettings; язык и масштаб требуют перезапуска.
class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr char skipAddDialogKey[] = "downloads/skipAddDialog";
    static constexpr char deleteSourceTorrentKey[] = "downloads/deleteSourceTorrent";
    static constexpr char clipboardLinksKey[] = "downloads/clipboardLinks";
    static constexpr char watchFolderKey[] = "downloads/watchFolder";
    static constexpr char watchDestinationKey[] = "downloads/watchDestination";

    explicit OptionsDialog(QWidget* parent = nullptr, int rpcVersion = 0,
                           tgui::RpcClient* rpc = nullptr, const QString& profile = {});

    // Вкладки диалога (для перехода из других окон).
    enum Page { GeneralPage, InterfacePage, SequentialPage, LabelsPage,
                PathsPage, SpeedPage, AddingPage };
    void setCurrentPage(int page);

    // Доступ к значениям (для UI и тестов).
    QString currentLanguage() const;
    void setLanguage(const QString& code);
    int scale() const;
    void setScale(int percent);
    int refreshIntervalSec() const;
    void setRefreshIntervalSec(int sec);
    // Маппинг remote→local путей (референс — вкладка Paths transgui): текст "remote=local" в строке.
    QString pathMappingsText() const;
    void setPathMappingsText(const QString& text);

    // Проверяет параметры, записывает их в QSettings и считает, нужен ли перезапуск.
    bool apply();
    // Язык/масштаб изменились → требуется перезапуск (применяется при следующем запуске).
    bool restartRequired() const { return m_restartRequired; }

private:
    void reject() override;
    void editProgressColors();
    void renameLabel(tgui::RpcClient& rpc);
    void addLabel();
    void removeLabel();
    void moveLabel(int delta);
    void restoreDefaultLabels();
    void updateLabelActions();

    ProfilePreferences m_preferences;
    tgui::RpcClient* m_rpc = nullptr;
    int m_rpcVersion = 0;
    QTabWidget* m_tabs = nullptr;
    QComboBox* m_language = nullptr;
    QSpinBox* m_scale = nullptr;
    QComboBox* m_toolbarStyle = nullptr;
    QSpinBox* m_refresh = nullptr;
    QSpinBox* m_backgroundRefresh = nullptr;
    QCheckBox* m_smoothRates = nullptr;
    QPlainTextEdit* m_paths = nullptr;
    QLineEdit* m_downloadSpeeds = nullptr;
    QLineEdit* m_uploadSpeeds = nullptr;
    QListWidget* m_labelList = nullptr;
    QCheckBox* m_closeToTray = nullptr;
    QCheckBox* m_minimizeToTray = nullptr;
    QCheckBox* m_trayIcon = nullptr;
    QCheckBox* m_trayNotify = nullptr;
    QCheckBox* m_checkUpdates = nullptr;
    QSpinBox* m_updateIntervalDays = nullptr;
    QCheckBox* m_torrentHandler = nullptr;
    QCheckBox* m_magnetHandler = nullptr;
    QCheckBox* m_autoSequential = nullptr;
    QCheckBox* m_skipAddDialog = nullptr;
    QCheckBox* m_deleteSourceTorrent = nullptr;
    QCheckBox* m_clipboardLinks = nullptr;
    QLineEdit* m_watchFolder = nullptr;
    QLineEdit* m_watchDestination = nullptr;
    QPlainTextEdit* m_sequentialExtensions = nullptr;
    tgui::theme::ProgressColors m_progressColors;
    bool m_restartRequired = false;
    bool m_renamingLabel = false;
};
