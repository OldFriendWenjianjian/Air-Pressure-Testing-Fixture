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

static int32_t getS32(const QByteArray &bytes, qsizetype offset)
{
    const uint32_t value = getU32(bytes, offset);
    const int64_t signedValue = (value & 0x80000000u) != 0u
        ? static_cast<int64_t>(value) - (int64_t{1} << 32)
        : static_cast<int64_t>(value);
    return static_cast<int32_t>(signedValue);
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

QByteArray buildSetValveMask(uint16_t sequence, uint32_t valveMask, uint32_t openMask)
{
    QByteArray payload;
    appendU32(payload, valveMask);
    appendU32(payload, openMask & valveMask);
    return buildFrame(Request, sequence, SetValveMask, payload);
}

QByteArray buildSingleTankLoop(uint16_t sequence,
                               uint8_t tankIndex,
                               double targetMmHg,
                               double toleranceMmHg,
                               bool enable)
{
    QByteArray payload;
    payload.append(static_cast<char>(tankIndex));
    appendU32(payload, static_cast<uint32_t>(to001mmHg(targetMmHg)));
    appendU32(payload, static_cast<uint32_t>(to001mmHg(toleranceMmHg)));
    payload.append(static_cast<char>(enable ? 1 : 0));
    return buildFrame(Request, sequence, SingleTankLoop, payload);
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

QByteArray buildSetPcbaSupplyVoltage(uint16_t sequence, bool enable5V)
{
    QByteArray payload;
    payload.append(static_cast<char>(enable5V ? 50 : 45));
    return buildFrame(Request, sequence, SetPcbaSupplyVoltage, payload);
}

QByteArray buildCalibrateAdc(uint16_t sequence)
{
    return buildFrame(Request, sequence, CalibrateAdc);
}

QByteArray buildRunPcbaTiming(uint16_t sequence, bool stopOnFail)
{
    QByteArray payload;
    payload.append(static_cast<char>(stopOnFail ? 1 : 0));
    return buildFrame(Request, sequence, RunPcbaTiming, payload);
}

QByteArray buildGetPcbaTiming(uint16_t sequence)
{
    return buildFrame(Request, sequence, GetPcbaTiming);
}

QByteArray buildRunSingleTankPcba(uint16_t sequence,
                                  bool continueOnFail,
                                  uint32_t maxDeviation001mmHg,
                                  uint32_t trendWindowMs,
                                  uint32_t maxDropRate001mmHgPerSecond)
{
    QByteArray payload;
    payload.append(static_cast<char>(continueOnFail ? 1 : 0));
    appendU32(payload, maxDeviation001mmHg);
    appendU32(payload, trendWindowMs);
    appendU32(payload, maxDropRate001mmHgPerSecond);
    return buildFrame(Request, sequence, RunSingleTankPcba, payload);
}

QByteArray buildGetSingleTankPcba(uint16_t sequence)
{
    return buildFrame(Request, sequence, GetSingleTankPcba);
}

QByteArray buildSensorCalibrationSimpleAction(uint16_t sequence, uint8_t operation)
{
    QByteArray payload(1, static_cast<char>(operation));
    return buildFrame(Request, sequence, SensorCalibrationAction, payload);
}

QByteArray buildSensorCalibrationEnter(uint16_t sequence, bool inPlaceMode, uint8_t slot)
{
    QByteArray payload;
    payload.append(static_cast<char>(SensorCalibrationEnter));
    payload.append(static_cast<char>(inPlaceMode ? 1 : 0));
    payload.append(static_cast<char>(slot));
    return buildFrame(Request, sequence, SensorCalibrationAction, payload);
}

QByteArray buildSensorCalibrationJog(uint16_t sequence, uint8_t actuator, uint16_t leaseMs)
{
    QByteArray payload;
    payload.append(static_cast<char>(SensorCalibrationJog));
    payload.append(static_cast<char>(actuator));
    appendU16(payload, leaseMs);
    return buildFrame(Request, sequence, SensorCalibrationAction, payload);
}

QByteArray buildSensorCalibrationRecord(uint16_t sequence, uint8_t point, uint32_t actual001mmHg)
{
    QByteArray payload;
    payload.append(static_cast<char>(SensorCalibrationRecord));
    payload.append(static_cast<char>(point));
    appendU32(payload, actual001mmHg);
    return buildFrame(Request, sequence, SensorCalibrationAction, payload);
}

QByteArray buildSensorCalibrationSlotAction(uint16_t sequence, uint8_t operation, uint8_t slot)
{
    QByteArray payload;
    payload.append(static_cast<char>(operation));
    payload.append(static_cast<char>(slot));
    return buildFrame(Request, sequence, SensorCalibrationAction, payload);
}

QByteArray buildGetSensorCalibrationStatus(uint16_t sequence, uint8_t slot)
{
    QByteArray payload;
    if (slot != 0u) {
        payload.append(static_cast<char>(slot));
    }
    return buildFrame(Request, sequence, GetSensorCalibrationStatus, payload);
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
    constexpr qsizetype pressureStatusByteOffset = 224;
    constexpr qsizetype pressureFaultCodeOffset = 238;
    constexpr qsizetype pcbaPowerFlagsOffset = 207;
    constexpr qsizetype currentVarianceOffset = 252;
    constexpr qsizetype currentVarianceBlockBytes = kChannelCount * 4;
    constexpr qsizetype currentSamplesOffset = currentVarianceOffset + (currentVarianceBlockBytes * 2);
    constexpr qsizetype currentSamplesBlockBytes = kChannelCount * kCurrentSampleCount * 4;
    constexpr qsizetype singleTankProtectionOffset = currentSamplesOffset + (currentSamplesBlockBytes * 2);
    constexpr qsizetype pressureCalibrationMaskOffset = 961;
    constexpr qsizetype pressureCalibrationFlagsOffset = 963;
    constexpr qsizetype pressureMathSaturationEventOffset = 964;
    constexpr qsizetype pressureMathSaturationAttemptOffset =
        pressureMathSaturationEventOffset + (kPressureSensorCount * 2);
    constexpr qsizetype pressureMathSaturationSuccessOffset =
        pressureMathSaturationAttemptOffset + (kPressureSensorCount * 2);
    if (payload.size() < minimumLen) {
        return false;
    }

    snapshot.elapsedMs = getU32(payload, 16);
    snapshot.sequence = getU16(payload, 20);
    snapshot.state = stateFromIndex(static_cast<uint8_t>(payload[5]));
    snapshot.running = (static_cast<uint8_t>(payload[6]) & 0x01) != 0;
    snapshot.paused = (static_cast<uint8_t>(payload[6]) & 0x02) != 0;
    snapshot.singlePcbaFlowActive = (static_cast<uint8_t>(payload[6]) & 0x10) != 0;
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
        snapshot.pressureFaultLatched[i] = false;
        snapshot.pressureStatusByte[i] = 0u;
        snapshot.pressureFaultCode[i] = 0u;
        offset += 4;
    }
    if (payload.size() >= pressureValidMaskOffset + 4) {
        const uint32_t pressureValidMask = getU32(payload, pressureValidMaskOffset);
        for (int i = 0; i < kPressureSensorCount; ++i) {
            snapshot.pressureValid[i] = (pressureValidMask & (1u << i)) != 0;
        }
    }
    if (payload.size() >= 207) {
        const uint16_t pressureFaultMask = getU16(payload, 205);
        for (int i = 0; i < kPressureSensorCount; ++i) {
            snapshot.pressureFaultLatched[i] = (pressureFaultMask & (1u << i)) != 0;
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
        channel.standbyCurrentVarianceUa2 = 0;
        channel.workCurrentVarianceUa2 = 0;
        channel.standbyCurrentSamplesUaX100.fill(0);
        channel.workCurrentSamplesUaX100.fill(0);
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
    snapshot.pcbaSupply5VEnabled = payload.size() > pcbaPowerFlagsOffset &&
                                   (static_cast<uint8_t>(payload[pcbaPowerFlagsOffset]) & 0x01u) != 0;
    snapshot.pcbaSupply45VEnabled = payload.size() > pcbaPowerFlagsOffset &&
                                    (static_cast<uint8_t>(payload[pcbaPowerFlagsOffset]) & 0x02u) != 0;
    snapshot.adcReferenceValid = false;
    snapshot.adcReferenceRangeError = false;
    snapshot.adcVrefintRaw = 0;
    snapshot.adcVddaMv = 3300;
    snapshot.adcScalePpm = 1000000;
    snapshot.singleTankProtectionActive = false;
    snapshot.singleTankProtectionReason = 0u;
    snapshot.singleTankProtectionTankIndex = -1;
    snapshot.singleTankProtectionSensorIndex = -1;
    snapshot.singleTankProtectionInletValve = 0;
    snapshot.pressureCalibrationStatusAvailable = false;
    snapshot.pressureCalibrationValidMask = 0u;
    snapshot.pressureCalibrationModeActive = false;
    snapshot.pressureCalibrationActuator = 0u;
    snapshot.pressureCalibrationCapturedMask = 0u;
    snapshot.pressureCalibrationStorageFault = false;
    snapshot.pressureMathSaturationEventCount.fill(0u);
    snapshot.pressureMathSaturationAttemptCount.fill(0u);
    snapshot.pressureMathSaturationSuccessCount.fill(0u);
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
    if (payload.size() >= pressureStatusByteOffset + kPressureSensorCount) {
        for (int i = 0; i < kPressureSensorCount; ++i) {
            snapshot.pressureStatusByte[i] = static_cast<uint8_t>(payload[pressureStatusByteOffset + i]);
        }
    }
    if (payload.size() >= pressureFaultCodeOffset + kPressureSensorCount) {
        for (int i = 0; i < kPressureSensorCount; ++i) {
            snapshot.pressureFaultCode[i] = static_cast<uint8_t>(payload[pressureFaultCodeOffset + i]);
        }
    }
    if (payload.size() >= currentVarianceOffset + currentVarianceBlockBytes * 2) {
        for (int i = 0; i < kChannelCount; ++i) {
            auto &channel = snapshot.channels[i];
            channel.standbyCurrentVarianceUa2 = getU32(payload, currentVarianceOffset + i * 4);
            channel.workCurrentVarianceUa2 = getU32(payload, currentVarianceOffset + currentVarianceBlockBytes + i * 4);
        }
    }
    if (payload.size() >= currentSamplesOffset + currentSamplesBlockBytes) {
        for (int i = 0; i < kChannelCount; ++i) {
            auto &channel = snapshot.channels[i];
            for (int sample = 0; sample < kCurrentSampleCount; ++sample) {
                const qsizetype sampleOffset = (i * kCurrentSampleCount + sample) * 4;
                channel.standbyCurrentSamplesUaX100[sample] = getU32(payload, currentSamplesOffset + sampleOffset);
            }
        }
    }
    if (payload.size() >= currentSamplesOffset + currentSamplesBlockBytes * 2) {
        for (int i = 0; i < kChannelCount; ++i) {
            auto &channel = snapshot.channels[i];
            for (int sample = 0; sample < kCurrentSampleCount; ++sample) {
                const qsizetype sampleOffset = (i * kCurrentSampleCount + sample) * 4;
                channel.workCurrentSamplesUaX100[sample] =
                    getU32(payload, currentSamplesOffset + currentSamplesBlockBytes + sampleOffset);
            }
        }
    }
    if (payload.size() >= singleTankProtectionOffset + 5) {
        snapshot.singleTankProtectionActive =
            (static_cast<uint8_t>(payload[singleTankProtectionOffset]) & 0x01u) != 0;
        snapshot.singleTankProtectionReason =
            static_cast<uint8_t>(payload[singleTankProtectionOffset + 1]);
        snapshot.singleTankProtectionTankIndex = snapshot.singleTankProtectionActive
            ? static_cast<int>(static_cast<uint8_t>(payload[singleTankProtectionOffset + 2]))
            : -1;
        snapshot.singleTankProtectionSensorIndex = snapshot.singleTankProtectionActive
            ? static_cast<int>(static_cast<uint8_t>(payload[singleTankProtectionOffset + 3]))
            : -1;
        snapshot.singleTankProtectionInletValve = snapshot.singleTankProtectionActive
            ? static_cast<int>(static_cast<uint8_t>(payload[singleTankProtectionOffset + 4]))
            : 0;
    }
    if (payload.size() > pressureCalibrationFlagsOffset) {
        const uint8_t flags = static_cast<uint8_t>(payload[pressureCalibrationFlagsOffset]);
        snapshot.pressureCalibrationStatusAvailable = true;
        snapshot.pressureCalibrationValidMask = getU16(payload, pressureCalibrationMaskOffset);
        snapshot.pressureCalibrationActuator = flags & 0x03u;
        snapshot.pressureCalibrationModeActive = (flags & 0x04u) != 0u;
        snapshot.pressureCalibrationCapturedMask = (flags >> 3) & 0x0Fu;
        snapshot.pressureCalibrationStorageFault = (flags & 0x80u) != 0u;
    }
    if (payload.size() >= pressureMathSaturationSuccessOffset + (kPressureSensorCount * 2)) {
        for (int i = 0; i < kPressureSensorCount; ++i) {
            snapshot.pressureMathSaturationEventCount[i] =
                getU16(payload, pressureMathSaturationEventOffset + i * 2);
            snapshot.pressureMathSaturationAttemptCount[i] =
                getU16(payload, pressureMathSaturationAttemptOffset + i * 2);
            snapshot.pressureMathSaturationSuccessCount[i] =
                getU16(payload, pressureMathSaturationSuccessOffset + i * 2);
        }
    }
    return true;
}

bool parseSensorCalibrationStatus(const QByteArray &payload, SensorCalibrationStatus &status)
{
    constexpr qsizetype statusLength = 48;
    constexpr qsizetype pointBaseOffset = 16;
    constexpr qsizetype pointSize = 8;
    if (payload.size() != statusLength) {
        return false;
    }

    const uint8_t flags = static_cast<uint8_t>(payload[1]);
    status = SensorCalibrationStatus{};
    status.version = static_cast<uint8_t>(payload[0]);
    status.active = (flags & 0x01u) != 0u;
    status.sourceValid = (flags & 0x02u) != 0u;
    status.sourceFault = (flags & 0x04u) != 0u;
    status.storageLoaded = (flags & 0x08u) != 0u;
    status.stagedComplete = (flags & 0x10u) != 0u;
    status.zeroReady = (flags & 0x20u) != 0u;
    status.autoVentActive = (flags & 0x40u) != 0u;
    status.storageFault = (flags & 0x80u) != 0u;
    status.actuator = static_cast<uint8_t>(payload[2]) & 0x03u;
    status.inPlaceMode = (static_cast<uint8_t>(payload[2]) & 0x04u) != 0u;
    status.capturedMask = static_cast<uint8_t>(payload[3]) & 0x0Fu;
    status.calibratedMask = getU16(payload, 4);
    status.selectedSlot = static_cast<uint8_t>(payload[6]);
    status.detail = static_cast<uint8_t>(payload[7]);
    status.liveRaw = getU32(payload, 8);
    status.liveNominal001mmHg = getU32(payload, 12);
    for (int point = 0; point < 4; ++point) {
        const qsizetype offset = pointBaseOffset + point * pointSize;
        status.rawPoints[point] = getU32(payload, offset);
        status.actual001mmHg[point] = getU32(payload, offset + 4);
    }
    return true;
}

bool parsePcbaTimingReport(const QByteArray &payload, PcbaTimingReport &report)
{
    constexpr qsizetype legacyEntrySize = 12;
    constexpr qsizetype extendedEntrySize = 16;
    constexpr qsizetype rawEntrySize = 40;
    constexpr qsizetype rawOffset = 16;
    constexpr qsizetype rawMax = 24;

    if (payload.size() < 4) {
        return false;
    }

    const uint8_t count = static_cast<uint8_t>(payload[2]);
    qsizetype actualEntrySize = rawEntrySize;
    if (payload.size() < 4 + (count * actualEntrySize)) {
        actualEntrySize = extendedEntrySize;
    }
    if (payload.size() < 4 + (count * actualEntrySize)) {
        actualEntrySize = legacyEntrySize;
    }
    if (payload.size() < 4 + (count * actualEntrySize) ||
        count > static_cast<uint8_t>(report.entries.size())) {
        return false;
    }

    PcbaTimingReport parsed;
    parsed.running = static_cast<uint8_t>(payload[0]) != 0;
    parsed.done = static_cast<uint8_t>(payload[1]) != 0;
    parsed.count = count;
    parsed.finalPass = static_cast<uint8_t>(payload[3]) != 0;

    for (uint8_t i = 0; i < count; ++i) {
        const qsizetype base = 4 + (i * actualEntrySize);
        auto &entry = parsed.entries[i];
        entry.kind = static_cast<uint8_t>(payload[base + 0]);
        entry.command = static_cast<uint8_t>(payload[base + 1]);
        entry.ok = static_cast<uint8_t>(payload[base + 2]) != 0;
        entry.responseCommandOrByte = static_cast<uint8_t>(payload[base + 3]);
        entry.responseChannel = static_cast<uint8_t>(payload[base + 4]);
        entry.responseLength = static_cast<uint8_t>(payload[base + 5]);
        entry.responseData[0] = static_cast<uint8_t>(payload[base + 6]);
        entry.responseData[1] = actualEntrySize >= extendedEntrySize ? static_cast<uint8_t>(payload[base + 7]) : 0;
        entry.responseData[2] = actualEntrySize >= extendedEntrySize ? static_cast<uint8_t>(payload[base + 8]) : 0;
        entry.responseData[3] = actualEntrySize >= extendedEntrySize ? static_cast<uint8_t>(payload[base + 9]) : 0;
        entry.elapsedUs = actualEntrySize >= extendedEntrySize ? getU32(payload, base + 12) : getU32(payload, base + 8);
        if (actualEntrySize >= rawEntrySize) {
            entry.rawResponseLength = static_cast<uint8_t>(payload[base + 10]);
            if (entry.rawResponseLength > entry.rawResponse.size()) {
                entry.rawResponseLength = static_cast<uint8_t>(entry.rawResponse.size());
            }
            for (uint8_t rawIndex = 0; rawIndex < entry.rawResponseLength && rawIndex < rawMax; ++rawIndex) {
                entry.rawResponse[rawIndex] = static_cast<uint8_t>(payload[base + rawOffset + rawIndex]);
            }
        }
    }

    report = parsed;
    return true;
}

bool parseSingleTankPcbaReport(const QByteArray &payload, SingleTankPcbaReport &report)
{
    constexpr qsizetype headerSize = 12;
    constexpr qsizetype legacyEntrySize = 24;
    constexpr qsizetype rawEntrySize = 52;
    constexpr qsizetype rawOffset = 28;
    constexpr qsizetype rawMax = 24;

    if (payload.size() < headerSize) {
        return false;
    }

    const uint8_t count = static_cast<uint8_t>(payload[2]);
    qsizetype actualEntrySize = rawEntrySize;
    if (payload.size() < headerSize + (count * actualEntrySize)) {
        actualEntrySize = legacyEntrySize;
    }
    if (count > static_cast<uint8_t>(report.entries.size()) ||
        payload.size() < headerSize + (count * actualEntrySize)) {
        return false;
    }

    SingleTankPcbaReport parsed;
    parsed.running = static_cast<uint8_t>(payload[0]) != 0;
    parsed.done = static_cast<uint8_t>(payload[1]) != 0;
    parsed.count = count;
    parsed.finalPass = static_cast<uint8_t>(payload[3]) != 0;
    parsed.standbyCurrentUaX100 = getU32(payload, 4);
    parsed.workCurrentUaX100 = getU32(payload, 8);

    for (uint8_t i = 0; i < count; ++i) {
        const qsizetype base = headerSize + (i * actualEntrySize);
        auto &entry = parsed.entries[i];
        entry.kind = static_cast<uint8_t>(payload[base + 0]);
        entry.command = static_cast<uint8_t>(payload[base + 1]);
        entry.ok = static_cast<uint8_t>(payload[base + 2]) != 0;
        entry.flags = static_cast<uint8_t>(payload[base + 3]);
        entry.responseCommandOrByte = static_cast<uint8_t>(payload[base + 4]);
        entry.responseChannel = static_cast<uint8_t>(payload[base + 5]);
        entry.responseLength = static_cast<uint8_t>(payload[base + 6]);
        entry.responseData[0] = static_cast<uint8_t>(payload[base + 7]);
        entry.responseData[1] = static_cast<uint8_t>(payload[base + 8]);
        entry.responseData[2] = static_cast<uint8_t>(payload[base + 9]);
        entry.responseData[3] = static_cast<uint8_t>(payload[base + 10]);
        entry.currentUaX100 = getU32(payload, base + 12);
        entry.elapsedUs = getU32(payload, base + 16);
        entry.parsedValue = getU32(payload, base + 20);
        if (entry.kind == 1u && entry.command == 0x11u) {
            entry.comparisonPressure001mmHg = entry.currentUaX100;
        }
        if (entry.kind == 4u) {
            entry.trendSampleCount = entry.responseCommandOrByte;
            entry.trendSlope001mmHgPerSecond = getS32(payload, base + 12);
            entry.trendMaxResidual001mmHg = getU32(payload, base + 7);
            entry.trendObservationUs = entry.elapsedUs;
            entry.trendPredictedPressure001mmHg = entry.parsedValue;
        }
        if (actualEntrySize >= rawEntrySize) {
            entry.rawResponseLength = static_cast<uint8_t>(payload[base + 24]);
            if (entry.rawResponseLength > entry.rawResponse.size()) {
                entry.rawResponseLength = static_cast<uint8_t>(entry.rawResponse.size());
            }
            for (uint8_t rawIndex = 0; rawIndex < entry.rawResponseLength && rawIndex < rawMax; ++rawIndex) {
                entry.rawResponse[rawIndex] = static_cast<uint8_t>(payload[base + rawOffset + rawIndex]);
            }
        }
    }

    report = parsed;
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
