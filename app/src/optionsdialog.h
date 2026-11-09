#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;
class QCheckBox;
class QPlainTextEdit;

// W07: диалог «Параметры» (образец transgui/options.pas) — интерфейс и трей.
// Значения читаются/пишутся в QSettings (раздел ui/*):
//   - язык (ui/language) и масштаб (ui/scale) требуют перезапуска;
//   - интервал обновления (ui/refreshInterval) и опции трея применяются сразу.
class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OptionsDialog(QWidget* parent = nullptr);

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

    // Записывает все значения в QSettings и считает, нужен ли перезапуск.
    void apply();
    // Язык/масштаб изменились → требуется перезапуск (применяется при следующем запуске).
    bool restartRequired() const { return m_restartRequired; }

private:
    QComboBox* m_language = nullptr;
    QSpinBox* m_scale = nullptr;
    QSpinBox* m_refresh = nullptr;
    QPlainTextEdit* m_paths = nullptr;
    QCheckBox* m_closeToTray = nullptr;
    QCheckBox* m_minimizeToTray = nullptr;
    QCheckBox* m_trayIcon = nullptr;
    QCheckBox* m_trayNotify = nullptr;
    bool m_restartRequired = false;
};