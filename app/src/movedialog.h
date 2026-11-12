#pragma once

#include <QDialog>

// Диалог «Переместить»: папка демона и перенос существующих данных.
class MoveDialog : public QDialog {
    Q_OBJECT
public:
    explicit MoveDialog(const QString& currentDir, QWidget* parent = nullptr);

    QString targetDir() const;
    bool moveData() const;
    // Запомнить подтверждённую цель переноса в истории (LastMoveDir).
    static void rememberDirectory(const QString& dir);

private:
    void onBrowse();

    class QComboBox* m_path = nullptr;
    class QCheckBox* m_rename = nullptr;
};