#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

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
using JlinkWriteMemFn = int (*)(uint32_t, uint32_t, const void *);
using JlinkWriteU32Fn = int (*)(uint32_t, uint32_t);
using JlinkRttControlFn = int (*)(int, void *);
using JlinkRttReadFn = int (*)(unsigned, char *, unsigned);
using JlinkRttWriteFn = int (*)(unsigned, const char *, unsigned);

namespace {

constexpr uint32_t kRttControlBlockAddress = 0x20000070u;
constexpr uint32_t kFaultRecordAddress = 0x20000B18u;
constexpr uint32_t kTickAddress = 0x200026DCu;
constexpr unsigned kRttChannelIndex = 0u;
constexpr int kJlinkRttTerminalStart = 0;
constexpr int kJlinkRttTerminalStatus = 4;
constexpr uint32_t kSeggerRttMaxUpOffset = 16u;
constexpr uint32_t kSeggerRttMaxDownOffset = 20u;
constexpr uint32_t kSeggerRttDescriptorBaseOffset = 24u;
constexpr uint32_t kSeggerRttBufferDescriptorSize = 24u;
constexpr uint32_t kSeggerRttDescriptorWriteOffset = 12u;
constexpr uint32_t kSeggerRttDescriptorReadOffset = 16u;

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

struct RttControlBlockHead {
    char id[16];
    uint32_t maxUpBuffers;
    uint32_t maxDownBuffers;
};

struct RttBufferDescriptor {
    uint32_t nameAddress;
    uint32_t bufferAddress;
    uint32_t sizeOfBuffer;
    uint32_t writeOffset;
    uint32_t readOffset;
    uint32_t flags;
};

struct AppFaultRecord {
    uint32_t signature;
    uint32_t faultType;
    uint32_t stackedR0;
    uint32_t stackedR1;
    uint32_t stackedR2;
    uint32_t stackedR3;
    uint32_t stackedR12;
    uint32_t stackedLr;
    uint32_t stackedPc;
    uint32_t stackedPsr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t msp;
    uint32_t psp;
    uint32_t excReturn;
};

static void dump_bytes(const char *label, const uint8_t *data, size_t len) {
    std::printf("%s", label);
    for (size_t i = 0; i < len; ++i) {
        std::printf("%s%02X", i == 0 ? "" : " ", data[i]);
    }
    std::printf("\n");
}

static void sleep_ms(DWORD ms) {
    Sleep(ms);
}

static void dump_ascii(const char *label, const uint8_t *data, size_t len) {
    std::printf("%s", label);
    for (size_t i = 0; i < len; ++i) {
        const uint8_t ch = data[i];
        if (ch >= 32u && ch <= 126u) {
            std::printf("%c", static_cast<char>(ch));
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

static bool read_mem(JlinkReadMemFn fn, uint32_t address, void *data, uint32_t size) {
    if (!fn) {
        return false;
    }
    const int rc = fn(address, size, data);
    if (rc < 0) {
        return false;
    }
    if (rc == 0 || rc == static_cast<int>(size)) {
        return true;
    }
    if ((size % sizeof(uint32_t)) == 0u && rc == static_cast<int>(size / sizeof(uint32_t))) {
        return true;
    }
    return false;
}

static bool write_mem(JlinkWriteMemFn fn, uint32_t address, const void *data, uint32_t size) {
    if (!fn) {
        return false;
    }
    const int rc = fn(address, size, data);
    return rc == 0 || rc == static_cast<int>(size);
}

static bool write_u32(JlinkWriteU32Fn fn, uint32_t address, uint32_t value) {
    return fn && fn(address, value) >= 0;
}

static void dump_fault_record(const AppFaultRecord& fault) {
    std::printf("Fault signature=0x%08X type=%u pc=0x%08X lr=0x%08X cfsr=0x%08X hfsr=0x%08X mmfar=0x%08X bfar=0x%08X exc=0x%08X\n",
                fault.signature,
                fault.faultType,
                fault.stackedPc,
                fault.stackedLr,
                fault.cfsr,
                fault.hfsr,
                fault.mmfar,
                fault.bfar,
                fault.excReturn);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char *dllPath = "C:\\Program Files\\SEGGER\\JLink\\JLink_x64.dll";
    std::printf("Loading %s\n", dllPath);
    HMODULE dll = LoadLibraryA(dllPath);
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
    auto jlinkWriteMem = reinterpret_cast<JlinkWriteMemFn>(GetProcAddress(dll, "JLINKARM_WriteMem"));
    auto jlinkWriteU32 = reinterpret_cast<JlinkWriteU32Fn>(GetProcAddress(dll, "JLINKARM_WriteU32"));
    auto rttControl = reinterpret_cast<JlinkRttControlFn>(GetProcAddress(dll, "JLINK_RTTERMINAL_Control"));
    auto rttRead = reinterpret_cast<JlinkRttReadFn>(GetProcAddress(dll, "JLINK_RTTERMINAL_Read"));
    auto rttWrite = reinterpret_cast<JlinkRttWriteFn>(GetProcAddress(dll, "JLINK_RTTERMINAL_Write"));

    std::printf("Fns open=%p exec=%p connect=%p halt=%p go=%p isHalted=%p read=%p write=%p writeU32=%p rttCtl=%p rttRead=%p rttWrite=%p\n",
                reinterpret_cast<void *>(jlinkOpen),
                reinterpret_cast<void *>(jlinkExecCommand),
                reinterpret_cast<void *>(jlinkConnect),
                reinterpret_cast<void *>(jlinkHalt),
                reinterpret_cast<void *>(jlinkGo),
                reinterpret_cast<void *>(jlinkIsHalted),
                reinterpret_cast<void *>(jlinkReadMem),
                reinterpret_cast<void *>(jlinkWriteMem),
                reinterpret_cast<void *>(jlinkWriteU32),
                reinterpret_cast<void *>(rttControl),
                reinterpret_cast<void *>(rttRead),
                reinterpret_cast<void *>(rttWrite));
    if (!jlinkOpen || !jlinkClose || !jlinkTifSelect || !jlinkSetSpeed || !jlinkExecCommand || !jlinkConnect || !jlinkIsConnected || !jlinkReadMem || !jlinkGo || !jlinkIsHalted) {
        std::printf("Required exports missing\n");
        return 2;
    }

    char cmdBuf[256] = {};
    auto run_cmd = [&](const char *cmd) {
        std::memset(cmdBuf, 0, sizeof(cmdBuf));
        int rc = jlinkExecCommand(cmd, cmdBuf, sizeof(cmdBuf));
        std::printf("ExecCommand('%s') rc=%d msg='%s'\n", cmd, rc, cmdBuf);
    };

    run_cmd("SuppressGUI");
    run_cmd("SuppressGUI 1");

    const char *openErr = jlinkOpen();
    std::printf("Open err='%s'\n", openErr ? openErr : "");

    run_cmd("SuppressGUI");
    run_cmd("SuppressGUI 1");
    run_cmd("device = STM32F103ZE");
    run_cmd("SetRTTSearchRanges 0x20000000 0x10000");

    int rc = jlinkTifSelect(1);
    std::printf("TIF_Select rc=%d\n", rc);
    jlinkSetSpeed(100);
    rc = jlinkConnect();
    std::printf("Connect rc=%d isConnected=%d\n", rc, static_cast<int>(jlinkIsConnected()));
    sleep_ms(200);

    run_cmd("SetRTTAddr 0x20000070");
    uint8_t raw[64];
    std::memset(raw, 0xAA, sizeof(raw));
    rc = jlinkReadMem(kRttControlBlockAddress, static_cast<uint32_t>(sizeof(raw)), raw);
    std::printf("ReadMem rc=%d\n", rc);
    dump_bytes("ReadMem data=", raw, sizeof(raw));
    if (sizeof(raw) >= sizeof(RttControlBlockHead)) {
        const auto *head = reinterpret_cast<const RttControlBlockHead *>(raw);
        std::printf("ControlBlock id='%s' maxUp=%u maxDown=%u\n",
                    head->id,
                    head->maxUpBuffers,
                    head->maxDownBuffers);
    }

    uint32_t maxUp = 0;
    uint32_t maxDown = 0;
    read_mem(jlinkReadMem, kRttControlBlockAddress + kSeggerRttMaxUpOffset, &maxUp, sizeof(maxUp));
    read_mem(jlinkReadMem, kRttControlBlockAddress + kSeggerRttMaxDownOffset, &maxDown, sizeof(maxDown));
    std::printf("Descriptor counts up=%u down=%u\n", maxUp, maxDown);

    RttBufferDescriptor upDesc{};
    RttBufferDescriptor downDesc{};
    const uint32_t upDescAddr = kRttControlBlockAddress + kSeggerRttDescriptorBaseOffset;
    const uint32_t downDescAddr = upDescAddr + (maxUp * kSeggerRttBufferDescriptorSize);
    if (read_mem(jlinkReadMem, upDescAddr, &upDesc, sizeof(upDesc))) {
        std::printf("UpDesc buf=0x%08X size=%u wr=%u rd=%u flags=%u\n",
                    upDesc.bufferAddress, upDesc.sizeOfBuffer, upDesc.writeOffset, upDesc.readOffset, upDesc.flags);
    }
    if (read_mem(jlinkReadMem, downDescAddr, &downDesc, sizeof(downDesc))) {
        std::printf("DownDesc buf=0x%08X size=%u wr=%u rd=%u flags=%u\n",
                    downDesc.bufferAddress, downDesc.sizeOfBuffer, downDesc.writeOffset, downDesc.readOffset, downDesc.flags);
    }

    uint32_t tick = 0;
    if (read_mem(jlinkReadMem, kTickAddress, &tick, sizeof(tick))) {
        std::printf("Tick before Go=%u\n", tick);
    }

    AppFaultRecord fault{};
    if (read_mem(jlinkReadMem, kFaultRecordAddress, &fault, sizeof(fault))) {
        dump_fault_record(fault);
    }

    std::printf("Skipping RTTERMINAL start; probing raw RTT ring buffers directly.\n");

    std::printf("IsHalted before Go=%d\n", jlinkIsHalted());
    if (jlinkHalt) {
        const unsigned char haltRc = jlinkHalt();
        std::printf("Halt rc=%u IsHalted after Halt=%d\n",
                    static_cast<unsigned>(haltRc),
                    jlinkIsHalted());
    }
    rc = jlinkGo();
    std::printf("Go rc=%d\n", rc);
    sleep_ms(1000);
    std::printf("IsHalted after 1s=%d\n", jlinkIsHalted());
    if (jlinkHalt) {
        const unsigned char haltAfterGoRc = jlinkHalt();
        std::printf("Halt after Go rc=%u IsHalted after Halt2=%d\n",
                    static_cast<unsigned>(haltAfterGoRc),
                    jlinkIsHalted());
    }
    if (read_mem(jlinkReadMem, kTickAddress, &tick, sizeof(tick))) {
        std::printf("Tick after 1s=%u\n", tick);
    }
    if (read_mem(jlinkReadMem, kFaultRecordAddress, &fault, sizeof(fault))) {
        dump_fault_record(fault);
    }

    auto read_up_buffer = [&](const char *tag) {
        if (upDesc.bufferAddress == 0u || upDesc.sizeOfBuffer < 2u) {
            std::printf("%s invalid up descriptor\n", tag);
            return;
        }
        if (!read_mem(jlinkReadMem, upDescAddr, &upDesc, sizeof(upDesc))) {
            std::printf("%s refresh up descriptor failed\n", tag);
            return;
        }
        const uint32_t used = upDesc.readOffset <= upDesc.writeOffset
            ? upDesc.writeOffset - upDesc.readOffset
            : upDesc.sizeOfBuffer - (upDesc.readOffset - upDesc.writeOffset);
        std::printf("%s up used=%u wr=%u rd=%u\n", tag, used, upDesc.writeOffset, upDesc.readOffset);
        if (used == 0u) {
            return;
        }

        uint8_t buf[1024];
        const uint32_t bytesToRead = used > sizeof(buf) ? static_cast<uint32_t>(sizeof(buf)) : used;
        const uint32_t firstChunk = (upDesc.readOffset + bytesToRead <= upDesc.sizeOfBuffer)
            ? bytesToRead
            : (upDesc.sizeOfBuffer - upDesc.readOffset);
        if (!read_mem(jlinkReadMem, upDesc.bufferAddress + upDesc.readOffset, buf, firstChunk)) {
            std::printf("%s read first chunk failed\n", tag);
            return;
        }
        if (firstChunk < bytesToRead) {
            if (!read_mem(jlinkReadMem, upDesc.bufferAddress, buf + firstChunk, bytesToRead - firstChunk)) {
                std::printf("%s read wrap chunk failed\n", tag);
                return;
            }
        }
        dump_bytes("UP data=", buf, bytesToRead);
        dump_ascii("UP ascii=", buf, bytesToRead);
        const uint32_t newReadOffset = (upDesc.readOffset + bytesToRead) % upDesc.sizeOfBuffer;
        if (!write_u32(jlinkWriteU32, upDescAddr + kSeggerRttDescriptorReadOffset, newReadOffset)) {
            std::printf("%s update read offset failed\n", tag);
        }
    };

    auto write_down_buffer = [&](const char *bytes, uint32_t len) {
        if (downDesc.bufferAddress == 0u || downDesc.sizeOfBuffer < 2u) {
            std::printf("invalid down descriptor\n");
            return false;
        }
        if (!read_mem(jlinkReadMem, downDescAddr, &downDesc, sizeof(downDesc))) {
            std::printf("refresh down descriptor failed\n");
            return false;
        }
        const uint32_t used = downDesc.readOffset <= downDesc.writeOffset
            ? downDesc.writeOffset - downDesc.readOffset
            : downDesc.sizeOfBuffer - (downDesc.readOffset - downDesc.writeOffset);
        const uint32_t free = downDesc.sizeOfBuffer - used - 1u;
        std::printf("down free=%u wr=%u rd=%u len=%u\n", free, downDesc.writeOffset, downDesc.readOffset, len);
        if (free < len) {
            return false;
        }
        const uint32_t firstChunk = (downDesc.writeOffset + len <= downDesc.sizeOfBuffer)
            ? len
            : (downDesc.sizeOfBuffer - downDesc.writeOffset);
        if (!write_mem(jlinkWriteMem, downDesc.bufferAddress + downDesc.writeOffset, bytes, firstChunk)) {
            std::printf("write first down chunk failed\n");
            return false;
        }
        if (firstChunk < len) {
            if (!write_mem(jlinkWriteMem, downDesc.bufferAddress, bytes + firstChunk, len - firstChunk)) {
                std::printf("write wrap down chunk failed\n");
                return false;
            }
        }
        const uint32_t newWriteOffset = (downDesc.writeOffset + len) % downDesc.sizeOfBuffer;
        if (!write_u32(jlinkWriteU32, downDescAddr + kSeggerRttDescriptorWriteOffset, newWriteOffset)) {
            std::printf("update down write offset failed\n");
            return false;
        }
        return true;
    };

    const char *helloHexLine = "A55A010101000102000100EB84\n";
    read_up_buffer("before-hello");
    std::printf("Writing HELLO hex line via raw down buffer...\n");
    const bool downOk = write_down_buffer(helloHexLine, static_cast<uint32_t>(std::strlen(helloHexLine)));
    std::printf("Down buffer write ok=%d\n", downOk ? 1 : 0);
    if (read_mem(jlinkReadMem, downDescAddr, &downDesc, sizeof(downDesc))) {
        std::printf("DownDesc after write buf=0x%08X size=%u wr=%u rd=%u flags=%u\n",
                    downDesc.bufferAddress, downDesc.sizeOfBuffer, downDesc.writeOffset, downDesc.readOffset, downDesc.flags);
    }
    sleep_ms(200);
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (read_mem(jlinkReadMem, kTickAddress, &tick, sizeof(tick))) {
            std::printf("Tick poll %d=%u halted=%d\n", attempt + 1, tick, jlinkIsHalted());
        }
        read_up_buffer("poll");
        if (read_mem(jlinkReadMem, downDescAddr, &downDesc, sizeof(downDesc))) {
            std::printf("DownDesc poll %d wr=%u rd=%u\n",
                        attempt + 1,
                        downDesc.writeOffset,
                        downDesc.readOffset);
        }
        sleep_ms(200);
    }

    jlinkClose();
    return 0;
}
