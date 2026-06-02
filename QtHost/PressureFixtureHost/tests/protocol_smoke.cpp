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

    QByteArray snapshotPayload(118, '\0');
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

    FixtureSnapshot snapshot;
    if (!usb::applyStatusSnapshot(snapshotPayload, snapshot) ||
        snapshot.state != RuntimeState::Test285 ||
        !snapshot.valvesOpen[12] ||
        !snapshot.channels[0].online ||
        snapshot.channels[7].pressure001mmHg != 285007) {
        qCritical() << "STATUS snapshot decode failed";
        return 3;
    }

    return 0;
}
