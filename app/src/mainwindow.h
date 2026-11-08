#pragma once

#include <QMainWindow>

class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    QLabel* m_placeholder = nullptr;
};
