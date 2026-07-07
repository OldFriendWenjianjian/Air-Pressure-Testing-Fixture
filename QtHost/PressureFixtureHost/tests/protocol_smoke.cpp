#include "PressureFixtureModel.h"
#include "UsbControlProtocol.h"

#include <QCoreApplication>
#include <QDebug>

using namespace fixture;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app)

    const QByteArray hello = usb::buildHello(0x1234);
    const auto parsedHello = usb::parseOne(hello);
    if (!parsedHello.ok ||
        parsedHello.frame.sequence != 0x1234 ||
        parsedHello.frame.command != usb::Hello ||
        parsedHello.frame.payload.size() != 2) {
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

    QByteArray snapshotPayload(252, '\0');
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

    const QByteArray runSingleTankPcba = usb::buildRunSingleTankPcba(0x55AE);
    const auto parsedRunSingleTankPcba = usb::parseOne(runSingleTankPcba);
    if (!parsedRunSingleTankPcba.ok ||
        parsedRunSingleTankPcba.frame.command != usb::RunSingleTankPcba ||
        !parsedRunSingleTankPcba.frame.payload.isEmpty()) {
        qCritical() << "RUN_SINGLE_TANK_PCBA frame parse failed";
        return 10;
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
        singleTankPcbaRawReport.entries[0].rawResponse[11] != 0xCDu) {
        qCritical() << "SINGLE_TANK_PCBA raw report decode failed";
        return 13;
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
        snapshot.channels[7].currentRawAdc != 130) {
        qCritical() << "STATUS snapshot decode failed";
        return 4;
    }

    QByteArray legacyPayload = snapshotPayload.left(118);
    putU32(legacyPayload, 30 + 4 * 4, 0);
    FixtureSnapshot legacySnapshot;
    if (!usb::applyStatusSnapshot(legacyPayload, legacySnapshot) ||
        !legacySnapshot.pressureValid[0] ||
        legacySnapshot.pressureValid[4] ||
        legacySnapshot.pressureFaultLatched[0]) {
        qCritical() << "Legacy STATUS pressure validity fallback failed";
        return 5;
    }

    return 0;
}
