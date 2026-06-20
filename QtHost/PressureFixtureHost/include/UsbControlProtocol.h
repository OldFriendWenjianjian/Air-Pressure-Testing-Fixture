#pragma once

#include "PressureFixtureModel.h"

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace fixture::usb {

constexpr uint8_t kHead0 = 0xA5;
constexpr uint8_t kHead1 = 0x5A;
constexpr uint8_t kVersion = 0x01;
constexpr qsizetype kMaxPayload = 256;
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
QByteArray buildEnterMscReboot(uint16_t sequence);
QByteArray buildSetRtcTime(uint16_t sequence, uint32_t epochSeconds);
QByteArray buildSetPcbaCurrentRange(uint16_t sequence, bool enable50mA);
QByteArray buildCalibrateAdc(uint16_t sequence);
bool applyStatusSnapshot(const QByteArray &payload, FixtureSnapshot &snapshot);
QString frameSummary(const Frame &frame);

} // namespace fixture::usb
