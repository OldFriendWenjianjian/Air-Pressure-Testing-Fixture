#pragma once

#include "PressureFixtureModel.h"

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace fixture::usb {

constexpr uint8_t kHead0 = 0xA5;
constexpr uint8_t kHead1 = 0x5A;
constexpr uint8_t kVersion = 0x01;
constexpr qsizetype kMaxPayload = 1024;
constexpr qsizetype kMinFrameSize = 11;

enum FrameType : uint8_t {
    Request = 0x01,
    Response = 0x02,
    Report = 0x03,
};

enum Command : uint8_t {
    Hello = 0x01,
    GetStatus = 0x02,
    Start = 0x03,
    Stop = 0x04,
    Pause = 0x05,
    Resume = 0x06,
    SetState = 0x07,
    SetThreshold = 0x08,
    ManualValve = 0x09,
    EnterMscReboot = 0x0A,
    SetRtcTime = 0x0B,
    SetPcbaCurrentRange = 0x0C,
    CalibrateAdc = 0x0D,
    SetValveMask = 0x0E,
    SingleTankLoop = 0x0F,
    RunPcbaTiming = 0x10,
    GetPcbaTiming = 0x11,
    RunSingleTankPcba = 0x12,
    GetSingleTankPcba = 0x13,
    SetPcbaSupplyVoltage = 0x14,
    StatusSnapshot = 0x7E,
    Ack = 0x7F,
    Nak = 0x80,
};

struct Frame {
    uint8_t version = kVersion;
    uint8_t type = 0;
    uint16_t sequence = 0;
    uint8_t command = 0;
    QByteArray payload;
};

struct ParsedFrame {
    bool ok = false;
    bool needMore = false;
    qsizetype consumed = 0;
    QString error;
    Frame frame;
};

uint16_t crc16Modbus(const QByteArray &bytes);
QByteArray buildFrame(uint8_t type, uint16_t sequence, uint8_t command, const QByteArray &payload = {});
ParsedFrame parseOne(const QByteArray &buffer);
QByteArray buildHello(uint16_t sequence);
QByteArray buildStart(uint16_t sequence, uint16_t targetMmHg = 285);
QByteArray buildSetState(uint16_t sequence, RuntimeState state);
QByteArray buildSetThreshold(uint16_t sequence, double thresholdMmHg);
QByteArray buildManualValve(uint16_t sequence, uint8_t valveNumber, bool open);
QByteArray buildSetValveMask(uint16_t sequence, uint32_t valveMask, uint32_t openMask);
QByteArray buildSingleTankLoop(uint16_t sequence,
                               uint8_t tankIndex,
                               double targetMmHg,
                               double toleranceMmHg,
                               bool enable);
QByteArray buildEnterMscReboot(uint16_t sequence);
QByteArray buildSetRtcTime(uint16_t sequence, uint32_t epochSeconds);
QByteArray buildSetPcbaCurrentRange(uint16_t sequence, bool enable50mA);
QByteArray buildSetPcbaSupplyVoltage(uint16_t sequence, bool enable5V);
QByteArray buildCalibrateAdc(uint16_t sequence);
QByteArray buildRunPcbaTiming(uint16_t sequence, bool stopOnFail);
QByteArray buildGetPcbaTiming(uint16_t sequence);
QByteArray buildRunSingleTankPcba(uint16_t sequence);
QByteArray buildGetSingleTankPcba(uint16_t sequence);
bool applyStatusSnapshot(const QByteArray &payload, FixtureSnapshot &snapshot);
bool parsePcbaTimingReport(const QByteArray &payload, PcbaTimingReport &report);
bool parseSingleTankPcbaReport(const QByteArray &payload, SingleTankPcbaReport &report);
QString frameSummary(const Frame &frame);

} // namespace fixture::usb
