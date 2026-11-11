#pragma once

#include <QStyledItemDelegate>

// Делегат строки списка торрентов: чередование фона, hover/selection,
// чекбокс в колонке Name, статус-точка, градиентный прогресс и цветные скорости.
class TorrentDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

// Колонка «Done»: синий градиентный бар + процент (референс — DrawProgressCell,
// main.pas): текст по центру клетки, белый если центром закрыт баром.
class ProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

// Колонка «Name»: чекбокс выделения строки (референс — чекбоксы TVarGrid).
// Клик по чекбоксу обрабатывается в MainWindow (clicked → toggle);
// текст сдвигается на ширину поля.
class NameCheckDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    static int checkSize();
};

// Колонка «Want» вкладки «Файлы»: чекбокс want/skip (референс — TFilesTree).
// editorEvent запрашивает изменение для файла или поддерева через wantToggled.
// paint() — явное рисование чекбокса с контрастом при выделении.
class WantCheckDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
};
