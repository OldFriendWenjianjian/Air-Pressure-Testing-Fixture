#pragma once

#include <array>
#include <cstdint>
#include <QString>
#include <QStringList>

namespace fixture {

constexpr int kTankCount = 6;
constexpr int kChannelCount = 8;
constexpr int kValveCount = 26;
constexpr int kPressureSensorCount = 14;
constexpr int kPressureScale = 1000;

enum class RuntimeState : uint8_t {
    UsbMsc = 0,
    InitTanks,
    AutoAirtightness,
    Ready,
    PcbaPowerOn,
    PcbaStandbyCurrentCheck,
    PcbaWake,
    PcbaWorkCurrentMeasure,
    PcbaSetTestMode,
    PcbaZero,
    Switch45V,
    LowPowerQuery,
    Switch5V,
    NormalPowerQuery,
    Cal50,
    Cal150,
    Cal250,
    Test100,
    Test200,
    Test285,
    Result,
    Refill,
    Error,
    PcbaCurrentTest,
    RtcDebug,
    Count
};

enum class LinkMode : uint8_t {
    UsbCdc,
    Disconnected
};

struct TankSpec {
    QString name;
    int targetMmHg = 0;
    int inletValve = 0;
    int outletValve = 0;
    int reliefValve = 0;
    int pressureSensor = 0;
};

struct ChannelSpec {
    int channel = 0;
    int valve = 0;
    int pressureSensor = 0;
    QString uartName;
};

struct PcbaChannelStatus {
    bool online = false;
    bool lowPowerOk = false;
    bool normalPowerOk = false;
    bool pass = false;
    int pressure001mmHg = 0;
    int fixturePressure001mmHg = 0;
    int error001mmHg = 0;
    bool standbyCurrentValid = false;
    bool workCurrentValid = false;
    uint32_t standbyCurrentUaX100 = 0;
    uint32_t workCurrentUaX100 = 0;
    bool currentRawAdcValid = false;
    uint16_t currentRawAdc = 0;
};

struct FixtureSnapshot {
    RuntimeState state = RuntimeState::Ready;
    LinkMode linkMode = LinkMode::Disconnected;
    bool running = false;
    bool paused = false;
    bool remoteControlEnabled = false;
    uint32_t elapsedMs = 0;
    uint32_t sequence = 0;
    double thresholdMmHg = 3.0;
    std::array<bool, kValveCount + 1> valvesOpen{};
    std::array<int, kPressureSensorCount> pressure001mmHg{};
    std::array<bool, kPressureSensorCount> pressureValid{};
    std::array<PcbaChannelStatus, kChannelCount> channels{};
    bool rtcSnapshotValid = false;
    bool rtcInitialized = false;
    bool rtcOscillatorReady = false;
    bool rtcBackupValid = false;
    uint32_t rtcEpochSeconds = 0;
    bool pcbaCurrent50mAEnabled = false;
    bool adcReferenceValid = false;
    bool adcReferenceRangeError = false;
    uint16_t adcVrefintRaw = 0;
    uint16_t adcVddaMv = 3300;
    uint32_t adcScalePpm = 1000000;
};

const std::array<TankSpec, kTankCount> &tankSpecs();
const std::array<ChannelSpec, kChannelCount> &channelSpecs();
QString stateName(RuntimeState state);
QString stateDisplayName(RuntimeState state);
QStringList stateDisplayNames();
RuntimeState stateFromIndex(int index);
int stateIndex(RuntimeState state);
QString commandName(uint8_t command);
double toMmHg(int pressure001mmHg);
int to001mmHg(double mmHg);
bool pressureSensorValid(const FixtureSnapshot &snapshot, int sensorIndex);
QString formatPressure001mmHg(int pressure001mmHg, bool valid, int precision = 1, bool withUnit = false);
QString sensorPressureText(const FixtureSnapshot &snapshot, int sensorIndex, int precision = 1, bool withUnit = false);
QString formatCurrentUaX100(uint32_t currentUaX100, bool valid, int precision = 2, bool withUnit = false);
QString formatCurrentUaX100AsMa(uint32_t currentUaX100, bool valid, int precision = 3, bool withUnit = false);

} // namespace fixture
