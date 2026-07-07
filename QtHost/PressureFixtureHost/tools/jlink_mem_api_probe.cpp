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
using JlinkReadMemU32Fn = int (*)(uint32_t, uint32_t, uint32_t *, void *);
using JlinkReadMemU8Fn = int (*)(uint32_t, uint32_t, void *, void *);

namespace {

constexpr char kDllPath[] = "C:\\Program Files\\SEGGER\\JLink\\JLink_x64.dll";
constexpr uint32_t kRttControlBlockAddress = 0x20000070u;
constexpr uint32_t kUpDescriptorAddress = 0x20000088u;
constexpr uint32_t kDownDescriptorAddress = 0x200000D0u;
constexpr uint32_t kTickAddress = 0x200026DCu;
constexpr uint32_t kProbeBlockSize = 32u;
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

void probe_read_mem(JlinkReadMemFn fn, uint32_t address, uint32_t size, const char *label) {
    unsigned char buffer[64];
    std::memset(buffer, 0xAA, sizeof(buffer));
    const int rc = fn ? fn(address, size, buffer) : -999;
    std::printf("%s ReadMem rc=%d\n", label, rc);
    dump_bytes("  data=", buffer, size);
}

void probe_read_mem_u8(JlinkReadMemU8Fn fn, uint32_t address, uint32_t size, const char *label) {
    unsigned char buffer[64];
    std::memset(buffer, 0xAA, sizeof(buffer));
    const int rc = fn ? fn(address, size, buffer, nullptr) : -999;
    std::printf("%s ReadMemU8 rc=%d\n", label, rc);
    dump_bytes("  data=", buffer, size);
}

void probe_read_mem_u32(JlinkReadMemU32Fn fn, uint32_t address, uint32_t words, const char *label) {
    uint32_t buffer[16];
    std::memset(buffer, 0xAA, sizeof(buffer));
    const int rc = fn ? fn(address, words, buffer, nullptr) : -999;
    std::printf("%s ReadMemU32 rc=%d\n", label, rc);
    dump_bytes("  data=", reinterpret_cast<const unsigned char *>(buffer), words * sizeof(uint32_t));
}

void probe_all(JlinkReadMemFn readMem,
               JlinkReadMemU8Fn readMemU8,
               JlinkReadMemU32Fn readMemU32,
               const char *phase) {
    std::printf("=== %s ===\n", phase);
    probe_read_mem(readMem, kRttControlBlockAddress, kProbeBlockSize, "RTT CB");
    probe_read_mem_u8(readMemU8, kRttControlBlockAddress, kProbeBlockSize, "RTT CB");
    probe_read_mem_u32(readMemU32, kRttControlBlockAddress, kProbeBlockSize / 4u, "RTT CB");

    probe_read_mem(readMem, kUpDescriptorAddress, 24u, "UP DESC");
    probe_read_mem_u8(readMemU8, kUpDescriptorAddress, 24u, "UP DESC");
    probe_read_mem_u32(readMemU32, kUpDescriptorAddress, 6u, "UP DESC");

    probe_read_mem(readMem, kDownDescriptorAddress, 24u, "DOWN DESC");
    probe_read_mem_u8(readMemU8, kDownDescriptorAddress, 24u, "DOWN DESC");
    probe_read_mem_u32(readMemU32, kDownDescriptorAddress, 6u, "DOWN DESC");

    probe_read_mem(readMem, kTickAddress, 4u, "TICK");
    probe_read_mem_u8(readMemU8, kTickAddress, 4u, "TICK");
    probe_read_mem_u32(readMemU32, kTickAddress, 1u, "TICK");
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
    auto jlinkHalt = reinterpret_cast<JlinkHaltFn>(GetProcAddress(dll, "JLINKARM_Halt"));
    auto jlinkGo = reinterpret_cast<JlinkGoFn>(GetProcAddress(dll, "JLINKARM_Go"));
    auto jlinkIsHalted = reinterpret_cast<JlinkIsHaltedFn>(GetProcAddress(dll, "JLINKARM_IsHalted"));
    auto jlinkReadMem = reinterpret_cast<JlinkReadMemFn>(GetProcAddress(dll, "JLINKARM_ReadMem"));
    auto jlinkReadMemU32 = reinterpret_cast<JlinkReadMemU32Fn>(GetProcAddress(dll, "JLINKARM_ReadMemU32"));
    auto jlinkReadMemU8 = reinterpret_cast<JlinkReadMemU8Fn>(GetProcAddress(dll, "JLINKARM_ReadMemU8"));

    std::printf("Fns read=%p readU8=%p readU32=%p halt=%p go=%p isHalted=%p\n",
                reinterpret_cast<void *>(jlinkReadMem),
                reinterpret_cast<void *>(jlinkReadMemU8),
                reinterpret_cast<void *>(jlinkReadMemU32),
                reinterpret_cast<void *>(jlinkHalt),
                reinterpret_cast<void *>(jlinkGo),
                reinterpret_cast<void *>(jlinkIsHalted));
    if (!jlinkOpen || !jlinkClose || !jlinkTifSelect || !jlinkSetSpeed ||
        !jlinkExecCommand || !jlinkConnect || !jlinkIsConnected ||
        !jlinkHalt || !jlinkGo || !jlinkIsHalted ||
        !jlinkReadMem || !jlinkReadMemU8 || !jlinkReadMemU32) {
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

    probe_all(jlinkReadMem, jlinkReadMemU8, jlinkReadMemU32, "HALTED/AFTER CONNECT");

    const int goRc = jlinkGo();
    std::printf("Go rc=%d isHaltedAfterGo=%d\n", goRc, jlinkIsHalted());
    Sleep(1000);
    probe_all(jlinkReadMem, jlinkReadMemU8, jlinkReadMemU32, "RUNNING/AFTER GO");

    const unsigned char haltRc = jlinkHalt();
    std::printf("Halt after Go rc=%u isHaltedAfterHalt=%d\n",
                static_cast<unsigned>(haltRc),
                jlinkIsHalted());
    Sleep(200);
    probe_all(jlinkReadMem, jlinkReadMemU8, jlinkReadMemU32, "AFTER HALT AGAIN");

    jlinkClose();
    return 0;
}
