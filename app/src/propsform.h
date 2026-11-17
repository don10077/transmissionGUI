#pragma once

#include <QPointer>
#include <QHash>
#include <QUrl>
#include <QWidget>

#include "tgui/models.h"

#include <optional>

class QVBoxLayout;
class QScrollArea;
class QLabel;
class QAction;
class QMenu;
class QGridLayout;
class QLineEdit;
class QPushButton;
class PieceMapBar;

// Панель «General» (содержимое диалога «Свойства»): раскладка по референсу
// (tabGeneral: общий прогресс + секции «Передача»/«Торрент»), только чтение.
// Повторные обновления выбранного торрента сохраняют виджеты и выделение текста.
class PropsForm : public QWidget {
    Q_OBJECT
public:
    explicit PropsForm(QWidget* parent = nullptr);

    void setTorrent(const tgui::Torrent& t);
    // Лимиты скорости для секции «Передача» (session-get).
    void setSession(const tgui::SessionInfo& info);
    /** Подключает существующую команду изменения меток для ссылки в форме. */
    void setEditLabelsAction(QAction* action);
    void clear();
    const tgui::Torrent* torrent() const
    {
        return m_torrent.has_value() ? &m_torrent.value() : nullptr;
    }

private:
    void rebuild();
    /** Создаёт меню с копией значения, не зависящее от времени жизни поля. */
    QMenu* createCopyMenu(const QString& value, const QUrl& link = {});
    /** Подключает меню полного значения к заголовку или значению поля. */
    void enableFieldMenu(QWidget* widget, const QString& value, const QUrl& link = {});

    QVBoxLayout* m_layout = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QWidget* m_progressRow = nullptr;
    PieceMapBar* m_pieceBar = nullptr;
    QLabel* m_progressPercent = nullptr;
    QLabel* m_magnetLabel = nullptr;
    QLineEdit* m_magnet = nullptr;
    QPushButton* m_labelEdit = nullptr;
    QHash<QString, QPair<QLabel*, QLabel*>> m_values;
    QList<QGridLayout*> m_transferGrids;
    QList<QGridLayout*> m_torrentGrids;
    QStringList m_renderedLabels;
    QPointer<QAction> m_editLabels;
    std::optional<tgui::Torrent> m_torrent;
    std::optional<tgui::SessionInfo> m_session;
    // Сохранение прокрутки между пересборками (автообновление раз в 2 с).
    int m_builtTorrentId = -1;
    bool m_builtWithLabels = false;
    int m_pendingScroll = -1;
    int m_rebuildSerial = 0;
};
