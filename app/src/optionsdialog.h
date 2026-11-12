#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
namespace tgui { class RpcClient; }

// W07: диалог «Параметры» (образец transgui/options.pas) — интерфейс и трей.
// Значения читаются/пишутся в QSettings (раздел ui/*):
//   - язык (ui/language) и масштаб (ui/scale) требуют перезапуска;
//   - интервал обновления (ui/refreshInterval) и опции трея применяются сразу.
class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr char skipAddDialogKey[] = "downloads/skipAddDialog";
    static constexpr char deleteSourceTorrentKey[] = "downloads/deleteSourceTorrent";
    static constexpr char clipboardLinksKey[] = "downloads/clipboardLinks";
    static constexpr char watchFolderKey[] = "downloads/watchFolder";
    static constexpr char watchDestinationKey[] = "downloads/watchDestination";

    explicit OptionsDialog(QWidget* parent = nullptr, int rpcVersion = 0,
                           tgui::RpcClient* rpc = nullptr);

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
    void renameLabel(tgui::RpcClient& rpc);
    QComboBox* m_language = nullptr;
    QSpinBox* m_scale = nullptr;
    QSpinBox* m_refresh = nullptr;
    QPlainTextEdit* m_paths = nullptr;
    QPlainTextEdit* m_labels = nullptr;
    QCheckBox* m_closeToTray = nullptr;
    QCheckBox* m_minimizeToTray = nullptr;
    QCheckBox* m_trayIcon = nullptr;
    QCheckBox* m_trayNotify = nullptr;
    QCheckBox* m_autoSequential = nullptr;
    QCheckBox* m_skipAddDialog = nullptr;
    QCheckBox* m_deleteSourceTorrent = nullptr;
    QCheckBox* m_clipboardLinks = nullptr;
    QLineEdit* m_watchFolder = nullptr;
    QLineEdit* m_watchDestination = nullptr;
    QPlainTextEdit* m_sequentialExtensions = nullptr;
    bool m_restartRequired = false;
    bool m_renamingLabel = false;
};
