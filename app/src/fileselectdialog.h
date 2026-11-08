#pragma once

#include <QDialog>
#include <QVector>

#include "tgui/models.h"

class QTreeWidget;

// Выбор файлов торрента (want/skip) перед запуском.
class FileSelectDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileSelectDialog(const QVector<tgui::FileStat>& files, QWidget* parent = nullptr);

    QVector<int> wanted() const;
    QVector<int> unwanted() const;

private:
    void setAll(Qt::CheckState state);

    QTreeWidget* m_tree;
};
