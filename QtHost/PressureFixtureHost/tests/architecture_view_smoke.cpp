#include "ArchitectureView.h"

#include <QApplication>
#include <QDebug>
#include <QSignalSpy>
#include <QTest>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Q_UNUSED(app)

    ArchitectureView view;
    view.resize(3180, 1180);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view)) {
        qCritical() << "ArchitectureView test window was not exposed";
        return 3;
    }

    QSignalSpy valveSpy(&view, &ArchitectureView::valveClicked);
    QSignalSpy sensorSpy(&view, &ArchitectureView::sensorClicked);
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(230, 496));
    if (valveSpy.count() != 1 || valveSpy.takeFirst().at(0).toInt() != 1) {
        qCritical() << "Clicking valve 1 did not emit valveClicked(1)";
        return 1;
    }

    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(230, 845));
    if (sensorSpy.count() != 1 || sensorSpy.takeFirst().at(0).toInt() != 1) {
        qCritical() << "Clicking pressure sensor 1 did not emit sensorClicked(1)";
        return 4;
    }

    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(230, 610));
    if (!valveSpy.isEmpty() || !sensorSpy.isEmpty()) {
        qCritical() << "Clicking tank area unexpectedly emitted a device click";
        return 2;
    }

    return 0;
}
