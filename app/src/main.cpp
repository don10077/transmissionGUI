#include "mainwindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("transmissionGUI");
    app.setApplicationName("transmissionGUI");
    app.setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();
    return app.exec();
}
