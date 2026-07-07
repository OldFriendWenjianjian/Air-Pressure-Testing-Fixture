#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>

using JlinkOpenFn = const char *(*)();
using JlinkCloseFn = void (*)();
using JlinkTifSelectFn = int (*)(int);
using JlinkSetSpeedFn = void (*)(int);
using JlinkExecCommandFn = int (*)(const char *, char *, int);
using JlinkConnectFn = int (*)();
using JlinkIsConnectedFn = char (*)();
using JlinkGoFn = int (*)();
using JlinkIsHaltedFn = int (*)();
using JlinkRttControlFn = int (*)(int, void *);
using JlinkRttReadFn = int (*)(unsigned, char *, unsigned);
using JlinkRttWriteFn = int (*)(unsigned, const char *, unsigned);

namespace {

constexpr char kDllPath[] = "C:\\Program Files\\SEGGER\\JLink\\JLink_x64.dll";
constexpr char kDeviceName[] = "STM32F103ZE";
constexpr uint32_t kRttControlBlockAddress = 0x20000070u;
constexpr int kJlinkTifSwd = 1;
constexpr int kJlinkSpeedKhz = 100;
constexpr int kJlinkRttTerminalStart = 0;
constexpr int kJlinkRttTerminalStop = 1;
constexpr int kJlinkRttTerminalStatus = 4;
constexpr unsigned kRttChannelIndex = 0u;

struct JlinkRttStartInfo {
    uint32_t configBlockAddress = 0;
    uint32_t reserved[3] = {};
};

struct JlinkRttStatusInfo {
    uint32_t numBytesTransferred = 0;
    uint32_t numBytesRead = 0;
    int hostOverflowCount = 0;
    int isRunning = 0;
    int numUpBuffers = 0;
    int numDownBuffers = 0;
    uint32_t reserved[2] = {};
};

void run_cmd(JlinkExecCommandFn fn, const char *cmd) {
    char buffer[256] = {};
    const int rc = fn ? fn(cmd, buffer, sizeof(buffer)) : -1;
    std::printf("ExecCommand('%s') rc=%d msg='%s'\n", cmd, rc, buffer);
}

void dump_ascii(const char *prefix, const char *data, int len) {
    std::printf("%s", prefix);
    for (int i = 0; i < len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        if (ch >= 32u && ch <= 126u) {
            std::printf("%c", ch);
        } else if (ch == '\r') {
            std::printf("<CR>");
        } else if (ch == '\n') {
            std::printf("<LF>");
        } else {
            std::printf("<%02X>", ch);
        }
    }
    std::printf("\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("Loading %s\n", kDllPath);
    HMODULE dll = LoadLibraryA(kDllPath);
    if (dll == nullptr) {
        std::printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    auto jlinkOpen = reinterpret_cast<JlinkOpenFn>(GetProcAddress(dll, "JLINKARM_Open"));
    auto jlinkClose = reinterpret_cast<JlinkCloseFn>(GetProcAddress(dll, "JLINKARM_Close"));
    auto jlinkTifSelect = reinterpret_cast<JlinkTifSelectFn>(GetProcAddress(dll, "JLINKARM_TIF_Select"));
    auto jlinkSetSpeed = reinterpret_cast<JlinkSetSpeedFn>(GetProcAddress(dll, "JLINKARM_SetSpeed"));
    auto jlinkExecCommand = reinterpret_cast<JlinkExecCommandFn>(GetProcAddress(dll, "JLINKARM_ExecCommand"));
    auto jlinkConnect = reinterpret_cast<JlinkConnectFn>(GetProcAddress(dll, "JLINKARM_Connect"));
    auto jlinkIsConnected = reinterpret_cast<JlinkIsConnectedFn>(GetProcAddress(dll, "JLINKARM_IsConnected"));
    auto jlinkGo = reinterpret_cast<JlinkGoFn>(GetProcAddress(dll, "JLINKARM_Go"));
    auto jlinkIsHalted = reinterpret_cast<JlinkIsHaltedFn>(GetProcAddress(dll, "JLINKARM_IsHalted"));
    auto rttControl = reinterpret_cast<JlinkRttControlFn>(GetProcAddress(dll, "JLINK_RTTERMINAL_Control"));
    auto rttRead = reinterpret_cast<JlinkRttReadFn>(GetProcAddress(dll, "JLINK_RTTERMINAL_Read"));
    auto rttWrite = reinterpret_cast<JlinkRttWriteFn>(GetProcAddress(dll, "JLINK_RTTERMINAL_Write"));

    std::printf("Fns open=%p connect=%p go=%p isHalted=%p rttCtl=%p rttRead=%p rttWrite=%p\n",
                reinterpret_cast<void *>(jlinkOpen),
                reinterpret_cast<void *>(jlinkConnect),
                reinterpret_cast<void *>(jlinkGo),
                reinterpret_cast<void *>(jlinkIsHalted),
                reinterpret_cast<void *>(rttControl),
                reinterpret_cast<void *>(rttRead),
                reinterpret_cast<void *>(rttWrite));
    if (!jlinkOpen || !jlinkClose || !jlinkTifSelect || !jlinkSetSpeed ||
        !jlinkExecCommand || !jlinkConnect || !jlinkIsConnected ||
        !jlinkGo || !jlinkIsHalted || !rttControl || !rttRead || !rttWrite) {
        std::printf("Required exports missing\n");
        return 2;
    }

    run_cmd(jlinkExecCommand, "SuppressGUI");
    run_cmd(jlinkExecCommand, "SuppressGUI 1");
    const char *openErr = jlinkOpen();
    std::printf("Open err='%s'\n", openErr ? openErr : "");
    run_cmd(jlinkExecCommand, "Device = STM32F103ZE");
    run_cmd(jlinkExecCommand, "SetRTTSearchRanges 0x20000000 0x10000");
    run_cmd(jlinkExecCommand, "SetRTTAddr 0x20000070");

    const int tifRc = jlinkTifSelect(kJlinkTifSwd);
    std::printf("TIF_Select rc=%d\n", tifRc);
    jlinkSetSpeed(kJlinkSpeedKhz);
    const int connectRc = jlinkConnect();
    std::printf("Connect rc=%d isConnected=%d isHalted=%d\n",
                connectRc,
                static_cast<int>(jlinkIsConnected()),
                jlinkIsHalted());
    const int goRc = jlinkGo();
    std::printf("Go rc=%d isHaltedAfterGo=%d\n", goRc, jlinkIsHalted());
    Sleep(300);

    JlinkRttStartInfo startInfo{};
    startInfo.configBlockAddress = kRttControlBlockAddress;
    const int startRc = rttControl(kJlinkRttTerminalStart, &startInfo);
    std::printf("RTTERMINAL start rc=%d\n", startRc);

    JlinkRttStatusInfo status{};
    const int statusRc = rttControl(kJlinkRttTerminalStatus, &status);
    std::printf("RTTERMINAL status rc=%d running=%d up=%d down=%d tx=%u rx=%u ovf=%d\n",
                statusRc,
                status.isRunning,
                status.numUpBuffers,
                status.numDownBuffers,
                status.numBytesTransferred,
                status.numBytesRead,
                status.hostOverflowCount);

    char buffer[1024];
    for (int attempt = 0; attempt < 15; ++attempt) {
        std::memset(buffer, 0, sizeof(buffer));
        const int readRc = rttRead(kRttChannelIndex, buffer, sizeof(buffer));
        std::printf("RTT read attempt %d rc=%d halted=%d\n",
                    attempt + 1,
                    readRc,
                    jlinkIsHalted());
        if (readRc > 0) {
            dump_ascii("RTT ascii=", buffer, readRc);
        }
        Sleep(200);
    }

    const char *helloHexLine = "A55A010101000102000100EB84\n";
    const int writeRc = rttWrite(kRttChannelIndex,
                                 helloHexLine,
                                 static_cast<unsigned>(std::strlen(helloHexLine)));
    std::printf("RTT write rc=%d\n", writeRc);
    Sleep(200);

    for (int attempt = 0; attempt < 20; ++attempt) {
        std::memset(buffer, 0, sizeof(buffer));
        const int readRc = rttRead(kRttChannelIndex, buffer, sizeof(buffer));
        std::printf("RTT post-write read attempt %d rc=%d halted=%d\n",
                    attempt + 1,
                    readRc,
                    jlinkIsHalted());
        if (readRc > 0) {
            dump_ascii("RTT ascii=", buffer, readRc);
        }
        Sleep(200);
    }

    const int stopRc = rttControl(kJlinkRttTerminalStop, nullptr);
    std::printf("RTTERMINAL stop rc=%d\n", stopRc);
    jlinkClose();
    return 0;
}
