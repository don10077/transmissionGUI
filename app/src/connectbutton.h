#pragma once

#include <QToolButton>

/** Кнопка меню профилей с названием подключения и строкой состояния. */
class ConnectButton : public QToolButton {
public:
    /** Создаёт кнопку профилей внутри панели инструментов.
     * @param parent Родительский виджет.
     */
    explicit ConnectButton(QWidget* parent);

    /** Показывает профиль и состояние подключения.
     * @param title Название профиля или команда подключения.
     * @param subtitle Адрес сервера или состояние подключения.
     * @param connected Есть активное соединение.
     */
    void setConnection(const QString& title, const QString& subtitle, bool connected);

    /** Возвращает предпочтительный размер с учётом текста обеих строк. */
    QSize sizeHint() const override;
    /** Возвращает минимальный размер, при котором помещаются иконка и стрелка. */
    QSize minimumSizeHint() const override;

protected:
    /** Рисует обе строки и стрелку меню.
     * @param event Событие отрисовки.
     */
    void paintEvent(QPaintEvent* event) override;
    /** Открывает меню клавишами Enter, Space и Down.
     * @param event Нажатие клавиши.
     */
    void keyPressEvent(QKeyEvent* event) override;

private:
    QString m_subtitle;
    bool m_connected = false;
};
