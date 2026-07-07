#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

using JlinkOpenFn = const char *(*)();
using JlinkCloseFn = void (*)();
using JlinkTifSelectFn = int (*)(int);
using JlinkSetSpeedFn = void (*)(int);
using JlinkExecCommandFn = int (*)(const char *, char *, int);
using JlinkConnectFn = int (*)();
using JlinkIsConnectedFn = char (*)();
using JlinkHaltFn = unsigned char (*)();
using JlinkGoFn = int (*)();
using JlinkIsHaltedFn = int (*)();
using JlinkReadMemFn = int (*)(uint32_t, uint32_t, void *);

namespace {

constexpr char kDllPath[] = "C:\\Program Files\\SEGGER\\JLink\\JLink_x64.dll";
constexpr uint32_t kRttControlBlockAddress = 0x20000070u;
constexpr uint32_t kTickAddress = 0x200026DCu;
constexpr int kJlinkTifSwd = 1;
constexpr int kJlinkSpeedKhz = 100;

void run_cmd(JlinkExecCommandFn fn, const char *cmd) {
    char buffer[256] = {};
    const int rc = fn ? fn(cmd, buffer, sizeof(buffer)) : -1;
    std::printf("ExecCommand('%s') rc=%d msg='%s'\n", cmd, rc, buffer);
}

void dump_bytes(const char *label, const unsigned char *data, size_t len) {
    std::printf("%s", label);
    for (size_t i = 0; i < len; ++i) {
        std::printf("%s%02X", i == 0 ? "" : " ", data[i]);
    }
    std::printf("\n");
}

void read_probe(JlinkReadMemFn readMem, uint32_t address, uint32_t size, const char *label) {
    unsigned char buffer[32];
    std::memset(buffer, 0xAA, sizeof(buffer));
    const int rc = readMem ? readMem(address, size, buffer) : -999;
    std::printf("%s rc=%d\n", label, rc);
    dump_bytes("  data=", buffer, size);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
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
    auto jlinkHalt = reinterpret_cast<JlinkHaltFn>(GetProcAddress(dll, "JLINKARM_Halt"));
    auto jlinkGo = reinterpret_cast<JlinkGoFn>(GetProcAddress(dll, "JLINKARM_Go"));
    auto jlinkIsHalted = reinterpret_cast<JlinkIsHaltedFn>(GetProcAddress(dll, "JLINKARM_IsHalted"));
    auto jlinkReadMem = reinterpret_cast<JlinkReadMemFn>(GetProcAddress(dll, "JLINKARM_ReadMem"));

    if (!jlinkOpen || !jlinkClose || !jlinkTifSelect || !jlinkSetSpeed ||
        !jlinkExecCommand || !jlinkConnect || !jlinkIsConnected ||
        !jlinkHalt || !jlinkGo || !jlinkIsHalted || !jlinkReadMem) {
        std::printf("Required exports missing\n");
        return 2;
    }

    run_cmd(jlinkExecCommand, "SuppressGUI");
    run_cmd(jlinkExecCommand, "SuppressGUI 1");
    std::printf("Open err='%s'\n", jlinkOpen());
    run_cmd(jlinkExecCommand, "Device = STM32F103ZE");
    run_cmd(jlinkExecCommand, "SetRTTAddr 0x20000070");
    const int tifRc = jlinkTifSelect(kJlinkTifSwd);
    std::printf("TIF_Select rc=%d\n", tifRc);
    jlinkSetSpeed(kJlinkSpeedKhz);
    const int connectRc = jlinkConnect();
    std::printf("Connect rc=%d isConnected=%d isHalted=%d\n",
                connectRc,
                static_cast<int>(jlinkIsConnected()),
                jlinkIsHalted());

    read_probe(jlinkReadMem, kRttControlBlockAddress, 32u, "Initial RTT CB");
    read_probe(jlinkReadMem, kTickAddress, 4u, "Initial Tick");

    const DWORD delaysMs[] = {0u, 1u, 2u, 5u, 10u, 20u, 50u};
    for (DWORD delayMs : delaysMs) {
        std::printf("=== Delay %lu ms ===\n", static_cast<unsigned long>(delayMs));
        const int goRc = jlinkGo();
        std::printf("Go rc=%d isHaltedAfterGo=%d\n", goRc, jlinkIsHalted());
        if (delayMs > 0u) {
            Sleep(delayMs);
        }
        const unsigned char haltRc = jlinkHalt();
        const int haltedState = jlinkIsHalted();
        std::printf("Halt rc=%u haltedState=%d\n",
                    static_cast<unsigned>(haltRc),
                    haltedState);
        read_probe(jlinkReadMem, kRttControlBlockAddress, 32u, "RTT CB after halt");
        read_probe(jlinkReadMem, kTickAddress, 4u, "Tick after halt");
    }

    jlinkClose();
    return 0;
}
