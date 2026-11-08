#include "mainwindow.h"

#include <QLabel>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Transmission GUI"));
    resize(980, 700);

    m_placeholder = new QLabel(this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setText(tr(
        "transmissionGUI — каркас (этап 0).\n"
        "Следующий шаг: RPC-ядро и тесты (этап 1), см. WORKFLOW.md"));
    setCentralWidget(m_placeholder);

    statusBar()->showMessage(tr("Не подключено"));
}
