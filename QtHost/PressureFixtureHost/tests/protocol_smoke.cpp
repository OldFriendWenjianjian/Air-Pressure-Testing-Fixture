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

    QByteArray snapshotPayload(224, '\0');
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
    for (int i = 0; i < kChannelCount; ++i) {
        putU16(snapshotPayload, 208 + i * 2, static_cast<quint16>(123 + i));
    }

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

    const QByteArray adcFrame = usb::buildCalibrateAdc(0x55AC);
    const auto parsedAdc = usb::parseOne(adcFrame);
    if (!parsedAdc.ok ||
        parsedAdc.frame.command != usb::CalibrateAdc ||
        !parsedAdc.frame.payload.isEmpty()) {
        qCritical() << "CALIBRATE_ADC frame parse failed";
        return 7;
    }

    snapshotPayload[195] = 0x01;

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
        !snapshot.rtcSnapshotValid ||
        snapshot.rtcEpochSeconds != 1712345678u ||
        !snapshot.rtcInitialized ||
        !snapshot.rtcOscillatorReady ||
        !snapshot.rtcBackupValid ||
        !snapshot.pcbaCurrent50mAEnabled ||
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
        legacySnapshot.pressureValid[4]) {
        qCritical() << "Legacy STATUS pressure validity fallback failed";
        return 5;
    }

    return 0;
}
