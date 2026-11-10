#pragma once

#include <QDialog>

// Диалог «Переместить»: папка демона и перенос существующих данных.
class MoveDialog : public QDialog {
    Q_OBJECT
public:
    explicit MoveDialog(const QString& currentDir, QWidget* parent = nullptr);

    QString targetDir() const;
    bool moveData() const;

private:
    void onBrowse();

    class QLineEdit* m_path = nullptr;
    class QCheckBox* m_rename = nullptr;
};