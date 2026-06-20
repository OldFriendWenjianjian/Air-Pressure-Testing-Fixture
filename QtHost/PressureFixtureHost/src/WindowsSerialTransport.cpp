#include "WindowsSerialTransport.h"

#include <QByteArray>
#include <QFile>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cstring>

WindowsSerialTransport::WindowsSerialTransport(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(20);
    connect(&m_pollTimer, &QTimer::timeout, this, &WindowsSerialTransport::pollRead);
}

namespace {

constexpr int kJlinkRttTerminalStart = 0;
constexpr int kJlinkRttTerminalStop = 1;
constexpr int kJlinkRttTerminalStatus = 4;
constexpr unsigned kJlinkRttControlBufferIndex = 1;
constexpr int kJlinkSwdSpeedKhz = 100;

constexpr uint32_t kSeggerRttPreferredControlBlockAddress = 0x20000070u;
constexpr uint32_t kSeggerRttLegacyControlBlockAddress = 0x20000068u;
constexpr uint32_t kStm32F103zeSramBaseAddress = 0x20000000u;
constexpr uint32_t kStm32F103zeSramSize = 0x00010000u;
constexpr uint32_t kRttScanChunkSize = 1024u;
constexpr int kRttLocateRetryAttempts = 20;
constexpr unsigned long kRttLocateRetryDelayMs = 50u;
constexpr uint32_t kSeggerRttMaxUpOffset = 16u;
constexpr uint32_t kSeggerRttMaxDownOffset = 20u;
constexpr uint32_t kSeggerRttDescriptorBaseOffset = 24u;
constexpr uint32_t kSeggerRttBufferDescriptorSize = 24u;
constexpr uint32_t kSeggerRttDescriptorWriteOffset = 12u;
constexpr uint32_t kSeggerRttDescriptorReadOffset = 16u;
constexpr int kRttDownBufferWaitAttempts = 20;

} // namespace

WindowsSerialTransport::~WindowsSerialTransport()
{
    close();
}

QVector<WindowsSerialTransport::PortInfo> WindowsSerialTransport::availablePortInfos()
{
    QVector<PortInfo> ports;
#ifdef Q_OS_WIN
    PortInfo rtt;
    rtt.portName = QStringLiteral("RTT:JLINK");
    rtt.displayName = QStringLiteral("SEGGER RTT - J-Link SWD");
    rtt.description = QStringLiteral("SEGGER RTT");
    rtt.isSegger = true;
    rtt.isRtt = true;
    ports.push_back(rtt);

    QHash<QString, QString> descriptions;
    QSettings serialMap(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"),
                        QSettings::NativeFormat);
    const auto keys = serialMap.allKeys();
    for (const auto &key : keys) {
        const QString portName = serialMap.value(key).toString();
        if (key.contains(QStringLiteral("VID_1366&PID_0105"), Qt::CaseInsensitive) ||
            key.contains(QStringLiteral("JLink"), Qt::CaseInsensitive)) {
            descriptions.insert(portName, QStringLiteral("SEGGER J-Link VCOM"));
        }
    }

    for (int i = 1; i <= 64; ++i) {
        const QString name = QString("COM%1").arg(i);
        const QString devicePath = QString("\\\\.\\%1").arg(name);
        HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()),
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            PortInfo info;
            info.portName = name;
            info.description = descriptions.value(name, QStringLiteral("串口"));
            info.displayName = name;
            info.isSegger = info.description.contains(QStringLiteral("SEGGER"), Qt::CaseInsensitive);
            CloseHandle(h);
            if (info.isSegger) {
                info.displayName = QString("%1 - %2").arg(name, info.description);
            }

            ports.push_back(info);
        }
    }
    std::stable_sort(ports.begin(), ports.end(), [](const PortInfo &a, const PortInfo &b) {
        if (a.isSegger != b.isSegger) {
            return a.isSegger;
        }
        return a.portName < b.portName;
    });
#endif
    return ports;
}

QStringList WindowsSerialTransport::availablePorts()
{
    QStringList ports;
    const auto infos = availablePortInfos();
    for (const auto &info : infos) {
        ports << info.portName;
    }
    return ports;
}

bool WindowsSerialTransport::open(const QString &portName, int baudRate)
{
    close();
#ifndef Q_OS_WIN
    Q_UNUSED(portName)
    Q_UNUSED(baudRate)
    m_lastError = "当前串口传输实现仅支持 Windows";
    emit errorOccurred(m_lastError);
    return false;
#else
    if (portName == QStringLiteral("RTT:JLINK")) {
        Q_UNUSED(baudRate)
        return openRtt();
    }
    return openSerial(portName, baudRate);
#endif
}

bool WindowsSerialTransport::openSerial(const QString &portName, int baudRate)
{
#ifndef Q_OS_WIN
    Q_UNUSED(portName)
    Q_UNUSED(baudRate)
    return false;
#else
    m_portName = portName;
    const QString devicePath = QString("\\\\.\\%1").arg(portName);
    m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()),
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        m_lastError = QString("无法打开 %1").arg(portName);
        emit errorOccurred(m_lastError);
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(m_handle, &dcb)) {
        m_lastError = "读取串口参数失败";
        close();
        emit errorOccurred(m_lastError);
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(m_handle, &dcb)) {
        m_lastError = "设置串口参数失败";
        close();
        emit errorOccurred(m_lastError);
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 1;
    timeouts.ReadTotalTimeoutConstant = 1;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(m_handle, &timeouts);
    PurgeComm(m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    m_pollTimer.start();
    emit openChanged(true);
    return true;
#endif
}

void WindowsSerialTransport::close()
{
#ifdef Q_OS_WIN
    m_pollTimer.stop();
    closeRtt();
    if (m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        emit openChanged(false);
    }
#endif
}

bool WindowsSerialTransport::isOpen() const
{
#ifdef Q_OS_WIN
    return m_handle != INVALID_HANDLE_VALUE || m_rttOpen;
#else
    return false;
#endif
}

QString WindowsSerialTransport::portName() const
{
    return m_portName;
}

QString WindowsSerialTransport::lastError() const
{
    return m_lastError;
}

bool WindowsSerialTransport::writeBytes(const QByteArray &bytes)
{
#ifndef Q_OS_WIN
    Q_UNUSED(bytes)
    return false;
#else
    if (m_rttOpen) {
        return writeRtt(bytes);
    }
    if (!isOpen()) {
        m_lastError = "串口未打开";
        emit errorOccurred(m_lastError);
        return false;
    }

    DWORD written = 0;
    if (!WriteFile(m_handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != static_cast<DWORD>(bytes.size())) {
        m_lastError = "串口写入失败";
        emit errorOccurred(m_lastError);
        return false;
    }
    return true;
#endif
}

void WindowsSerialTransport::pollRead()
{
#ifdef Q_OS_WIN
    if (m_rttOpen) {
        pollRtt();
        return;
    }
    if (!isOpen()) {
        return;
    }

    DWORD errors = 0;
    COMSTAT stat{};
    if (!ClearCommError(m_handle, &errors, &stat)) {
        m_lastError = "读取串口状态失败";
        emit errorOccurred(m_lastError);
        close();
        return;
    }
    if (stat.cbInQue == 0) {
        return;
    }

    QByteArray buffer;
    buffer.resize(static_cast<int>(stat.cbInQue));
    DWORD read = 0;
    if (ReadFile(m_handle, buffer.data(), stat.cbInQue, &read, nullptr) && read > 0) {
        buffer.resize(static_cast<int>(read));
        emit bytesReceived(buffer);
    }
#endif
}

bool WindowsSerialTransport::openRtt()
{
#ifndef Q_OS_WIN
    return false;
#else
    const bool is64BitProcess = sizeof(void *) == 8u;
    const QStringList dllPaths = is64BitProcess
        ? QStringList{
              QStringLiteral("C:/Program Files/SEGGER/JLink/JLink_x64.dll"),
              QStringLiteral("C:/Program Files/SEGGER/JLink/JLinkARM.dll"),
              QStringLiteral("C:/Program Files (x86)/SEGGER/JLink/JLink_x64.dll"),
              QStringLiteral("C:/Program Files (x86)/SEGGER/JLink/JLinkARM.dll"),
              QStringLiteral("JLink_x64"),
              QStringLiteral("JLinkARM")}
        : QStringList{
              QStringLiteral("C:/Program Files (x86)/SEGGER/JLink/JLinkARM.dll"),
              QStringLiteral("C:/Program Files/SEGGER/JLink/JLinkARM.dll"),
              QStringLiteral("JLinkARM")};

    QStringList loadErrors;
    for (const auto &path : dllPaths) {
        const bool isBareName = !path.contains(QLatin1Char('/')) && !path.contains(QLatin1Char('\\'));
        if (!isBareName && !QFile::exists(path)) {
            continue;
        }

        m_jlink.setFileName(path);
        if (m_jlink.load()) {
            break;
        }
        loadErrors << QStringLiteral("%1: %2").arg(path, m_jlink.errorString());
    }

    if (!m_jlink.isLoaded()) {
        m_lastError = QStringLiteral("无法加载 J-Link DLL，当前程序为 %1 位: %2")
                          .arg(is64BitProcess ? 64 : 32)
                          .arg(loadErrors.join(QStringLiteral("; ")));
        emit errorOccurred(m_lastError);
        return false;
    }

    m_jlinkOpen = reinterpret_cast<JlinkOpenFn>(m_jlink.resolve("JLINKARM_Open"));
    m_jlinkClose = reinterpret_cast<JlinkCloseFn>(m_jlink.resolve("JLINKARM_Close"));
    m_jlinkTifSelect = reinterpret_cast<JlinkTifSelectFn>(m_jlink.resolve("JLINKARM_TIF_Select"));
    m_jlinkSetSpeed = reinterpret_cast<JlinkSetSpeedFn>(m_jlink.resolve("JLINKARM_SetSpeed"));
    m_jlinkExecCommand = reinterpret_cast<JlinkExecCommandFn>(m_jlink.resolve("JLINKARM_ExecCommand"));
    m_jlinkConnect = reinterpret_cast<JlinkConnectFn>(m_jlink.resolve("JLINKARM_Connect"));
    m_jlinkIsConnected = reinterpret_cast<JlinkIsConnectedFn>(m_jlink.resolve("JLINKARM_IsConnected"));
    m_jlinkGo = reinterpret_cast<JlinkGoFn>(m_jlink.resolve("JLINKARM_Go"));
    m_jlinkReset = reinterpret_cast<JlinkResetFn>(m_jlink.resolve("JLINKARM_Reset"));
    m_jlinkReadMem = reinterpret_cast<JlinkReadMemFn>(m_jlink.resolve("JLINKARM_ReadMem"));
    m_jlinkWriteMem = reinterpret_cast<JlinkWriteMemFn>(m_jlink.resolve("JLINKARM_WriteMem"));
    m_jlinkWriteU32 = reinterpret_cast<JlinkWriteU32Fn>(m_jlink.resolve("JLINKARM_WriteU32"));
    m_jlinkRttControl = reinterpret_cast<JlinkRttControlFn>(m_jlink.resolve("JLINK_RTTERMINAL_Control"));
    m_jlinkRttRead = reinterpret_cast<JlinkRttReadFn>(m_jlink.resolve("JLINK_RTTERMINAL_Read"));
    m_jlinkRttWrite = reinterpret_cast<JlinkRttWriteFn>(m_jlink.resolve("JLINK_RTTERMINAL_Write"));

    if (!m_jlinkOpen || !m_jlinkClose || !m_jlinkTifSelect || !m_jlinkSetSpeed ||
        !m_jlinkExecCommand || !m_jlinkConnect || !m_jlinkIsConnected || !m_jlinkReadMem ||
        !m_jlinkWriteMem || !m_jlinkWriteU32) {
        m_lastError = QStringLiteral("J-Link DLL 缺少内存读写 API");
        m_jlink.unload();
        emit errorOccurred(m_lastError);
        return false;
    }

    char suppressGuiError[128] = {};
    (void)m_jlinkExecCommand("SuppressGUI", suppressGuiError, sizeof(suppressGuiError));
    (void)m_jlinkExecCommand("SuppressGUI 1", suppressGuiError, sizeof(suppressGuiError));

    const char *openError = m_jlinkOpen();
    if (openError != nullptr && openError[0] != '\0') {
        m_lastError = QStringLiteral("打开 J-Link 失败: %1").arg(QString::fromLocal8Bit(openError));
        closeRtt();
        emit errorOccurred(m_lastError);
        return false;
    }
    (void)m_jlinkExecCommand("SuppressGUI", suppressGuiError, sizeof(suppressGuiError));
    (void)m_jlinkExecCommand("SuppressGUI 1", suppressGuiError, sizeof(suppressGuiError));

    char errorBuffer[256] = {};
    m_jlinkExecCommand("device = STM32F103ZE", errorBuffer, sizeof(errorBuffer));
    m_jlinkTifSelect(1);
    m_jlinkSetSpeed(kJlinkSwdSpeedKhz);
    if (m_jlinkConnect() < 0) {
        m_lastError = QStringLiteral("连接 J-Link / STM32F103ZE 失败");
        closeRtt();
        emit errorOccurred(m_lastError);
        return false;
    }

    if (m_jlinkIsConnected() == 0) {
        m_lastError = QStringLiteral("J-Link 已打开，但没有连接到 STM32F103ZE。请检查 SWD/NRST 接线、目标供电和读保护状态");
        closeRtt();
        emit errorOccurred(m_lastError);
        return false;
    }

    auto locateWithRetry = [this](uint32_t *address) {
        if (address != nullptr) {
            *address = 0u;
        }
        for (int attempt = 0; attempt < kRttLocateRetryAttempts; ++attempt) {
            if (locateRttControlBlock(address)) {
                return true;
            }
            if (attempt + 1 < kRttLocateRetryAttempts) {
                QThread::msleep(kRttLocateRetryDelayMs);
            }
        }
        return false;
    };

    if (!locateWithRetry(&m_rttControlBlockAddress) && m_jlinkReset) {
        m_jlinkReset();
        if (m_jlinkGo) {
            m_jlinkGo();
        }
        QThread::msleep(200u);
        (void)locateWithRetry(&m_rttControlBlockAddress);
    }

    if (m_rttControlBlockAddress == 0u) {
        closeRtt();
        emit errorOccurred(m_lastError);
        return false;
    }

    JlinkRttBufferDescriptor upDescriptor;
    JlinkRttBufferDescriptor downDescriptor;
    uint32_t upDescriptorAddress = 0u;
    if (!readRttBufferDescriptor(kJlinkRttControlBufferIndex, true, &upDescriptor) ||
        !readRttBufferDescriptor(kJlinkRttControlBufferIndex, true, &upDescriptor, &upDescriptorAddress) ||
        !readRttBufferDescriptor(kJlinkRttControlBufferIndex, false, &downDescriptor) ||
        upDescriptor.bufferAddress == 0u || upDescriptor.sizeOfBuffer < 512u ||
        downDescriptor.bufferAddress == 0u || downDescriptor.sizeOfBuffer < 64u) {
        m_lastError = QStringLiteral("未找到 RTT 控制通道 buffer %1，请重新烧录最新固件后再连接")
                          .arg(kJlinkRttControlBufferIndex);
        closeRtt();
        emit errorOccurred(m_lastError);
        return false;
    }
    if (!writeRttU32(upDescriptorAddress + kSeggerRttDescriptorReadOffset, upDescriptor.writeOffset)) {
        closeRtt();
        emit errorOccurred(m_lastError);
        return false;
    }

    m_portName = QStringLiteral("RTT:JLINK");
    m_rttOpen = true;
    m_pollTimer.start();
    emit openChanged(true);
    return true;
#endif
}

void WindowsSerialTransport::closeRtt()
{
#ifdef Q_OS_WIN
    if (m_rttOpen) {
        m_rttOpen = false;
        emit openChanged(false);
    }
    if (m_rttTerminalStarted && m_jlinkRttControl) {
        m_jlinkRttControl(kJlinkRttTerminalStop, nullptr);
        m_rttTerminalStarted = false;
    }
    if (m_jlinkClose) {
        m_jlinkClose();
    }
    if (m_jlink.isLoaded()) {
        m_jlink.unload();
    }
    m_jlinkOpen = nullptr;
    m_jlinkClose = nullptr;
    m_jlinkTifSelect = nullptr;
    m_jlinkSetSpeed = nullptr;
    m_jlinkExecCommand = nullptr;
    m_jlinkConnect = nullptr;
    m_jlinkIsConnected = nullptr;
    m_jlinkGo = nullptr;
    m_jlinkReset = nullptr;
    m_jlinkReadMem = nullptr;
    m_jlinkWriteMem = nullptr;
    m_jlinkWriteU32 = nullptr;
    m_jlinkRttControl = nullptr;
    m_jlinkRttRead = nullptr;
    m_jlinkRttWrite = nullptr;
    m_rttControlBlockAddress = 0u;
    m_rttTerminalStarted = false;
#endif
}

bool WindowsSerialTransport::writeRtt(const QByteArray &bytes)
{
#ifndef Q_OS_WIN
    Q_UNUSED(bytes)
    return false;
#else
    if (!m_rttOpen) {
        m_lastError = QStringLiteral("SEGGER RTT 未打开");
        emit errorOccurred(m_lastError);
        return false;
    }

    return writeRttDownBuffer(kJlinkRttControlBufferIndex, bytes);
#endif
}

void WindowsSerialTransport::pollRtt()
{
#ifdef Q_OS_WIN
    if (!m_rttOpen) {
        return;
    }

    QByteArray buffer;
    if (readRttUpBuffer(kJlinkRttControlBufferIndex, &buffer, 1024u) && !buffer.isEmpty()) {
        emit bytesReceived(buffer);
    }
#endif
}

#ifdef Q_OS_WIN
bool WindowsSerialTransport::readRttMemory(uint32_t address, void *data, uint32_t size)
{
    if (size == 0u) {
        return true;
    }
    if (!m_jlinkReadMem || data == nullptr) {
        m_lastError = QStringLiteral("J-Link 内存读取接口未就绪");
        return false;
    }

    const int rc = m_jlinkReadMem(address, size, data);
    if (rc < 0 || (rc > 0 && rc != static_cast<int>(size))) {
        m_lastError = QStringLiteral("J-Link 读取内存失败: addr=0x%1 size=%2 rc=%3")
                          .arg(address, 8, 16, QLatin1Char('0'))
                          .arg(size)
                          .arg(rc);
        return false;
    }
    return true;
}

bool WindowsSerialTransport::writeRttMemory(uint32_t address, const void *data, uint32_t size)
{
    if (size == 0u) {
        return true;
    }
    if (!m_jlinkWriteMem || data == nullptr) {
        m_lastError = QStringLiteral("J-Link 内存写入接口未就绪");
        return false;
    }

    const int rc = m_jlinkWriteMem(address, size, data);
    if (rc < 0 || (rc > 0 && rc != static_cast<int>(size))) {
        m_lastError = QStringLiteral("J-Link 写入内存失败: addr=0x%1 size=%2 rc=%3")
                          .arg(address, 8, 16, QLatin1Char('0'))
                          .arg(size)
                          .arg(rc);
        return false;
    }
    return true;
}

bool WindowsSerialTransport::writeRttU32(uint32_t address, uint32_t value)
{
    if (!m_jlinkWriteU32) {
        m_lastError = QStringLiteral("J-Link 32 位写入接口未就绪");
        return false;
    }

    const int rc = m_jlinkWriteU32(address, value);
    if (rc < 0) {
        m_lastError = QStringLiteral("J-Link 写入 U32 失败: addr=0x%1 value=%2 rc=%3")
                          .arg(address, 8, 16, QLatin1Char('0'))
                          .arg(value)
                          .arg(rc);
        return false;
    }
    return true;
}

bool WindowsSerialTransport::locateRttControlBlock(uint32_t *address)
{
    if (address == nullptr) {
        m_lastError = QStringLiteral("RTT 控制块输出参数错误");
        return false;
    }

    const QByteArray needle = QByteArrayLiteral("SEGGER RTT");
    QByteArray preferred(16, '\0');
    if (readRttMemory(kSeggerRttPreferredControlBlockAddress,
                      preferred.data(),
                      static_cast<uint32_t>(preferred.size())) &&
        preferred.startsWith(needle)) {
        *address = kSeggerRttPreferredControlBlockAddress;
        return true;
    }
    QByteArray legacy(16, '\0');
    if (readRttMemory(kSeggerRttLegacyControlBlockAddress,
                      legacy.data(),
                      static_cast<uint32_t>(legacy.size())) &&
        legacy.startsWith(needle)) {
        *address = kSeggerRttLegacyControlBlockAddress;
        return true;
    }

    QByteArray previousTail;
    for (uint32_t offset = 0u; offset < kStm32F103zeSramSize; offset += kRttScanChunkSize) {
        const uint32_t bytesToRead = std::min(kRttScanChunkSize, kStm32F103zeSramSize - offset);
        QByteArray chunk(static_cast<int>(bytesToRead), '\0');
        if (!readRttMemory(kStm32F103zeSramBaseAddress + offset,
                           chunk.data(),
                           bytesToRead)) {
            continue;
        }

        const QByteArray searchable = previousTail + chunk;
        const int found = searchable.indexOf(needle);
        if (found >= 0) {
            const uint32_t tailSize = static_cast<uint32_t>(previousTail.size());
            const uint32_t foundOffset = found >= previousTail.size()
                ? offset + static_cast<uint32_t>(found - previousTail.size())
                : offset - tailSize + static_cast<uint32_t>(found);
            *address = kStm32F103zeSramBaseAddress + foundOffset;
            return true;
        }

        previousTail = chunk.right(needle.size() - 1);
    }

    const QString hex = QString::fromLatin1(preferred.toHex(' '));
    const QString legacyHex = QString::fromLatin1(legacy.toHex(' '));
    m_lastError = QStringLiteral("未在 STM32 SRAM 中找到 SEGGER RTT 控制块，当前 map 地址 0x%1 数据=%2；旧地址 0x%3 数据=%4。已按 SWD %5kHz 放行/复位重试，请确认已烧录最新 RTT 固件、BOOT0=0、目标板供电正常且芯片未开启读保护")
                      .arg(kSeggerRttPreferredControlBlockAddress, 8, 16, QLatin1Char('0'))
                      .arg(hex.isEmpty() ? QStringLiteral("--") : hex)
                      .arg(kSeggerRttLegacyControlBlockAddress, 8, 16, QLatin1Char('0'))
                      .arg(legacyHex.isEmpty() ? QStringLiteral("--") : legacyHex)
                      .arg(kJlinkSwdSpeedKhz);
    return false;
}

bool WindowsSerialTransport::readRttBufferDescriptor(unsigned bufferIndex,
                                                     bool upBuffer,
                                                     JlinkRttBufferDescriptor *descriptor,
                                                     uint32_t *descriptorAddress)
{
    if (descriptor == nullptr) {
        m_lastError = QStringLiteral("RTT buffer 描述符参数错误");
        return false;
    }

    uint32_t maxUpBuffers = 0;
    uint32_t maxDownBuffers = 0;
    if (m_rttControlBlockAddress == 0u) {
        m_lastError = QStringLiteral("RTT 控制块地址未初始化");
        return false;
    }
    if (!readRttMemory(m_rttControlBlockAddress + kSeggerRttMaxUpOffset, &maxUpBuffers, sizeof(maxUpBuffers)) ||
        !readRttMemory(m_rttControlBlockAddress + kSeggerRttMaxDownOffset, &maxDownBuffers, sizeof(maxDownBuffers))) {
        return false;
    }

    const uint32_t maxBuffers = upBuffer ? maxUpBuffers : maxDownBuffers;
    if (maxUpBuffers == 0u || maxUpBuffers > 16u || maxDownBuffers == 0u || maxDownBuffers > 16u ||
        bufferIndex >= maxBuffers) {
        m_lastError = QStringLiteral("RTT buffer 索引无效: index=%1 up=%2 maxUp=%3 maxDown=%4")
                          .arg(bufferIndex)
                          .arg(upBuffer ? 1 : 0)
                          .arg(maxUpBuffers)
                          .arg(maxDownBuffers);
        return false;
    }

    const uint32_t firstDescriptor = m_rttControlBlockAddress + kSeggerRttDescriptorBaseOffset;
    const uint32_t offsetIndex = upBuffer ? bufferIndex : (maxUpBuffers + bufferIndex);
    const uint32_t address = firstDescriptor + (offsetIndex * kSeggerRttBufferDescriptorSize);
    if (!readRttMemory(address, descriptor, sizeof(*descriptor))) {
        return false;
    }
    if (descriptorAddress != nullptr) {
        *descriptorAddress = address;
    }
    return true;
}

bool WindowsSerialTransport::readRttUpBuffer(unsigned bufferIndex, QByteArray *bytes, uint32_t maxBytes)
{
    if (bytes == nullptr) {
        m_lastError = QStringLiteral("RTT up-buffer 输出参数错误");
        return false;
    }
    bytes->clear();
    if (maxBytes == 0u) {
        return true;
    }

    JlinkRttBufferDescriptor descriptor;
    uint32_t descriptorAddress = 0u;
    if (!readRttBufferDescriptor(bufferIndex, true, &descriptor, &descriptorAddress)) {
        emit errorOccurred(m_lastError);
        return false;
    }

    if (descriptor.bufferAddress == 0u || descriptor.sizeOfBuffer < 2u ||
        descriptor.writeOffset >= descriptor.sizeOfBuffer ||
        descriptor.readOffset >= descriptor.sizeOfBuffer) {
        m_lastError = QStringLiteral("RTT up-buffer 描述符异常: buf=0x%1 size=%2 wr=%3 rd=%4")
                          .arg(descriptor.bufferAddress, 8, 16, QLatin1Char('0'))
                          .arg(descriptor.sizeOfBuffer)
                          .arg(descriptor.writeOffset)
                          .arg(descriptor.readOffset);
        emit errorOccurred(m_lastError);
        return false;
    }

    const uint32_t used = descriptor.readOffset <= descriptor.writeOffset
        ? descriptor.writeOffset - descriptor.readOffset
        : descriptor.sizeOfBuffer - (descriptor.readOffset - descriptor.writeOffset);
    if (used == 0u) {
        return true;
    }

    const uint32_t bytesToRead = std::min(used, maxBytes);
    bytes->resize(static_cast<int>(bytesToRead));
    const uint32_t firstChunk = std::min(bytesToRead, descriptor.sizeOfBuffer - descriptor.readOffset);
    if (!readRttMemory(descriptor.bufferAddress + descriptor.readOffset,
                       bytes->data(),
                       firstChunk)) {
        emit errorOccurred(m_lastError);
        bytes->clear();
        return false;
    }
    if (firstChunk < bytesToRead) {
        if (!readRttMemory(descriptor.bufferAddress,
                           bytes->data() + firstChunk,
                           bytesToRead - firstChunk)) {
            emit errorOccurred(m_lastError);
            bytes->clear();
            return false;
        }
    }

    const uint32_t newReadOffset = (descriptor.readOffset + bytesToRead) % descriptor.sizeOfBuffer;
    if (!writeRttU32(descriptorAddress + kSeggerRttDescriptorReadOffset, newReadOffset)) {
        emit errorOccurred(m_lastError);
        bytes->clear();
        return false;
    }
    return true;
}

bool WindowsSerialTransport::writeRttDownBuffer(unsigned bufferIndex, const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return true;
    }

    JlinkRttBufferDescriptor descriptor;
    uint32_t descriptorAddress = 0;
    for (int attempt = 0; attempt < kRttDownBufferWaitAttempts; ++attempt) {
        if (!readRttBufferDescriptor(bufferIndex, false, &descriptor, &descriptorAddress)) {
            emit errorOccurred(m_lastError);
            return false;
        }

        if (descriptor.bufferAddress == 0u || descriptor.sizeOfBuffer < 2u ||
            descriptor.writeOffset >= descriptor.sizeOfBuffer ||
            descriptor.readOffset >= descriptor.sizeOfBuffer) {
            m_lastError = QStringLiteral("RTT down-buffer 描述符异常: buf=0x%1 size=%2 wr=%3 rd=%4")
                              .arg(descriptor.bufferAddress, 8, 16, QLatin1Char('0'))
                              .arg(descriptor.sizeOfBuffer)
                              .arg(descriptor.writeOffset)
                              .arg(descriptor.readOffset);
            emit errorOccurred(m_lastError);
            return false;
        }

        if (bytes.size() > static_cast<int>(descriptor.sizeOfBuffer - 1u)) {
            m_lastError = QStringLiteral("RTT 下行帧过长: len=%1 buffer=%2")
                              .arg(bytes.size())
                              .arg(descriptor.sizeOfBuffer);
            emit errorOccurred(m_lastError);
            return false;
        }

        const uint32_t used = descriptor.readOffset <= descriptor.writeOffset
            ? descriptor.writeOffset - descriptor.readOffset
            : descriptor.sizeOfBuffer - (descriptor.readOffset - descriptor.writeOffset);
        const uint32_t free = descriptor.sizeOfBuffer - used - 1u;
        if (free >= static_cast<uint32_t>(bytes.size())) {
            const uint32_t firstChunk = std::min<uint32_t>(static_cast<uint32_t>(bytes.size()),
                                                          descriptor.sizeOfBuffer - descriptor.writeOffset);
            if (!writeRttMemory(descriptor.bufferAddress + descriptor.writeOffset,
                                bytes.constData(),
                                firstChunk)) {
                emit errorOccurred(m_lastError);
                return false;
            }
            if (firstChunk < static_cast<uint32_t>(bytes.size())) {
                if (!writeRttMemory(descriptor.bufferAddress,
                                    bytes.constData() + firstChunk,
                                    static_cast<uint32_t>(bytes.size()) - firstChunk)) {
                    emit errorOccurred(m_lastError);
                    return false;
                }
            }

            const uint32_t newWriteOffset =
                (descriptor.writeOffset + static_cast<uint32_t>(bytes.size())) % descriptor.sizeOfBuffer;
            if (!writeRttU32(descriptorAddress + kSeggerRttDescriptorWriteOffset, newWriteOffset)) {
                emit errorOccurred(m_lastError);
                return false;
            }
            return true;
        }

        Sleep(10);
    }

    m_lastError = QStringLiteral("RTT 下行缓冲区已满，暂时无法发送命令");
    emit errorOccurred(m_lastError);
    return false;
}
#endif
