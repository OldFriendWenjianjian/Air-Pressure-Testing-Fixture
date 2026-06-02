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
    Count
};

enum class LinkMode : uint8_t {
    Simulation,
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
};

struct FixtureSnapshot {
    RuntimeState state = RuntimeState::Ready;
    LinkMode linkMode = LinkMode::Simulation;
    bool running = false;
    bool paused = false;
    bool remoteControlEnabled = false;
    uint32_t elapsedMs = 0;
    uint32_t sequence = 0;
    double thresholdMmHg = 3.0;
    std::array<bool, kValveCount + 1> valvesOpen{};
    std::array<int, kPressureSensorCount> pressure001mmHg{};
    std::array<PcbaChannelStatus, kChannelCount> channels{};
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
void applyStateOutputs(FixtureSnapshot &snapshot, RuntimeState state);

} // namespace fixture
