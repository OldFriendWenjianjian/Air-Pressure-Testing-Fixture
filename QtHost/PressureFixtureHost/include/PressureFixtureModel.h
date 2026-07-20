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
constexpr int kCurrentSampleCount = 10;

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
    PcbaTimingDiagnostic,
    SingleTankPcbaDiagnostic,
    SingleTankLoop,
    SinglePcbaFlow,
    PcbaPressureQuery,
    PcbaWriteFlash,
    SensorCalibration,
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
    uint32_t standbyCurrentVarianceUa2 = 0;
    uint32_t workCurrentVarianceUa2 = 0;
    std::array<uint32_t, kCurrentSampleCount> standbyCurrentSamplesUaX100{};
    std::array<uint32_t, kCurrentSampleCount> workCurrentSamplesUaX100{};
    bool currentRawAdcValid = false;
    uint16_t currentRawAdc = 0;
};

struct FixtureSnapshot {
    RuntimeState state = RuntimeState::Ready;
    LinkMode linkMode = LinkMode::Disconnected;
    bool running = false;
    bool paused = false;
    bool remoteControlEnabled = false;
    bool singlePcbaFlowActive = false;
    uint32_t elapsedMs = 0;
    uint32_t sequence = 0;
    double thresholdMmHg = 3.0;
    std::array<bool, kValveCount + 1> valvesOpen{};
    std::array<int, kPressureSensorCount> pressure001mmHg{};
    std::array<bool, kPressureSensorCount> pressureValid{};
    std::array<bool, kPressureSensorCount> pressureFaultLatched{};
    std::array<uint8_t, kPressureSensorCount> pressureStatusByte{};
    std::array<uint8_t, kPressureSensorCount> pressureFaultCode{};
    std::array<uint16_t, kPressureSensorCount> pressureMathSaturationEventCount{};
    std::array<uint16_t, kPressureSensorCount> pressureMathSaturationAttemptCount{};
    std::array<uint16_t, kPressureSensorCount> pressureMathSaturationSuccessCount{};
    std::array<PcbaChannelStatus, kChannelCount> channels{};
    bool rtcSnapshotValid = false;
    bool rtcInitialized = false;
    bool rtcOscillatorReady = false;
    bool rtcBackupValid = false;
    uint32_t rtcEpochSeconds = 0;
    bool pcbaCurrent50mAEnabled = false;
    bool pcbaSupply5VEnabled = false;
    bool pcbaSupply45VEnabled = false;
    bool adcReferenceValid = false;
    bool adcReferenceRangeError = false;
    uint16_t adcVrefintRaw = 0;
    uint16_t adcVddaMv = 3300;
    uint32_t adcScalePpm = 1000000;
    bool singleTankProtectionActive = false;
    uint8_t singleTankProtectionReason = 0;
    int singleTankProtectionTankIndex = -1;
    int singleTankProtectionSensorIndex = -1;
    int singleTankProtectionInletValve = 0;
    bool pressureCalibrationStatusAvailable = false;
    uint16_t pressureCalibrationValidMask = 0;
    bool pressureCalibrationModeActive = false;
    uint8_t pressureCalibrationActuator = 0;
    uint8_t pressureCalibrationCapturedMask = 0;
    bool pressureCalibrationStorageFault = false;
};

struct SensorCalibrationStatus {
    uint8_t version = 0;
    bool active = false;
    bool sourceValid = false;
    bool sourceFault = false;
    bool storageLoaded = false;
    bool stagedComplete = false;
    bool zeroReady = false;
    bool autoVentActive = false;
    bool storageFault = false;
    uint8_t actuator = 0;
    bool inPlaceMode = false;
    uint8_t capturedMask = 0;
    uint16_t calibratedMask = 0;
    uint8_t selectedSlot = 0;
    uint8_t detail = 0;
    uint32_t liveRaw = 0;
    uint32_t liveNominal001mmHg = 0;
    std::array<uint32_t, 4> rawPoints{};
    std::array<uint32_t, 4> actual001mmHg{};
};

struct PcbaTimingEntry {
    uint8_t kind = 0;
    uint8_t command = 0;
    bool ok = false;
    uint8_t responseCommandOrByte = 0;
    uint8_t responseChannel = 0;
    uint8_t responseLength = 0;
    std::array<uint8_t, 4> responseData{};
    uint8_t rawResponseLength = 0;
    std::array<uint8_t, 24> rawResponse{};
    uint32_t elapsedUs = 0;
};

struct PcbaTimingReport {
    bool running = false;
    bool done = false;
    uint8_t count = 0;
    bool finalPass = false;
    std::array<PcbaTimingEntry, 10> entries{};
};

struct SingleTankPcbaEntry {
    uint8_t kind = 0;
    uint8_t command = 0;
    bool ok = false;
    uint8_t flags = 0;
    uint8_t responseCommandOrByte = 0;
    uint8_t responseChannel = 0;
    uint8_t responseLength = 0;
    std::array<uint8_t, 4> responseData{};
    uint8_t rawResponseLength = 0;
    std::array<uint8_t, 24> rawResponse{};
    uint32_t currentUaX100 = 0;
    uint32_t elapsedUs = 0;
    uint32_t parsedValue = 0;
    uint32_t comparisonPressure001mmHg = 0;
    uint8_t trendSampleCount = 0;
    int32_t trendSlope001mmHgPerSecond = 0;
    uint32_t trendMaxResidual001mmHg = 0;
    uint32_t trendObservationUs = 0;
    uint32_t trendPredictedPressure001mmHg = 0;
};

struct SingleTankPcbaReport {
    bool running = false;
    bool done = false;
    uint8_t count = 0;
    bool finalPass = false;
    uint32_t standbyCurrentUaX100 = 0;
    uint32_t workCurrentUaX100 = 0;
    std::array<SingleTankPcbaEntry, 23> entries{};
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
bool pressureSensorFaultLatched(const FixtureSnapshot &snapshot, int sensorIndex);
QString pressureSensorFaultReasonText(const FixtureSnapshot &snapshot, int sensorIndex);
QString formatPressure001mmHg(int pressure001mmHg, bool valid, int precision = 1, bool withUnit = false);
QString sensorPressureText(const FixtureSnapshot &snapshot, int sensorIndex, int precision = 1, bool withUnit = false);
QString formatCurrentUaX100(uint32_t currentUaX100, bool valid, int precision = 2, bool withUnit = false);
QString formatCurrentUaX100AsMa(uint32_t currentUaX100, bool valid, int precision = 3, bool withUnit = false);
QString formatVarianceUa2(uint32_t varianceUa2, bool valid, int precision = 2, bool withUnit = false);
QString formatVarianceUa2AsMa2(uint32_t varianceUa2, bool valid, int precision = 6, bool withUnit = false);

} // namespace fixture
