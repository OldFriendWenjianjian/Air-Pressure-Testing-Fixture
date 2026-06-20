#include "MainWindow.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("气压检测工装上位机");
    QApplication::setOrganizationName("PressureFixture");
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/pressure_fixture_comic_transparent.png")));

    MainWindow window;
    window.resize(1600, 960);
    window.show();

    return app.exec();
}
