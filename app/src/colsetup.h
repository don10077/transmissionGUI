#pragma once

#include <QDialog>
#include <QList>
#include <QMap>
#include <QStringList>

class QTableView;
class QTreeView;
class QListWidget;
class QPushButton;

// W10: раскладка колонок списка (порядок + видимость + ширины) — персистентна (QSettings).
// Чистая логика отделена от виджетов (тестируется без GUI).
struct ColumnLayout {
    static int persistentColumn();         // ColName — всегда видима (нельзя скрыть)
    QList<int> order;                      // визуальный порядок (логические индексы колонок)
    QList<bool> visible;                   // видимость по логическому индексу
    QMap<int, int> widths;                 // ширина по логическому индексу

    static ColumnLayout defaults(int count); // порядок, видимость и ширины при первом запуске
    void ensureAtLeastOneVisible();          // хотя бы одна колонка видима
    void moveItem(int position, int delta);  // -1 вверх / +1 вниз
    static ColumnLayout fromSettings(int count,
                                     const QString& ns = QStringLiteral("columns"));
    void saveToSettings(const QString& ns = QStringLiteral("columns")) const;
    void applyToView(QTreeView* view) const;
    void applyToView(QTableView* view) const; // W11: пиры
    static ColumnLayout fromView(QTableView* view, int count); // W11: текущая раскладка пиров
};

// Диалог настройки колонок: порядок (Up/Down) + видимость (checkbox).
// Образец: transgui/colsetup.pas. Выход — новый порядок/видимость (ширины не меняются).
class ColSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit ColSetupDialog(const ColumnLayout& layout, const QStringList& columnTitles,
                            QWidget* parent = nullptr);
    ColumnLayout layout() const; // порядок + видимость (ширины — из входных)

private:
    void moveSelected(int delta);
    void updateButtons();
    ColumnLayout m_initial;
    QStringList m_titles;
    QListWidget* m_list = nullptr;
    QPushButton* m_up = nullptr;
    QPushButton* m_down = nullptr;
};
