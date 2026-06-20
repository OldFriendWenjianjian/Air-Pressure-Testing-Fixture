#include "PressureFixtureModel.h"

#include <algorithm>
#include <cmath>

namespace fixture {

const std::array<TankSpec, kTankCount> &tankSpecs()
{
    static const std::array<TankSpec, kTankCount> specs{{
        {"标定气罐 50mmHg", 50, 1, 2, 21, 1},
        {"标定气罐 150mmHg", 150, 3, 4, 22, 2},
        {"标定气罐 250mmHg", 250, 5, 6, 23, 3},
        {"测试气罐 100mmHg", 100, 7, 8, 24, 4},
        {"测试气罐 200mmHg", 200, 9, 10, 25, 5},
        {"测试气罐 285mmHg", 285, 11, 12, 26, 6},
    }};
    return specs;
}

const std::array<ChannelSpec, kChannelCount> &channelSpecs()
{
    static const std::array<ChannelSpec, kChannelCount> specs{{
        {1, 13, 7, "UART1"},
        {2, 14, 8, "UART2"},
        {3, 15, 9, "UART3"},
        {4, 16, 10, "UART4"},
        {5, 17, 11, "UART5"},
        {6, 18, 12, "UART6"},
        {7, 19, 13, "UART7"},
        {8, 20, 14, "UART8"},
    }};
    return specs;
}

QString stateName(RuntimeState state)
{
    switch (state) {
    case RuntimeState::UsbMsc: return "USB MSC";
    case RuntimeState::InitTanks: return "Init tanks";
    case RuntimeState::AutoAirtightness: return "Auto airtightness";
    case RuntimeState::Ready: return "Ready";
    case RuntimeState::PcbaPowerOn: return "PCBA power on";
    case RuntimeState::PcbaStandbyCurrentCheck: return "Standby current";
    case RuntimeState::PcbaWake: return "Wake PCBA";
    case RuntimeState::PcbaWorkCurrentMeasure: return "Work current";
    case RuntimeState::PcbaSetTestMode: return "Set test mode";
    case RuntimeState::PcbaZero: return "Record zero";
    case RuntimeState::Switch45V: return "Switch 4.5V";
    case RuntimeState::LowPowerQuery: return "Low power query";
    case RuntimeState::Switch5V: return "Switch 5V";
    case RuntimeState::NormalPowerQuery: return "Normal power query";
    case RuntimeState::Cal50: return "Cal 50mmHg";
    case RuntimeState::Cal150: return "Cal 150mmHg";
    case RuntimeState::Cal250: return "Cal 250mmHg";
    case RuntimeState::Test100: return "Test 100mmHg";
    case RuntimeState::Test200: return "Test 200mmHg";
    case RuntimeState::Test285: return "Test 285mmHg";
    case RuntimeState::Result: return "Result";
    case RuntimeState::Refill: return "Refill";
    case RuntimeState::Error: return "Error";
    case RuntimeState::PcbaCurrentTest: return "PCBA current test";
    case RuntimeState::RtcDebug: return "RTC debug";
    case RuntimeState::Count: break;
    }
    return "Unknown";
}

QString stateDisplayName(RuntimeState state)
{
    switch (state) {
    case RuntimeState::UsbMsc: return "U盘维护模式";
    case RuntimeState::InitTanks: return "初始化罐体闭环";
    case RuntimeState::AutoAirtightness: return "自动气密性测试";
    case RuntimeState::Ready: return "Ready 等待压合";
    case RuntimeState::PcbaPowerOn: return "PCBA 上电";
    case RuntimeState::PcbaStandbyCurrentCheck: return "待机电流检测";
    case RuntimeState::PcbaWake: return "唤醒 PCBA";
    case RuntimeState::PcbaWorkCurrentMeasure: return "工作电流检测";
    case RuntimeState::PcbaSetTestMode: return "进入测试模式";
    case RuntimeState::PcbaZero: return "记录零点";
    case RuntimeState::Switch45V: return "切换 4.5V";
    case RuntimeState::LowPowerQuery: return "低电状态查询";
    case RuntimeState::Switch5V: return "切换 5V";
    case RuntimeState::NormalPowerQuery: return "正常电压查询";
    case RuntimeState::Cal50: return "50mmHg 标定";
    case RuntimeState::Cal150: return "150mmHg 标定";
    case RuntimeState::Cal250: return "250mmHg 标定";
    case RuntimeState::Test100: return "100mmHg 测试";
    case RuntimeState::Test200: return "200mmHg 测试";
    case RuntimeState::Test285: return "285mmHg 测试";
    case RuntimeState::Result: return "结果显示";
    case RuntimeState::Refill: return "补气阶段";
    case RuntimeState::Error: return "错误停机";
    case RuntimeState::PcbaCurrentTest: return "PCBA电流测试";
    case RuntimeState::RtcDebug: return "RTC时钟调试模式";
    case RuntimeState::Count: break;
    }
    return "未知状态";
}

QStringList stateDisplayNames()
{
    QStringList names;
    for (int i = 0; i < stateIndex(RuntimeState::Count); ++i) {
        names << stateDisplayName(stateFromIndex(i));
    }
    return names;
}

RuntimeState stateFromIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(RuntimeState::Count)) {
        return RuntimeState::Error;
    }
    return static_cast<RuntimeState>(index);
}

int stateIndex(RuntimeState state)
{
    return static_cast<int>(state);
}

QString commandName(uint8_t command)
{
    switch (command) {
    case 0x01: return "HELLO";
    case 0x02: return "GET_STATUS";
    case 0x03: return "START";
    case 0x04: return "STOP";
    case 0x05: return "PAUSE";
    case 0x06: return "RESUME";
    case 0x07: return "SET_STATE";
    case 0x08: return "SET_THRESHOLD";
    case 0x09: return "MANUAL_VALVE";
    case 0x0A: return "ENTER_MSC_REBOOT";
    case 0x0B: return "SET_RTC_TIME";
    case 0x0C: return "SET_PCBA_CURRENT_RANGE";
    case 0x0D: return "CALIBRATE_ADC";
    case 0x7E: return "STATUS";
    case 0x7F: return "ACK";
    case 0x80: return "NAK";
    default: return QString("0x%1").arg(command, 2, 16, QLatin1Char('0')).toUpper();
    }
}

double toMmHg(int pressure001mmHg)
{
    return pressure001mmHg / static_cast<double>(kPressureScale);
}

int to001mmHg(double mmHg)
{
    return static_cast<int>(std::lround(mmHg * kPressureScale));
}

bool pressureSensorValid(const FixtureSnapshot &snapshot, int sensorIndex)
{
    return sensorIndex >= 0 &&
           sensorIndex < kPressureSensorCount &&
           snapshot.pressureValid[sensorIndex];
}

QString formatPressure001mmHg(int pressure001mmHg, bool valid, int precision, bool withUnit)
{
    if (!valid) {
        return "--";
    }

    const QString value = QString::number(toMmHg(pressure001mmHg), 'f', precision);
    return withUnit ? value + " mmHg" : value;
}

QString sensorPressureText(const FixtureSnapshot &snapshot, int sensorIndex, int precision, bool withUnit)
{
    if (sensorIndex < 0 || sensorIndex >= kPressureSensorCount) {
        return "--";
    }
    return formatPressure001mmHg(snapshot.pressure001mmHg[sensorIndex],
                                 pressureSensorValid(snapshot, sensorIndex),
                                 precision,
                                 withUnit);
}

QString formatCurrentUaX100(uint32_t currentUaX100, bool valid, int precision, bool withUnit)
{
    if (!valid) {
        return "--";
    }

    const int clampedPrecision = std::clamp(precision, 0, 2);
    const uint32_t scale = clampedPrecision == 0 ? 100u : (clampedPrecision == 1 ? 10u : 1u);
    const uint32_t rounded = currentUaX100 + (scale / 2u);
    const double currentUa = (rounded / scale) * scale / 100.0;
    const QString value = QString::number(currentUa, 'f', clampedPrecision);
    return withUnit ? value + " uA" : value;
}

QString formatCurrentUaX100AsMa(uint32_t currentUaX100, bool valid, int precision, bool withUnit)
{
    if (!valid) {
        return "--";
    }

    const int clampedPrecision = std::clamp(precision, 0, 3);
    const double currentMa = currentUaX100 / 100000.0;
    const QString value = QString::number(currentMa, 'f', clampedPrecision);
    return withUnit ? value + " mA" : value;
}

} // namespace fixture
