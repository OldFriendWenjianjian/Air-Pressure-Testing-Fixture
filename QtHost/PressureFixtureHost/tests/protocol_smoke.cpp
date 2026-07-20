#include "PressureFixtureModel.h"
#include "UsbControlProtocol.h"

#include <QCoreApplication>
#include <QDebug>

using namespace fixture;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app)

    if (stateIndex(RuntimeState::SensorCalibration) != 31 ||
        stateIndex(RuntimeState::Count) != 32 ||
        stateFromIndex(31) != RuntimeState::SensorCalibration ||
        stateName(RuntimeState::SensorCalibration) != QStringLiteral("Sensor calibration") ||
        stateDisplayName(RuntimeState::SensorCalibration) != QStringLiteral("传感器校准")) {
        qCritical() << "SENSOR_CALIBRATION runtime state mapping failed";
        return 17;
    }

    const QByteArray hello = usb::buildHello(0x1234);
    const auto parsedHello = usb::parseOne(hello);
    if (!parsedHello.ok ||
        parsedHello.frame.version != usb::kVersion ||
        parsedHello.frame.sequence != 0x1234 ||
        parsedHello.frame.command != usb::Hello ||
        parsedHello.frame.payload.size() != 2 ||
        static_cast<uint8_t>(parsedHello.frame.payload[0]) != usb::kVersion) {
        qCritical() << "HELLO frame parse failed";
        return 1;
    }

    const QByteArray start = usb::buildStart(7, 285);
    const auto parsedStart = usb::parseOne(start);
    if (!parsedStart.ok ||
        parsedStart.frame.command != usb::Start ||
        parsedStart.frame.payload.size() != 3 ||
        static_cast<uint8_t>(parsedStart.frame.payload[0]) != 0x1D ||
        static_cast<uint8_t>(parsedStart.frame.payload[1]) != 0x01) {
        qCritical() << "START frame parse failed";
        return 2;
    }

    QByteArray snapshotPayload(1048, '\0');
    auto putU16 = [](QByteArray &bytes, int offset, quint16 value) {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    };
    auto putU32 = [](QByteArray &bytes, int offset, quint32 value) {
        bytes[offset] = static_cast<char>(value & 0xFF);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
        bytes[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
        bytes[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
    };
    snapshotPayload[5] = static_cast<char>(stateIndex(RuntimeState::Test285));
    snapshotPayload[6] = 0x01;
    putU16(snapshotPayload, 8, 285);
    putU16(snapshotPayload, 10, 3000);
    putU32(snapshotPayload, 12, (1u << 11) | (1u << 12));
    putU16(snapshotPayload, 22, 0x00FF);
    for (int i = 0; i < kPressureSensorCount; ++i) {
        putU32(snapshotPayload, 30 + i * 4, static_cast<quint32>((i + 1) * 1000));
    }
    for (int i = 0; i < kChannelCount; ++i) {
        putU32(snapshotPayload, 86 + i * 4, static_cast<quint32>(285000 + i));
    }
    putU32(snapshotPayload, 118, 0x3FFB & ~(1u << 2));
    for (int i = 0; i < kChannelCount; ++i) {
        putU32(snapshotPayload, 122 + i * 4, static_cast<quint32>(1000 + i));
        putU32(snapshotPayload, 154 + i * 4, static_cast<quint32>(2000 + i));
    }
    putU16(snapshotPayload, 186, 0x00FF);
    putU16(snapshotPayload, 188, 0x00FE);
    putU32(snapshotPayload, 190, 1712345678u);
    snapshotPayload[194] = 0x07;
    putU16(snapshotPayload, 196, 1490);
    putU16(snapshotPayload, 198, 3298);
    putU32(snapshotPayload, 200, 999394u);
    snapshotPayload[204] = 0x01;
    putU16(snapshotPayload, 205, (1u << 0) | (1u << 6));
    for (int i = 0; i < kChannelCount; ++i) {
        putU16(snapshotPayload, 208 + i * 2, static_cast<quint16>(123 + i));
    }
    snapshotPayload[224] = static_cast<char>(0x41);
    snapshotPayload[230] = static_cast<char>(0x40);
    snapshotPayload[238] = static_cast<char>(0x06);
    snapshotPayload[244] = static_cast<char>(0x03);
    putU32(snapshotPayload, 252, 25u);
    putU32(snapshotPayload, 284, 36u);
    for (int sample = 0; sample < kCurrentSampleCount; ++sample) {
        putU32(snapshotPayload, 316 + sample * 4, static_cast<quint32>(1000 + sample * 10));
        putU32(snapshotPayload, 636 + sample * 4, static_cast<quint32>(2000 + sample * 20));
    }
    snapshotPayload[956] = 0x01;
    snapshotPayload[957] = 0x02;
    snapshotPayload[958] = 0x03;
    snapshotPayload[959] = 0x04;
    snapshotPayload[960] = 0x07;
    putU16(snapshotPayload, 961, 0x2001);
    snapshotPayload[963] = static_cast<char>(0x5E);
    putU16(snapshotPayload, 964, 2u);
    putU16(snapshotPayload, 992, 4u);
    putU16(snapshotPayload, 1020, 1u);

    const QByteArray rtcFrame = usb::buildSetRtcTime(0x55AA, 1712345678u);
    const auto parsedRtc = usb::parseOne(rtcFrame);
    if (!parsedRtc.ok ||
        parsedRtc.frame.command != usb::SetRtcTime ||
        parsedRtc.frame.payload.size() != 4) {
        qCritical() << "SET_RTC_TIME frame parse failed";
        return 3;
    }

    const QByteArray rangeFrame = usb::buildSetPcbaCurrentRange(0x55AB, true);
    const auto parsedRange = usb::parseOne(rangeFrame);
    if (!parsedRange.ok ||
        parsedRange.frame.command != usb::SetPcbaCurrentRange ||
        parsedRange.frame.payload.size() != 1 ||
        static_cast<uint8_t>(parsedRange.frame.payload[0]) != 1u) {
        qCritical() << "SET_PCBA_CURRENT_RANGE frame parse failed";
        return 6;
    }

    const QByteArray supplyFrame = usb::buildSetPcbaSupplyVoltage(0x55AD, false);
    const auto parsedSupply = usb::parseOne(supplyFrame);
    if (!parsedSupply.ok ||
        parsedSupply.frame.command != usb::SetPcbaSupplyVoltage ||
        parsedSupply.frame.payload.size() != 1 ||
        static_cast<uint8_t>(parsedSupply.frame.payload[0]) != 45u) {
        qCritical() << "SET_PCBA_SUPPLY_VOLTAGE frame parse failed";
        return 8;
    }

    const QByteArray adcFrame = usb::buildCalibrateAdc(0x55AC);
    const auto parsedAdc = usb::parseOne(adcFrame);
    if (!parsedAdc.ok ||
        parsedAdc.frame.command != usb::CalibrateAdc ||
        !parsedAdc.frame.payload.isEmpty()) {
        qCritical() << "CALIBRATE_ADC frame parse failed";
        return 9;
    }

    const QByteArray runSingleTankPcba =
        usb::buildRunSingleTankPcba(0x55AE, false, 550u, 4200u, 750u);
    const auto parsedRunSingleTankPcba = usb::parseOne(runSingleTankPcba);
    const auto parsedRunSingleTankPcbaContinue =
        usb::parseOne(usb::buildRunSingleTankPcba(0x55AF, true));
    const QByteArray expectedRunPayload = QByteArray::fromHex("002602000068100000ee020000");
    const QByteArray expectedDefaultRunPayload = QByteArray::fromHex("01f4010000b80b0000b80b0000");
    if (!parsedRunSingleTankPcba.ok ||
        parsedRunSingleTankPcba.frame.command != usb::RunSingleTankPcba ||
        parsedRunSingleTankPcba.frame.payload != expectedRunPayload ||
        !parsedRunSingleTankPcbaContinue.ok ||
        parsedRunSingleTankPcbaContinue.frame.payload != expectedDefaultRunPayload) {
        qCritical() << "RUN_SINGLE_TANK_PCBA frame parse failed";
        return 10;
    }

    QByteArray trendReportPayload(12 + 52, '\0');
    trendReportPayload[2] = 1;
    const int trendBase = 12;
    trendReportPayload[trendBase + 0] = 4;
    trendReportPayload[trendBase + 2] = 1;
    trendReportPayload[trendBase + 4] = 15;
    putU32(trendReportPayload, trendBase + 7, 325u);
    putU32(trendReportPayload, trendBase + 12, static_cast<quint32>(-750));
    putU32(trendReportPayload, trendBase + 16, 3000000u);
    putU32(trendReportPayload, trendBase + 20, 50325u);
    SingleTankPcbaReport trendReport;
    if (!usb::parseSingleTankPcbaReport(trendReportPayload, trendReport) ||
        trendReport.count != 1u ||
        trendReport.entries[0].trendSampleCount != 15u ||
        trendReport.entries[0].trendSlope001mmHgPerSecond != -750 ||
        trendReport.entries[0].trendMaxResidual001mmHg != 325u ||
        trendReport.entries[0].trendObservationUs != 3000000u ||
        trendReport.entries[0].trendPredictedPressure001mmHg != 50325u) {
        qCritical() << "SINGLE_TANK_PCBA trend entry decode failed";
        return 18;
    }

    const auto parsedCalibrationEnter = usb::parseOne(
        usb::buildSensorCalibrationEnter(0x6001, false, 14));
    const auto parsedCalibrationEnterInPlace = usb::parseOne(
        usb::buildSensorCalibrationEnter(0x6007, true, 7));
    const auto parsedCalibrationJog = usb::parseOne(
        usb::buildSensorCalibrationJog(0x6002, usb::SensorCalibrationFill, 500));
    const auto parsedCalibrationStop = usb::parseOne(
        usb::buildSensorCalibrationJog(0x6008, usb::SensorCalibrationStop, 0));
    const auto parsedCalibrationRecord = usb::parseOne(
        usb::buildSensorCalibrationRecord(0x6003, 2, 150125));
    const auto parsedCalibrationSave = usb::parseOne(
        usb::buildSensorCalibrationSlotAction(0x6004, usb::SensorCalibrationSaveSlot, 14));
    const auto parsedCalibrationGetStaged = usb::parseOne(
        usb::buildGetSensorCalibrationStatus(0x6005));
    const auto parsedCalibrationGetSlot = usb::parseOne(
        usb::buildGetSensorCalibrationStatus(0x6006, 14));
    if (!parsedCalibrationEnter.ok ||
        parsedCalibrationEnter.frame.command != usb::SensorCalibrationAction ||
        parsedCalibrationEnter.frame.payload != QByteArray("\x01\x00\x0E", 3) ||
        !parsedCalibrationEnterInPlace.ok ||
        parsedCalibrationEnterInPlace.frame.payload != QByteArray("\x01\x01\x07", 3) ||
        !parsedCalibrationJog.ok || parsedCalibrationJog.frame.payload.size() != 4 ||
        static_cast<uint8_t>(parsedCalibrationJog.frame.payload[0]) != usb::SensorCalibrationJog ||
        static_cast<uint8_t>(parsedCalibrationJog.frame.payload[1]) != usb::SensorCalibrationFill ||
        static_cast<uint8_t>(parsedCalibrationJog.frame.payload[2]) != 0xF4u ||
        static_cast<uint8_t>(parsedCalibrationJog.frame.payload[3]) != 0x01u ||
        !parsedCalibrationStop.ok ||
        parsedCalibrationStop.frame.payload != QByteArray("\x03\x00\x00\x00", 4) ||
        !parsedCalibrationRecord.ok || parsedCalibrationRecord.frame.payload.size() != 6 ||
        static_cast<uint8_t>(parsedCalibrationRecord.frame.payload[0]) != usb::SensorCalibrationRecord ||
        static_cast<uint8_t>(parsedCalibrationRecord.frame.payload[1]) != 2u ||
        !parsedCalibrationSave.ok || parsedCalibrationSave.frame.payload.size() != 2 ||
        static_cast<uint8_t>(parsedCalibrationSave.frame.payload[1]) != 14u ||
        !parsedCalibrationGetStaged.ok || !parsedCalibrationGetStaged.frame.payload.isEmpty() ||
        !parsedCalibrationGetSlot.ok || parsedCalibrationGetSlot.frame.payload != QByteArray(1, '\x0E')) {
        qCritical() << "SENSOR_CALIBRATION action frame build failed";
        return 15;
    }

    QByteArray calibrationStatusPayload(48, '\0');
    calibrationStatusPayload[0] = 1;
    calibrationStatusPayload[1] = 0x7B;
    calibrationStatusPayload[2] = usb::SensorCalibrationRelease | 0x04;
    calibrationStatusPayload[3] = 0x0D;
    putU16(calibrationStatusPayload, 4, 0x2001);
    calibrationStatusPayload[6] = 14;
    calibrationStatusPayload[7] = 7;
    putU32(calibrationStatusPayload, 8, 1234567);
    putU32(calibrationStatusPayload, 12, 149875);
    for (int point = 0; point < 4; ++point) {
        putU32(calibrationStatusPayload, 16 + point * 8, static_cast<quint32>(1000000 + point * 500000));
        putU32(calibrationStatusPayload, 20 + point * 8, static_cast<quint32>(point * 50000));
    }
    SensorCalibrationStatus calibrationStatus;
    if (!usb::parseSensorCalibrationStatus(calibrationStatusPayload, calibrationStatus) ||
        calibrationStatus.version != 1u || !calibrationStatus.active ||
        !calibrationStatus.sourceValid || calibrationStatus.sourceFault ||
        !calibrationStatus.storageLoaded || !calibrationStatus.stagedComplete ||
        !calibrationStatus.zeroReady || !calibrationStatus.autoVentActive ||
        calibrationStatus.storageFault ||
        calibrationStatus.actuator != usb::SensorCalibrationRelease ||
        !calibrationStatus.inPlaceMode ||
        calibrationStatus.capturedMask != 0x0Du || calibrationStatus.calibratedMask != 0x2001u ||
        calibrationStatus.selectedSlot != 14u || calibrationStatus.detail != 7u ||
        calibrationStatus.liveRaw != 1234567u || calibrationStatus.liveNominal001mmHg != 149875u ||
        calibrationStatus.rawPoints[3] != 2500000u ||
        calibrationStatus.actual001mmHg[3] != 150000u ||
        usb::parseSensorCalibrationStatus(calibrationStatusPayload.left(47), calibrationStatus)) {
        qCritical() << "SENSOR_CALIBRATION status decode failed";
        return 16;
    }

    QByteArray singleTankPcbaPayload(12 + 13 * 24, '\0');
    singleTankPcbaPayload[0] = 0x00;
    singleTankPcbaPayload[1] = 0x01;
    singleTankPcbaPayload[2] = 13;
    singleTankPcbaPayload[3] = 0x01;
    putU32(singleTankPcbaPayload, 4, 1234);
    putU32(singleTankPcbaPayload, 8, 567890);
    singleTankPcbaPayload[12] = 2;
    singleTankPcbaPayload[14] = 1;
    singleTankPcbaPayload[15] = 0x0A;
    putU32(singleTankPcbaPayload, 24, 1234);
    singleTankPcbaPayload[36] = 1;
    singleTankPcbaPayload[37] = 0x7F;
    singleTankPcbaPayload[38] = 1;
    singleTankPcbaPayload[39] = 0x05;
    singleTankPcbaPayload[40] = 0x7F;
    singleTankPcbaPayload[41] = 0x00;
    singleTankPcbaPayload[42] = 0x01;
    singleTankPcbaPayload[43] = 0x00;
    putU32(singleTankPcbaPayload, 52, 8765);

    SingleTankPcbaReport singleTankPcbaReport;
    if (!usb::parseSingleTankPcbaReport(singleTankPcbaPayload, singleTankPcbaReport) ||
        !singleTankPcbaReport.done ||
        !singleTankPcbaReport.finalPass ||
        singleTankPcbaReport.count != 13 ||
        singleTankPcbaReport.standbyCurrentUaX100 != 1234u ||
        singleTankPcbaReport.workCurrentUaX100 != 567890u ||
        singleTankPcbaReport.entries[0].kind != 2u ||
        singleTankPcbaReport.entries[0].currentUaX100 != 1234u ||
        singleTankPcbaReport.entries[1].responseCommandOrByte != 0x7Fu ||
        singleTankPcbaReport.entries[1].responseLength != 1u ||
        singleTankPcbaReport.entries[1].responseData[0] != 0x00u ||
        singleTankPcbaReport.entries[1].elapsedUs != 8765u) {
        qCritical() << "SINGLE_TANK_PCBA report decode failed";
        return 11;
    }

    QByteArray pcbaTimingRawPayload(4 + 40, '\0');
    pcbaTimingRawPayload[1] = 0x01;
    pcbaTimingRawPayload[2] = 1;
    pcbaTimingRawPayload[3] = 0x01;
    pcbaTimingRawPayload[4] = 1;
    pcbaTimingRawPayload[5] = 0x03;
    pcbaTimingRawPayload[6] = 1;
    pcbaTimingRawPayload[7] = 0x7F;
    pcbaTimingRawPayload[8] = 0x05;
    pcbaTimingRawPayload[9] = 1;
    pcbaTimingRawPayload[10] = 0x01;
    pcbaTimingRawPayload[14] = 9;
    putU32(pcbaTimingRawPayload, 16, 9000);
    const uint8_t ackRaw[] = {0x55, 0xAA, 0x7F, 0x05, 0x01, 0x00, 0x00, 0x12, 0x34};
    for (int i = 0; i < 9; ++i) {
        pcbaTimingRawPayload[20 + i] = static_cast<char>(ackRaw[i]);
    }
    PcbaTimingReport pcbaTimingRawReport;
    if (!usb::parsePcbaTimingReport(pcbaTimingRawPayload, pcbaTimingRawReport) ||
        pcbaTimingRawReport.count != 1 ||
        pcbaTimingRawReport.entries[0].rawResponseLength != 9u ||
        pcbaTimingRawReport.entries[0].rawResponse[0] != 0x55u ||
        pcbaTimingRawReport.entries[0].rawResponse[3] != 0x05u ||
        pcbaTimingRawReport.entries[0].rawResponse[8] != 0x34u ||
        pcbaTimingRawReport.entries[0].elapsedUs != 9000u) {
        qCritical() << "PCBA_TIMING raw report decode failed";
        return 12;
    }

    QByteArray singleTankPcbaRawPayload(12 + 52, '\0');
    singleTankPcbaRawPayload[1] = 0x01;
    singleTankPcbaRawPayload[2] = 1;
    singleTankPcbaRawPayload[3] = 0x01;
    putU32(singleTankPcbaRawPayload, 4, 111);
    putU32(singleTankPcbaRawPayload, 8, 222);
    singleTankPcbaRawPayload[12] = 1;
    singleTankPcbaRawPayload[13] = 0x11;
    singleTankPcbaRawPayload[14] = 1;
    singleTankPcbaRawPayload[16] = 0x11;
    singleTankPcbaRawPayload[18] = 4;
    singleTankPcbaRawPayload[19] = 0x78;
    singleTankPcbaRawPayload[20] = 0x56;
    singleTankPcbaRawPayload[21] = 0x34;
    singleTankPcbaRawPayload[22] = 0x12;
    putU32(singleTankPcbaRawPayload, 24, 12345);
    putU32(singleTankPcbaRawPayload, 28, 6789);
    putU32(singleTankPcbaRawPayload, 32, 0x12345678);
    singleTankPcbaRawPayload[36] = 12;
    const uint8_t pressureRaw[] = {0x55, 0xAA, 0x11, 0x09, 0x04, 0x00, 0x78, 0x56, 0x34, 0x12, 0xAB, 0xCD};
    for (int i = 0; i < 12; ++i) {
        singleTankPcbaRawPayload[40 + i] = static_cast<char>(pressureRaw[i]);
    }
    SingleTankPcbaReport singleTankPcbaRawReport;
    if (!usb::parseSingleTankPcbaReport(singleTankPcbaRawPayload, singleTankPcbaRawReport) ||
        singleTankPcbaRawReport.count != 1 ||
        singleTankPcbaRawReport.entries[0].rawResponseLength != 12u ||
        singleTankPcbaRawReport.entries[0].rawResponse[0] != 0x55u ||
        singleTankPcbaRawReport.entries[0].rawResponse[3] != 0x09u ||
        singleTankPcbaRawReport.entries[0].rawResponse[11] != 0xCDu ||
        singleTankPcbaRawReport.entries[0].comparisonPressure001mmHg != 12345u) {
        qCritical() << "SINGLE_TANK_PCBA raw report decode failed";
        return 13;
    }

    QByteArray fullSingleTankPcbaPayload(12 + (23 * 52), '\0');
    fullSingleTankPcbaPayload[1] = 1;
    fullSingleTankPcbaPayload[2] = 23;
    fullSingleTankPcbaPayload[3] = 1;
    for (int step = 0; step < 23; ++step) {
        const qsizetype base = 12 + (step * 52);
        fullSingleTankPcbaPayload[base + 0] = 1;
        fullSingleTankPcbaPayload[base + 1] = static_cast<char>(step);
        fullSingleTankPcbaPayload[base + 2] = 1;
    }
    const QByteArray fullSingleTankPcbaFrame = usb::buildFrame(
        usb::Report, 0x55AF, usb::GetSingleTankPcba, fullSingleTankPcbaPayload);
    const auto parsedFullSingleTankPcbaFrame = usb::parseOne(fullSingleTankPcbaFrame);
    SingleTankPcbaReport fullSingleTankPcbaReport;
    if (fullSingleTankPcbaFrame.isEmpty() ||
        !parsedFullSingleTankPcbaFrame.ok ||
        !usb::parseSingleTankPcbaReport(parsedFullSingleTankPcbaFrame.frame.payload,
                                        fullSingleTankPcbaReport) ||
        fullSingleTankPcbaReport.count != 23 ||
        !fullSingleTankPcbaReport.done ||
        !fullSingleTankPcbaReport.finalPass) {
        qCritical() << "SINGLE_TANK_PCBA full 23-step report failed";
        return 14;
    }

    snapshotPayload[195] = 0x01;
    snapshotPayload[207] = 0x01;

    FixtureSnapshot snapshot;
    if (!usb::applyStatusSnapshot(snapshotPayload, snapshot) ||
        snapshot.state != RuntimeState::Test285 ||
        !snapshot.valvesOpen[12] ||
        !snapshot.channels[0].online ||
        snapshot.channels[7].pressure001mmHg != 285007 ||
        !snapshot.channels[0].standbyCurrentValid ||
        snapshot.channels[0].standbyCurrentUaX100 != 1000 ||
        snapshot.channels[0].standbyCurrentVarianceUa2 != 25u ||
        snapshot.channels[0].standbyCurrentSamplesUaX100[0] != 1000u ||
        snapshot.channels[0].standbyCurrentSamplesUaX100[9] != 1090u ||
        snapshot.channels[0].workCurrentValid ||
        !snapshot.channels[1].workCurrentValid ||
        snapshot.channels[1].workCurrentUaX100 != 2001 ||
        !snapshot.pressureValid[0] ||
        snapshot.pressureValid[2] ||
        !snapshot.pressureFaultLatched[0] ||
        snapshot.pressureFaultLatched[1] ||
        !snapshot.pressureFaultLatched[6] ||
        snapshot.pressureStatusByte[0] != 0x41u ||
        snapshot.pressureFaultCode[0] != 0x06u ||
        snapshot.pressureStatusByte[6] != 0x40u ||
        snapshot.pressureFaultCode[6] != 0x03u ||
        snapshot.pressureMathSaturationEventCount[0] != 2u ||
        snapshot.pressureMathSaturationAttemptCount[0] != 4u ||
        snapshot.pressureMathSaturationSuccessCount[0] != 1u ||
        snapshot.pressureMathSaturationEventCount[1] != 0u ||
        pressureSensorFaultReasonText(snapshot, 0) != QStringLiteral("数学饱和 (0x41)") ||
        sensorPressureText(snapshot, 0, 1, true) != QStringLiteral("故障锁定 | 数学饱和 (0x41)") ||
        !snapshot.rtcSnapshotValid ||
        snapshot.rtcEpochSeconds != 1712345678u ||
        !snapshot.rtcInitialized ||
        !snapshot.rtcOscillatorReady ||
        !snapshot.rtcBackupValid ||
        !snapshot.pcbaCurrent50mAEnabled ||
        !snapshot.pcbaSupply5VEnabled ||
        snapshot.pcbaSupply45VEnabled ||
        !snapshot.adcReferenceValid ||
        snapshot.adcReferenceRangeError ||
        snapshot.adcVrefintRaw != 1490 ||
        snapshot.adcVddaMv != 3298 ||
        snapshot.adcScalePpm != 999394u ||
        !snapshot.channels[0].currentRawAdcValid ||
        snapshot.channels[0].currentRawAdc != 123 ||
        snapshot.channels[7].currentRawAdc != 130 ||
        !snapshot.singleTankProtectionActive ||
        snapshot.singleTankProtectionReason != 0x02u ||
        snapshot.singleTankProtectionTankIndex != 3 ||
        snapshot.singleTankProtectionSensorIndex != 4 ||
        snapshot.singleTankProtectionInletValve != 7 ||
        !snapshot.pressureCalibrationStatusAvailable ||
        snapshot.pressureCalibrationValidMask != 0x2001u ||
        !snapshot.pressureCalibrationModeActive ||
        snapshot.pressureCalibrationActuator != usb::SensorCalibrationRelease ||
        snapshot.pressureCalibrationCapturedMask != 0x0Bu ||
        snapshot.pressureCalibrationStorageFault) {
        qCritical() << "STATUS snapshot decode failed";
        return 4;
    }

    QByteArray legacyPayload = snapshotPayload.left(118);
    putU32(legacyPayload, 30 + 4 * 4, 0);
    FixtureSnapshot legacySnapshot;
    if (!usb::applyStatusSnapshot(legacyPayload, legacySnapshot) ||
        !legacySnapshot.pressureValid[0] ||
        legacySnapshot.pressureValid[4] ||
        legacySnapshot.pressureFaultLatched[0] ||
        legacySnapshot.pressureCalibrationStatusAvailable ||
        legacySnapshot.pressureCalibrationValidMask != 0u) {
        qCritical() << "Legacy STATUS pressure validity fallback failed";
        return 5;
    }

    return 0;
}
