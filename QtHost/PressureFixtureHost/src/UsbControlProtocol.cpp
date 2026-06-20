#include "UsbControlProtocol.h"

#include <QtEndian>

namespace fixture::usb {

static void appendU16(QByteArray &out, uint16_t value)
{
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
}

static void appendU32(QByteArray &out, uint32_t value)
{
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 24) & 0xFF));
}

static uint16_t getU16(const QByteArray &bytes, qsizetype offset)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return qFromLittleEndian<uint16_t>(p);
}

static uint32_t getU32(const QByteArray &bytes, qsizetype offset)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return qFromLittleEndian<uint32_t>(p);
}

uint16_t crc16Modbus(const QByteArray &bytes)
{
    uint16_t crc = 0xFFFF;
    for (const char c : bytes) {
        crc ^= static_cast<uint8_t>(c);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001) != 0) {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

QByteArray buildFrame(uint8_t type, uint16_t sequence, uint8_t command, const QByteArray &payload)
{
    QByteArray out;
    if (payload.size() > kMaxPayload) {
        return out;
    }

    out.reserve(static_cast<int>(kMinFrameSize + payload.size()));
    out.append(static_cast<char>(kHead0));
    out.append(static_cast<char>(kHead1));
    out.append(static_cast<char>(kVersion));
    out.append(static_cast<char>(type));
    appendU16(out, sequence);
    out.append(static_cast<char>(command));
    appendU16(out, static_cast<uint16_t>(payload.size()));
    out.append(payload);

    const uint16_t crc = crc16Modbus(out.sliced(2));
    appendU16(out, crc);
    return out;
}

ParsedFrame parseOne(const QByteArray &buffer)
{
    ParsedFrame result;
    if (buffer.size() < 2) {
        result.needMore = true;
        return result;
    }

    const int head = buffer.indexOf(QByteArray::fromRawData("\xA5\x5A", 2));
    if (head < 0) {
        result.consumed = buffer.size() - 1;
        result.error = "未找到帧头";
        return result;
    }
    if (head > 0) {
        result.consumed = head;
        result.error = "跳过帧头前噪声";
        return result;
    }
    if (buffer.size() < kMinFrameSize) {
        result.needMore = true;
        return result;
    }
    if (static_cast<uint8_t>(buffer[2]) != kVersion) {
        result.consumed = kMinFrameSize;
        result.error = "协议版本不匹配";
        return result;
    }

    const uint16_t payloadLen = getU16(buffer, 7);
    if (payloadLen > kMaxPayload) {
        result.consumed = kMinFrameSize;
        result.error = "payload 长度超过上限";
        return result;
    }

    const qsizetype frameLen = kMinFrameSize + payloadLen;
    if (buffer.size() < frameLen) {
        result.needMore = true;
        return result;
    }

    const uint16_t rxCrc = getU16(buffer, 9 + payloadLen);
    const uint16_t calcCrc = crc16Modbus(buffer.sliced(2, 7 + payloadLen));
    if (rxCrc != calcCrc) {
        result.consumed = frameLen;
        result.error = "CRC 校验失败";
        return result;
    }

    result.ok = true;
    result.consumed = frameLen;
    result.frame.version = static_cast<uint8_t>(buffer[2]);
    result.frame.type = static_cast<uint8_t>(buffer[3]);
    result.frame.sequence = getU16(buffer, 4);
    result.frame.command = static_cast<uint8_t>(buffer[6]);
    result.frame.payload = buffer.sliced(9, payloadLen);
    return result;
}

QByteArray buildHello(uint16_t sequence)
{
    QByteArray payload;
    payload.append(static_cast<char>(kVersion));
    payload.append('\0');
    return buildFrame(Request, sequence, Hello, payload);
}

QByteArray buildStart(uint16_t sequence, uint16_t targetMmHg)
{
    QByteArray payload;
    appendU16(payload, targetMmHg);
    payload.append('\0');
    return buildFrame(Request, sequence, Start, payload);
}

QByteArray buildSetState(uint16_t sequence, RuntimeState state)
{
    QByteArray payload;
    payload.append(static_cast<char>(stateIndex(state)));
    return buildFrame(Request, sequence, SetState, payload);
}

QByteArray buildSetThreshold(uint16_t sequence, double thresholdMmHg)
{
    QByteArray payload;
    payload.append(static_cast<char>(0x02));
    appendU32(payload, static_cast<uint32_t>(to001mmHg(thresholdMmHg)));
    return buildFrame(Request, sequence, SetThreshold, payload);
}

QByteArray buildManualValve(uint16_t sequence, uint8_t valveNumber, bool open)
{
    QByteArray payload;
    payload.append(static_cast<char>(valveNumber));
    payload.append(static_cast<char>(open ? 1 : 0));
    appendU16(payload, 0);
    return buildFrame(Request, sequence, ManualValve, payload);
}

QByteArray buildEnterMscReboot(uint16_t sequence)
{
    return buildFrame(Request, sequence, EnterMscReboot, QByteArray("MSC!", 4));
}

QByteArray buildSetRtcTime(uint16_t sequence, uint32_t epochSeconds)
{
    QByteArray payload;
    appendU32(payload, epochSeconds);
    return buildFrame(Request, sequence, SetRtcTime, payload);
}

QByteArray buildSetPcbaCurrentRange(uint16_t sequence, bool enable50mA)
{
    QByteArray payload;
    payload.append(static_cast<char>(enable50mA ? 1 : 0));
    return buildFrame(Request, sequence, SetPcbaCurrentRange, payload);
}

QByteArray buildCalibrateAdc(uint16_t sequence)
{
    return buildFrame(Request, sequence, CalibrateAdc);
}

bool applyStatusSnapshot(const QByteArray &payload, FixtureSnapshot &snapshot)
{
    constexpr qsizetype minimumLen = 118;
    constexpr qsizetype pressureValidMaskOffset = 118;
    constexpr qsizetype currentBaseOffset = 122;
    constexpr qsizetype currentBlockBytes = kChannelCount * 4;
    constexpr qsizetype currentValidMaskOffset = 186;
    constexpr qsizetype rtcOffset = 190;
    constexpr qsizetype pcbaCurrentFlagsOffset = 195;
    constexpr qsizetype adcReferenceOffset = 196;
    constexpr qsizetype currentRawAdcOffset = 208;
    constexpr qsizetype currentRawAdcBlockBytes = kChannelCount * 2;
    if (payload.size() < minimumLen) {
        return false;
    }

    snapshot.elapsedMs = getU32(payload, 16);
    snapshot.sequence = getU16(payload, 20);
    snapshot.state = stateFromIndex(static_cast<uint8_t>(payload[5]));
    snapshot.running = (static_cast<uint8_t>(payload[6]) & 0x01) != 0;
    snapshot.paused = (static_cast<uint8_t>(payload[6]) & 0x02) != 0;
    snapshot.remoteControlEnabled = true;
    snapshot.thresholdMmHg = toMmHg(getU16(payload, 10));

    const uint32_t valveMask = getU32(payload, 12);
    snapshot.valvesOpen.fill(false);
    for (int valve = 1; valve <= kValveCount; ++valve) {
        snapshot.valvesOpen[valve] = (valveMask & (1u << (valve - 1))) != 0;
    }

    const uint16_t onlineMask = getU16(payload, 22);
    const uint16_t lowPowerMask = getU16(payload, 24);
    const uint16_t normalPowerMask = getU16(payload, 26);
    const uint16_t passMask = getU16(payload, 28);

    qsizetype offset = 30;
    for (int i = 0; i < kPressureSensorCount; ++i) {
        snapshot.pressure001mmHg[i] = static_cast<int>(getU32(payload, offset));
        snapshot.pressureValid[i] = snapshot.pressure001mmHg[i] > 0;
        offset += 4;
    }
    if (payload.size() >= pressureValidMaskOffset + 4) {
        const uint32_t pressureValidMask = getU32(payload, pressureValidMaskOffset);
        for (int i = 0; i < kPressureSensorCount; ++i) {
            snapshot.pressureValid[i] = (pressureValidMask & (1u << i)) != 0;
        }
    }
    for (int i = 0; i < kChannelCount; ++i) {
        auto &channel = snapshot.channels[i];
        channel.online = (onlineMask & (1u << i)) != 0;
        channel.lowPowerOk = (lowPowerMask & (1u << i)) != 0;
        channel.normalPowerOk = (normalPowerMask & (1u << i)) != 0;
        channel.pass = (passMask & (1u << i)) != 0;
        channel.fixturePressure001mmHg = snapshot.pressure001mmHg[6 + i];
        channel.pressure001mmHg = static_cast<int>(getU32(payload, offset));
        channel.error001mmHg = channel.pressure001mmHg - channel.fixturePressure001mmHg;
        channel.standbyCurrentValid = false;
        channel.workCurrentValid = false;
        channel.standbyCurrentUaX100 = 0;
        channel.workCurrentUaX100 = 0;
        channel.currentRawAdcValid = false;
        channel.currentRawAdc = 0;
        offset += 4;
    }
    if (payload.size() >= currentBaseOffset + currentBlockBytes) {
        uint16_t standbyCurrentValidMask = 0;
        if (payload.size() >= currentValidMaskOffset + 2) {
            standbyCurrentValidMask = getU16(payload, currentValidMaskOffset);
        }
        for (int i = 0; i < kChannelCount; ++i) {
            auto &channel = snapshot.channels[i];
            channel.standbyCurrentUaX100 = getU32(payload, currentBaseOffset + i * 4);
            channel.standbyCurrentValid = payload.size() >= currentValidMaskOffset + 2
                ? ((standbyCurrentValidMask & (1u << i)) != 0)
                : channel.standbyCurrentUaX100 > 0;
        }
    }
    if (payload.size() >= currentBaseOffset + currentBlockBytes * 2) {
        uint16_t workCurrentValidMask = 0;
        if (payload.size() >= currentValidMaskOffset + 4) {
            workCurrentValidMask = getU16(payload, currentValidMaskOffset + 2);
        }
        for (int i = 0; i < kChannelCount; ++i) {
            auto &channel = snapshot.channels[i];
            channel.workCurrentUaX100 = getU32(payload, currentBaseOffset + currentBlockBytes + i * 4);
            channel.workCurrentValid = payload.size() >= currentValidMaskOffset + 4
                ? ((workCurrentValidMask & (1u << i)) != 0)
                : channel.workCurrentUaX100 > 0;
        }
    }
    snapshot.rtcSnapshotValid = false;
    snapshot.rtcInitialized = false;
    snapshot.rtcOscillatorReady = false;
    snapshot.rtcBackupValid = false;
    snapshot.rtcEpochSeconds = 0;
    if (payload.size() >= rtcOffset + 5) {
        const uint8_t flags = static_cast<uint8_t>(payload[rtcOffset + 4]);
        snapshot.rtcSnapshotValid = true;
        snapshot.rtcEpochSeconds = getU32(payload, rtcOffset);
        snapshot.rtcInitialized = (flags & 0x01u) != 0;
        snapshot.rtcOscillatorReady = (flags & 0x02u) != 0;
        snapshot.rtcBackupValid = (flags & 0x04u) != 0;
    }
    snapshot.pcbaCurrent50mAEnabled = payload.size() > pcbaCurrentFlagsOffset &&
                                      (static_cast<uint8_t>(payload[pcbaCurrentFlagsOffset]) & 0x01u) != 0;
    snapshot.adcReferenceValid = false;
    snapshot.adcReferenceRangeError = false;
    snapshot.adcVrefintRaw = 0;
    snapshot.adcVddaMv = 3300;
    snapshot.adcScalePpm = 1000000;
    if (payload.size() >= adcReferenceOffset + 12) {
        const uint8_t flags = static_cast<uint8_t>(payload[adcReferenceOffset + 8]);
        snapshot.adcVrefintRaw = getU16(payload, adcReferenceOffset);
        snapshot.adcVddaMv = getU16(payload, adcReferenceOffset + 2);
        snapshot.adcScalePpm = getU32(payload, adcReferenceOffset + 4);
        snapshot.adcReferenceValid = (flags & 0x01u) != 0;
        snapshot.adcReferenceRangeError = (flags & 0x02u) != 0;
    }
    if (payload.size() >= currentRawAdcOffset + currentRawAdcBlockBytes) {
        for (int i = 0; i < kChannelCount; ++i) {
            auto &channel = snapshot.channels[i];
            channel.currentRawAdc = getU16(payload, currentRawAdcOffset + i * 2);
            channel.currentRawAdcValid = true;
        }
    }
    return true;
}

QString frameSummary(const Frame &frame)
{
    return QString("seq=%1 type=0x%2 cmd=%3 len=%4")
        .arg(frame.sequence)
        .arg(frame.type, 2, 16, QLatin1Char('0'))
        .arg(commandName(frame.command))
        .arg(frame.payload.size());
}

} // namespace fixture::usb
