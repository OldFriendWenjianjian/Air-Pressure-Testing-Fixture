#include "MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QIcon>
#include <QMessageBox>
#include <windows.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("气压检测工装上位机");
    QApplication::setOrganizationName("PressureFixture");
    QApplication::setApplicationVersion(QStringLiteral("usb-cdc-%1").arg(QString::fromLatin1(__DATE__ " " __TIME__)));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/pressure_fixture_comic_transparent.png")));

    HANDLE singleInstanceMutex = CreateMutexW(nullptr, FALSE, L"Global\\PressureFixtureHost.SingleInstance");
    if (singleInstanceMutex == nullptr) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("上位机启动失败"),
                             QStringLiteral("无法创建上位机实例锁，请重试。"));
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("上位机已在运行"),
                             QStringLiteral("检测到已有一个气压检测工装上位机实例在运行。\n请直接使用已打开的窗口，避免多个实例同时占用串口连接。"));
        CloseHandle(singleInstanceMutex);
        return 0;
    }

    MainWindow window;
    window.resize(1600, 960);
    window.show();

    const int rc = app.exec();
    CloseHandle(singleInstanceMutex);
    return rc;
}
