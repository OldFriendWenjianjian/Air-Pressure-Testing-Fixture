#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("气压检测工装上位机");
    QApplication::setOrganizationName("PressureFixture");

    MainWindow window;
    window.resize(1600, 960);
    window.show();

    return app.exec();
}
