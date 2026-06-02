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

static void openAllChannelValves(FixtureSnapshot &snapshot)
{
    for (const auto &channel : channelSpecs()) {
        snapshot.valvesOpen[channel.valve] = true;
    }
}

static int targetForState(RuntimeState state)
{
    switch (state) {
    case RuntimeState::Cal50: return 50;
    case RuntimeState::Cal150: return 150;
    case RuntimeState::Cal250: return 250;
    case RuntimeState::Test100: return 100;
    case RuntimeState::Test200: return 200;
    case RuntimeState::Test285: return 285;
    default: return 0;
    }
}

void applyStateOutputs(FixtureSnapshot &snapshot, RuntimeState state)
{
    snapshot.state = state;
    std::fill(snapshot.valvesOpen.begin(), snapshot.valvesOpen.end(), false);

    const auto &tanks = tankSpecs();
    for (int i = 0; i < kTankCount; ++i) {
        const int target = to001mmHg(tanks[i].targetMmHg);
        if (snapshot.pressure001mmHg[i] <= 0) {
            snapshot.pressure001mmHg[i] = target;
        }
    }

    switch (state) {
    case RuntimeState::InitTanks:
    case RuntimeState::Refill:
        for (const auto &tank : tanks) {
            snapshot.valvesOpen[tank.inletValve] = true;
        }
        break;
    case RuntimeState::Cal50:
        snapshot.valvesOpen[2] = true;
        openAllChannelValves(snapshot);
        break;
    case RuntimeState::Cal150:
        snapshot.valvesOpen[4] = true;
        openAllChannelValves(snapshot);
        break;
    case RuntimeState::Cal250:
        snapshot.valvesOpen[6] = true;
        openAllChannelValves(snapshot);
        break;
    case RuntimeState::Test100:
        snapshot.valvesOpen[8] = true;
        openAllChannelValves(snapshot);
        break;
    case RuntimeState::Test200:
        snapshot.valvesOpen[10] = true;
        openAllChannelValves(snapshot);
        break;
    case RuntimeState::Test285:
        snapshot.valvesOpen[12] = true;
        openAllChannelValves(snapshot);
        break;
    default:
        break;
    }

    const int stageTarget = targetForState(state);
    for (int i = 0; i < kChannelCount; ++i) {
        auto &channel = snapshot.channels[i];
        if (state >= RuntimeState::PcbaWake && state <= RuntimeState::Result) {
            channel.online = true;
        }
        if (state >= RuntimeState::LowPowerQuery) {
            channel.lowPowerOk = true;
        }
        if (state >= RuntimeState::NormalPowerQuery) {
            channel.normalPowerOk = true;
        }
        if (stageTarget > 0) {
            const int fixturePressure = to001mmHg(stageTarget);
            snapshot.pressure001mmHg[6 + i] = fixturePressure;
            channel.fixturePressure001mmHg = fixturePressure;
            if (state == RuntimeState::Test100 || state == RuntimeState::Test200 || state == RuntimeState::Test285) {
                const int offset = ((i % 3) - 1) * 700;
                channel.pressure001mmHg = fixturePressure + offset;
                channel.error001mmHg = channel.pressure001mmHg - fixturePressure;
                channel.pass = std::abs(channel.error001mmHg) <= to001mmHg(snapshot.thresholdMmHg);
            }
        } else if (state == RuntimeState::Ready || state == RuntimeState::AutoAirtightness || state == RuntimeState::Result) {
            snapshot.pressure001mmHg[6 + i] = 0;
            channel.fixturePressure001mmHg = 0;
        }
    }
}

} // namespace fixture
