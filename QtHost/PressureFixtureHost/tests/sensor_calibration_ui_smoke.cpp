#include "MainWindow.h"
#include "ArchitectureView.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>

#include <cstdio>
#include <initializer_list>

using namespace fixture;

class SensorCalibrationUiSmokeAccessor {
public:
    static void showCalibration(MainWindow &window)
    {
        window.m_sensorCalibrationTestConnected = true;
        window.setSensorCalibrationCapability(true);
        window.selectDebugMode();
        window.selectSensorCalibrationTool();
    }

    static void setStatus(MainWindow &window, const SensorCalibrationStatus &status)
    {
        window.m_sensorCalibrationStatus = status;
        window.m_snapshot.pressureCalibrationStatusAvailable = true;
        window.m_snapshot.pressureCalibrationValidMask = status.calibratedMask;
        window.m_snapshot.pressureCalibrationModeActive = status.active;
        window.m_snapshot.pressureCalibrationActuator = status.actuator;
        window.m_snapshot.pressureCalibrationCapturedMask = status.capturedMask;
        window.m_snapshot.pressureCalibrationStorageFault = status.storageFault;
        window.m_architectureView->setSnapshot(window.m_snapshot);
        window.updateTables();
        window.updateSensorCalibrationPanel();
    }

    static void setUnsupported(MainWindow &window)
    {
        window.m_sensorCalibrationSupported = false;
        window.updateSensorCalibrationPanel();
    }

    static QGroupBox *panel(MainWindow &window) { return window.m_debugSensorCalibrationBox; }
    static QComboBox *mode(MainWindow &window) { return window.m_sensorCalibrationModeCombo; }
    static QComboBox *slot(MainWindow &window) { return window.m_sensorCalibrationSlotCombo; }
    static QLabel *live(MainWindow &window) { return window.m_sensorCalibrationLiveLabel; }
    static QLabel *state(MainWindow &window) { return window.m_sensorCalibrationStateLabel; }
    static QLabel *slotState(MainWindow &window) { return window.m_sensorCalibrationSlotLabel; }
    static QPushButton *enter(MainWindow &window) { return window.m_sensorCalibrationEnterButton; }
    static QPushButton *exit(MainWindow &window) { return window.m_sensorCalibrationExitButton; }
    static QPushButton *fill(MainWindow &window) { return window.m_sensorCalibrationFillButton; }
    static QPushButton *release(MainWindow &window) { return window.m_sensorCalibrationReleaseButton; }
    static QPushButton *vent(MainWindow &window) { return window.m_sensorCalibrationVentButton; }
    static QPushButton *cancelVent(MainWindow &window) { return window.m_sensorCalibrationCancelVentButton; }
    static QPushButton *save(MainWindow &window) { return window.m_sensorCalibrationSaveButton; }
    static QTableWidget *points(MainWindow &window) { return window.m_sensorCalibrationPointTable; }
    static QTableWidget *pressure(MainWindow &window) { return window.m_pressureTable; }
    static QDoubleSpinBox *actual(MainWindow &window, int point)
    {
        return window.m_sensorCalibrationActualSpins[static_cast<size_t>(point)];
    }
    static QPushButton *record(MainWindow &window, int point)
    {
        return window.m_sensorCalibrationRecordButtons[static_cast<size_t>(point)];
    }
    static void setPendingRecord(MainWindow &window, int point, uint32_t actual001mmHg)
    {
        window.m_sensorCalibrationPendingRecordPoint = point;
        window.m_sensorCalibrationPendingRecordActual001mmHg = actual001mmHg;
        window.m_sensorCalibrationPendingRecordAcknowledged = false;
        window.updateSensorCalibrationPanel();
    }
    static void clearPendingRecord(MainWindow &window)
    {
        window.m_sensorCalibrationPendingRecordPoint = -1;
        window.m_sensorCalibrationPendingRecordActual001mmHg = 0;
        window.m_sensorCalibrationPendingRecordAcknowledged = false;
        window.updateSensorCalibrationPanel();
    }
    static void disconnectHardware(MainWindow &window)
    {
        window.m_sensorCalibrationPollTimer.stop();
        window.m_handshakeTimer.stop();
        window.m_transport.close();
    }
    static void simulateActiveJog(MainWindow &window)
    {
        window.m_sensorCalibrationJogActuator = usb::SensorCalibrationFill;
        window.m_sensorCalibrationJogTimer.start();
    }
    static bool jogStopped(const MainWindow &window)
    {
        return window.m_sensorCalibrationJogActuator == usb::SensorCalibrationStop &&
               !window.m_sensorCalibrationJogTimer.isActive();
    }
    static int jogRefreshInterval(const MainWindow &window)
    {
        return window.m_sensorCalibrationJogTimer.interval();
    }
};

namespace {

bool containsAll(const QString &text, std::initializer_list<QString> values)
{
    for (const QString &value : values) {
        if (!text.contains(value)) {
            return false;
        }
    }
    return true;
}

SensorCalibrationStatus readyStatus(bool inPlace, int slot)
{
    SensorCalibrationStatus status;
    status.version = 1;
    status.active = true;
    status.sourceValid = true;
    status.storageLoaded = true;
    status.stagedComplete = true;
    status.zeroReady = true;
    status.inPlaceMode = inPlace;
    status.selectedSlot = static_cast<uint8_t>(slot);
    status.capturedMask = 0x0F;
    status.calibratedMask = static_cast<uint16_t>((1u << 0) | (1u << 7));
    status.liveRaw = 1234567;
    status.liveNominal001mmHg = 149875;
    for (int point = 0; point < 4; ++point) {
        status.rawPoints[static_cast<size_t>(point)] = 1000000u + static_cast<uint32_t>(point) * 500000u;
        static const uint32_t actuals[4] = {0u, 50000u, 150000u, 250000u};
        status.actual001mmHg[static_cast<size_t>(point)] = actuals[point];
    }
    return status;
}

} // namespace

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("PRESSURE_FIXTURE_UI_SCREENSHOT")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    MainWindow window;
    SensorCalibrationUiSmokeAccessor::showCalibration(window);

    auto *panel = SensorCalibrationUiSmokeAccessor::panel(window);
    auto *mode = SensorCalibrationUiSmokeAccessor::mode(window);
    auto *slot = SensorCalibrationUiSmokeAccessor::slot(window);
    auto *live = SensorCalibrationUiSmokeAccessor::live(window);
    auto *points = SensorCalibrationUiSmokeAccessor::points(window);
    if (!panel || !mode || !slot || !live || !points || mode->count() != 2 || slot->count() != 14 ||
        points->rowCount() != 4 || points->columnCount() != 5) {
        std::fprintf(stderr, "sensor calibration widgets are incomplete\n");
        return 2;
    }
    for (int point = 1; point < 4; ++point) {
        auto *actual = SensorCalibrationUiSmokeAccessor::actual(window, point);
        if (!actual || actual->maximum() != 294.999) {
            std::fprintf(stderr, "positive calibration input safety maximum is wrong\n");
            return 10;
        }
    }

    SensorCalibrationStatus emptyStatus = readyStatus(false, 1);
    emptyStatus.capturedMask = 0;
    emptyStatus.stagedComplete = false;
    emptyStatus.zeroReady = false;
    emptyStatus.rawPoints.fill(0u);
    emptyStatus.actual001mmHg.fill(0u);
    SensorCalibrationUiSmokeAccessor::setStatus(window, emptyStatus);
    app.processEvents();
    if (!points->horizontalHeaderItem(1) ||
        points->horizontalHeaderItem(1)->text() != QStringLiteral("实测气压") ||
        SensorCalibrationUiSmokeAccessor::record(window, 0)->isEnabled() ||
        !SensorCalibrationUiSmokeAccessor::state(window)->text().contains(QStringLiteral("零点未就绪"))) {
        std::fprintf(stderr, "reference calibration header or zero-ready gate is wrong\n");
        return 16;
    }
    SensorCalibrationStatus zeroReadyStatus = emptyStatus;
    zeroReadyStatus.zeroReady = true;
    SensorCalibrationUiSmokeAccessor::setStatus(window, zeroReadyStatus);
    app.processEvents();
    if (!SensorCalibrationUiSmokeAccessor::record(window, 0)->isEnabled()) {
        std::fprintf(stderr, "zero record did not enable after zero became ready\n");
        return 19;
    }
    SensorCalibrationUiSmokeAccessor::setStatus(window, emptyStatus);
    app.processEvents();
    static const double expectedActuals[4] = {0.0, 50.0, 150.0, 250.0};
    for (int point = 1; point < 4; ++point) {
        auto *actual = SensorCalibrationUiSmokeAccessor::actual(window, point);
        auto *record = SensorCalibrationUiSmokeAccessor::record(window, point);
        if (!actual || !record || actual->value() != expectedActuals[point] || !record->isEnabled()) {
            std::fprintf(stderr, "positive point defaults or record button state are wrong\n");
            return 17;
        }
    }

    SensorCalibrationStatus entryWhileBusyStatus = emptyStatus;
    entryWhileBusyStatus.sourceValid = false;
    entryWhileBusyStatus.sourceFault = true;
    entryWhileBusyStatus.autoVentActive = true;
    entryWhileBusyStatus.actuator = 3u;
    SensorCalibrationUiSmokeAccessor::setStatus(window, entryWhileBusyStatus);
    auto *manualEntry = SensorCalibrationUiSmokeAccessor::actual(window, 1);
    manualEntry->setValue(5.8);
    app.processEvents();
    if (!manualEntry->isEnabled() || manualEntry->isReadOnly() || manualEntry->value() != 5.8 ||
        SensorCalibrationUiSmokeAccessor::record(window, 1)->isEnabled()) {
        std::fprintf(stderr, "mmHg entry is coupled to sensor or actuator readiness\n");
        return 25;
    }
    manualEntry->setValue(expectedActuals[1]);
    SensorCalibrationUiSmokeAccessor::setStatus(window, emptyStatus);
    app.processEvents();

    SensorCalibrationStatus editableCapturedStatus = readyStatus(false, 1);
    editableCapturedStatus.capturedMask = 0x06;
    editableCapturedStatus.stagedComplete = false;
    editableCapturedStatus.zeroReady = false;
    editableCapturedStatus.rawPoints[1] = 879171u;
    editableCapturedStatus.actual001mmHg[1] = 47000u;
    editableCapturedStatus.rawPoints[2] = 1968155u;
    editableCapturedStatus.actual001mmHg[2] = 150000u;
    SensorCalibrationUiSmokeAccessor::setStatus(window, editableCapturedStatus);
    app.processEvents();
    auto *editablePoint = SensorCalibrationUiSmokeAccessor::actual(window, 1);
    if (!editablePoint || !editablePoint->isEnabled() || editablePoint->value() != 47.0) {
        std::fprintf(stderr, "captured calibration point did not load as editable\n");
        return 20;
    }
    editablePoint->setValue(48.125);
    SensorCalibrationUiSmokeAccessor::setStatus(window, editableCapturedStatus);
    app.processEvents();
    if (editablePoint->value() != 48.125) {
        std::fprintf(stderr, "status polling overwrote the edited captured point\n");
        return 21;
    }
    editableCapturedStatus.detail = 9;
    SensorCalibrationUiSmokeAccessor::setStatus(window, editableCapturedStatus);
    if (!SensorCalibrationUiSmokeAccessor::state(window)->text().contains(
            QStringLiteral("标定点顺序或间距不合法"))) {
        std::fprintf(stderr, "calibration detail reason is not visible\n");
        return 22;
    }
    editableCapturedStatus.rawPoints[1] = 879172u;
    editableCapturedStatus.actual001mmHg[1] = 49125u;
    editableCapturedStatus.detail = 11;
    SensorCalibrationUiSmokeAccessor::setStatus(window, editableCapturedStatus);
    if (editablePoint->value() != 49.125 ||
        !SensorCalibrationUiSmokeAccessor::state(window)->text().contains(QStringLiteral("记录完成"))) {
        std::fprintf(stderr, "new capture did not refresh the edited point or success detail\n");
        return 23;
    }
    SensorCalibrationUiSmokeAccessor::setPendingRecord(window, 1, 49125u);
    if (!SensorCalibrationUiSmokeAccessor::state(window)->text().contains(
            QStringLiteral("标定点1重新记录命令已发送"))) {
        std::fprintf(stderr, "re-record pending feedback is not visible\n");
        return 24;
    }
    SensorCalibrationUiSmokeAccessor::clearPendingRecord(window);
    SensorCalibrationUiSmokeAccessor::setStatus(window, emptyStatus);
    app.processEvents();

    SensorCalibrationUiSmokeAccessor::setStatus(window, readyStatus(true, 8));
    app.processEvents();
    if (mode->currentData().toInt() != 1 || slot->currentData().toInt() != 8 ||
        mode->isEnabled() || slot->isEnabled() ||
        !containsAll(live->text(), {QStringLiteral("压力检测8/IIC8"), QStringLiteral("罐1"),
                                    QStringLiteral("V2+V14"), QStringLiteral("进气V1"),
                                    QStringLiteral("泄压V21")}) ||
        !SensorCalibrationUiSmokeAccessor::fill(window)->text().contains(QStringLiteral("V1")) ||
        !SensorCalibrationUiSmokeAccessor::release(window)->text().contains(QStringLiteral("V21"))) {
        std::fprintf(stderr, "in-place channel sensor resource mapping is wrong\n");
        return 3;
    }

    SensorCalibrationUiSmokeAccessor::setStatus(window, readyStatus(true, 4));
    app.processEvents();
    if (!containsAll(live->text(), {QStringLiteral("压力检测4/IIC4"), QStringLiteral("罐4"),
                                    QStringLiteral("进气V7"), QStringLiteral("泄压V24")}) ||
        !SensorCalibrationUiSmokeAccessor::fill(window)->text().contains(QStringLiteral("V7")) ||
        !SensorCalibrationUiSmokeAccessor::release(window)->text().contains(QStringLiteral("V24"))) {
        std::fprintf(stderr, "in-place tank sensor resource mapping is wrong\n");
        return 4;
    }

    SensorCalibrationUiSmokeAccessor::setStatus(window, readyStatus(false, 14));
    SensorCalibrationStatus storageFaultStatus = readyStatus(false, 14);
    storageFaultStatus.storageFault = true;
    storageFaultStatus.detail = 9;
    SensorCalibrationUiSmokeAccessor::setStatus(window, storageFaultStatus);
    if (!SensorCalibrationUiSmokeAccessor::save(window)->isEnabled() ||
        !SensorCalibrationUiSmokeAccessor::state(window)->text().contains(QStringLiteral("存储故障"))) {
        std::fprintf(stderr, "storage fault warning incorrectly blocks repair Save\n");
        return 15;
    }
    SensorCalibrationUiSmokeAccessor::setStatus(window, readyStatus(false, 14));
    app.processEvents();
    if (mode->currentData().toInt() != 0 || slot->currentData().toInt() != 14 ||
        !containsAll(live->text(), {QStringLiteral("借用IIC1工位"), QStringLiteral("压力检测1/IIC1"),
                                    QStringLiteral("保存目标=压力检测14"), QStringLiteral("进气V1"),
                                    QStringLiteral("泄压V21")}) ||
        SensorCalibrationUiSmokeAccessor::enter(window)->isEnabled() ||
        !SensorCalibrationUiSmokeAccessor::exit(window)->isEnabled() ||
        !SensorCalibrationUiSmokeAccessor::fill(window)->isEnabled() ||
        !SensorCalibrationUiSmokeAccessor::release(window)->isEnabled() ||
        !SensorCalibrationUiSmokeAccessor::vent(window)->isEnabled() ||
        SensorCalibrationUiSmokeAccessor::cancelVent(window)->isEnabled() ||
        !SensorCalibrationUiSmokeAccessor::save(window)->isEnabled()) {
        std::fprintf(stderr, "fixed-IIC1 state or button gating is wrong\n");
        return 5;
    }
    if (SensorCalibrationUiSmokeAccessor::jogRefreshInterval(window) != 150) {
        std::fprintf(stderr, "jog lease refresh interval is not 150ms\n");
        return 11;
    }
    SensorCalibrationUiSmokeAccessor::simulateActiveJog(window);
    QEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(SensorCalibrationUiSmokeAccessor::fill(window), &focusOut);
    if (!SensorCalibrationUiSmokeAccessor::jogStopped(window)) {
        std::fprintf(stderr, "focus loss did not stop calibration jog renewal\n");
        return 12;
    }
    SensorCalibrationUiSmokeAccessor::simulateActiveJog(window);
    SensorCalibrationUiSmokeAccessor::fill(window)->setEnabled(false);
    if (!SensorCalibrationUiSmokeAccessor::jogStopped(window)) {
        std::fprintf(stderr, "disabling the jog button did not stop renewal\n");
        return 13;
    }
    SensorCalibrationUiSmokeAccessor::setStatus(window, readyStatus(false, 14));
    SensorCalibrationUiSmokeAccessor::simulateActiveJog(window);
    QEvent applicationDeactivate(QEvent::ApplicationDeactivate);
    QApplication::sendEvent(QCoreApplication::instance(), &applicationDeactivate);
    if (!SensorCalibrationUiSmokeAccessor::jogStopped(window)) {
        std::fprintf(stderr, "application deactivation did not stop jog renewal\n");
        return 14;
    }
    SensorCalibrationUiSmokeAccessor::setStatus(window, readyStatus(false, 14));
    for (int point = 0; point < 4; ++point) {
        const auto *stateItem = points->item(point, 3);
        if (!stateItem || stateItem->text() != QStringLiteral("已记录")) {
            std::fprintf(stderr, "captured point is not visible\n");
            return 6;
        }
    }

    auto *pressure = SensorCalibrationUiSmokeAccessor::pressure(window);
    if (!pressure || pressure->columnCount() != 4 ||
        !pressure->item(0, 2) || pressure->item(0, 2)->text() != QStringLiteral("已校准") ||
        !pressure->item(1, 2) || pressure->item(1, 2)->text() != QStringLiteral("未校准") ||
        !pressure->item(7, 2) || pressure->item(7, 2)->text() != QStringLiteral("已校准") ||
        !pressure->item(0, 3) || pressure->item(0, 3)->text() != QStringLiteral("0 次")) {
        std::fprintf(stderr, "pressure table calibration badges are wrong\n");
        return 7;
    }

    const QString screenshotPath = QString::fromLocal8Bit(qgetenv("PRESSURE_FIXTURE_UI_SCREENSHOT"));
    if (!screenshotPath.isEmpty()) {
        bool screenshotWidthOk = false;
        const int requestedWidth = qEnvironmentVariableIntValue("PRESSURE_FIXTURE_UI_WIDTH",
                                                                 &screenshotWidthOk);
        const int screenshotWidth = screenshotWidthOk && requestedWidth >= 420
            ? requestedWidth
            : 580;
        panel->setParent(nullptr);
        panel->setWindowFlag(Qt::Window, true);
        panel->resize(screenshotWidth, panel->sizeHint().height());
        panel->show();
        panel->layout()->activate();
        app.processEvents();
        SensorCalibrationUiSmokeAccessor::disconnectHardware(window);
        SensorCalibrationUiSmokeAccessor::showCalibration(window);
        SensorCalibrationStatus screenshotStatus = readyStatus(false, 1);
        screenshotStatus.capturedMask = 0;
        screenshotStatus.calibratedMask = 0;
        screenshotStatus.stagedComplete = false;
        screenshotStatus.zeroReady = false;
        screenshotStatus.liveRaw = 406981u;
        screenshotStatus.liveNominal001mmHg = 0u;
        screenshotStatus.rawPoints.fill(0u);
        screenshotStatus.actual001mmHg.fill(0u);
        for (int point = 1; point < 4; ++point) {
            SensorCalibrationUiSmokeAccessor::actual(window, point)->setValue(
                point == 1 ? 5.8 : expectedActuals[point]);
        }
        SensorCalibrationUiSmokeAccessor::setStatus(window, screenshotStatus);
        app.processEvents();
        if (!panel->grab().save(screenshotPath)) {
            std::fprintf(stderr, "failed to save sensor calibration screenshot\n");
            return 8;
        }
    }

    SensorCalibrationUiSmokeAccessor::setUnsupported(window);
    app.processEvents();
    if (SensorCalibrationUiSmokeAccessor::enter(window)->isEnabled() ||
        SensorCalibrationUiSmokeAccessor::exit(window)->isEnabled() ||
        SensorCalibrationUiSmokeAccessor::fill(window)->isEnabled() ||
        SensorCalibrationUiSmokeAccessor::release(window)->isEnabled() ||
        SensorCalibrationUiSmokeAccessor::vent(window)->isEnabled() ||
        SensorCalibrationUiSmokeAccessor::save(window)->isEnabled()) {
        std::fprintf(stderr, "old firmware capability gate is not fail-closed\n");
        return 9;
    }

    std::fprintf(stdout, "sensor calibration UI smoke ok\n");
    return 0;
}
