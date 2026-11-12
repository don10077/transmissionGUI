#pragma once

#include <QFont>
#include <QHash>
#include <QStringList>
#include <QStyledItemDelegate>

/** Рисует строки списка торрентов, прогресс, статусы и метки по макету. */
class TorrentDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

private:
    void updateFonts(const QFont& font) const;
    void paintProgress(QPainter& painter, const QRect& cell, const QModelIndex& index) const;
    void paintLabels(QPainter& painter, const QRect& cell, const QStringList& labels) const;

    mutable bool m_fontsReady = false;
    mutable QFont m_baseFont;
    mutable QFont m_progressFont;
    mutable QFont m_labelFont;
    mutable int m_progressHeight = 15;
    mutable QHash<QString, int> m_labelWidths;
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
