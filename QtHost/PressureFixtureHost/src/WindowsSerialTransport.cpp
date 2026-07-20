#include "WindowsSerialTransport.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSettings>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cstring>

WindowsSerialTransport::WindowsSerialTransport(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(20);
    connect(&m_pollTimer, &QTimer::timeout, this, &WindowsSerialTransport::pollRead);
    connect(&m_rttSocket, &QTcpSocket::readyRead, this, &WindowsSerialTransport::handleRttSocketReadyRead);
    connect(&m_rttSocket, &QTcpSocket::errorOccurred, this, &WindowsSerialTransport::handleRttSocketError);
    connect(&m_rttServerProcess, &QProcess::readyReadStandardOutput, this, &WindowsSerialTransport::handleRttServerOutput);
    connect(&m_rttServerProcess, &QProcess::readyReadStandardError, this, &WindowsSerialTransport::handleRttServerErrorOutput);
    connect(&m_rttDllPollWatcher, &QFutureWatcher<QByteArray>::finished, this, &WindowsSerialTransport::handleDllPollFinished);
    connect(&m_rttServerProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &WindowsSerialTransport::handleRttServerFinished);
}

namespace {

constexpr int kJlinkRttTerminalStart = 0;
constexpr int kJlinkRttTerminalStop = 1;
constexpr int kJlinkRttTerminalStatus = 4;
constexpr int kJlinkRttTerminalBufferInfo = 5;
constexpr unsigned kJlinkRttControlBufferIndex = 0;
constexpr int kJlinkSwdSpeedKhz = 100;

constexpr uint32_t kSeggerRttControlBlockAddress = 0x20000078u;
constexpr uint32_t kSeggerRttLegacyPreferredControlBlockAddress = 0x20000070u;
constexpr uint32_t kSeggerRttLegacyFallbackControlBlockAddress = 0x20000068u;
constexpr uint32_t kStm32F103zeSramBaseAddress = 0x20000000u;
constexpr uint32_t kStm32F103zeSramSize = 0x00010000u;
constexpr uint32_t kRttScanChunkSize = 1024u;
constexpr quint16 kJlinkRttPort = 19021u;
constexpr quint16 kJlinkGdbPort = 52331u;
constexpr quint16 kJlinkSwoPort = 52332u;
constexpr quint16 kJlinkTerminalPort = 52333u;
constexpr int kJlinkGdbServerStartupDelayMs = 2500;
constexpr int kJlinkGdbServerShutdownWaitMs = 3000;
constexpr int kJlinkRttReadyWaitMs = 8000;
constexpr int kJlinkRttSocketConfigWriteWaitMs = 1000;
constexpr int kJlinkRttCycleRunMs = 50;
constexpr int kRttLocateRetryAttempts = 20;
constexpr unsigned long kRttLocateRetryDelayMs = 50u;
constexpr uint32_t kSeggerRttMaxUpOffset = 16u;
constexpr uint32_t kSeggerRttMaxDownOffset = 20u;
constexpr uint32_t kSeggerRttDescriptorBaseOffset = 24u;
constexpr uint32_t kSeggerRttBufferDescriptorSize = 24u;
constexpr uint32_t kSeggerRttDescriptorWriteOffset = 12u;
constexpr uint32_t kSeggerRttDescriptorReadOffset = 16u;
constexpr int kRttDownBufferWaitAttempts = 20;
constexpr int kRttDescriptorRetryAttempts = 20;
constexpr int kRttPollFailureWarnEvery = 50;
constexpr int kRttPollFailureCloseAfter = 250;
constexpr int kJlinkTifSwd = 1;

#ifdef Q_OS_WIN
QString registryStringValue(HKEY key, const wchar_t *valueName)
{
    wchar_t buffer[512] = {};
    DWORD type = 0u;
    DWORD size = sizeof(buffer);
    const LONG rc = RegQueryValueExW(key,
                                     valueName,
                                     nullptr,
                                     &type,
                                     reinterpret_cast<LPBYTE>(buffer),
                                     &size);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return QString();
    }
    return QString::fromWCharArray(buffer).trimmed();
}

QStringList registrySubkeys(HKEY key)
{
    QStringList names;
    for (DWORD index = 0u;; ++index) {
        wchar_t name[256] = {};
        DWORD nameLength = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
        const LONG rc = RegEnumKeyExW(key,
                                      index,
                                      name,
                                      &nameLength,
                                      nullptr,
                                      nullptr,
                                      nullptr,
                                      nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc == ERROR_SUCCESS) {
            names.push_back(QString::fromWCharArray(name, static_cast<int>(nameLength)));
        }
    }
    return names;
}
#endif

bool isSuccessfulJlinkTransferResult(int rc, uint32_t size)
{
    if (size == 0u) {
        return true;
    }
    if (rc < 0) {
        return false;
    }
    if (rc == 0) {
        return true;
    }

    const int byteCount = static_cast<int>(size);
    if (rc == byteCount) {
        return true;
    }

    // Some J-Link DLL variants report the count in 32-bit words for memory APIs.
    if ((size % sizeof(uint32_t)) == 0u) {
        const int wordCount = static_cast<int>(size / sizeof(uint32_t));
        if (rc == wordCount) {
            return true;
        }
    }

    return false;
}

bool isSramAddressRange(uint32_t address, uint32_t size)
{
    if (address < kStm32F103zeSramBaseAddress) {
        return false;
    }
    const uint32_t end = address + size;
    const uint32_t sramEnd = kStm32F103zeSramBaseAddress + kStm32F103zeSramSize;
    return end >= address && end <= sramEnd;
}

bool isJlinkTargetHaltedState(int state)
{
    return state == 1;
}

QByteArray toHexLine(const QByteArray &bytes)
{
    QByteArray line;
    line.reserve(bytes.size() * 2 + 1);
    line.append(bytes.toHex().toUpper());
    line.append('\n');
    return line;
}

QString sanitizeProcessOutput(const QByteArray &bytes)
{
    QString text = QString::fromLocal8Bit(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.trimmed();
}

#ifdef Q_OS_WIN
QString formatWin32ErrorMessage(DWORD errorCode)
{
    if (errorCode == 0u) {
        return QString();
    }

    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags,
                                        nullptr,
                                        errorCode,
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    const QString systemText = length > 0 && buffer != nullptr
        ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
        : QString();
    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    return systemText.isEmpty()
        ? QStringLiteral("Win32=%1").arg(errorCode)
        : QStringLiteral("Win32=%1: %2").arg(errorCode).arg(systemText);
}

bool configureSerialHandle(HANDLE handle, int baudRate, QString *errorMessage)
{
    QString configureWarning;
    DCB baseDcb{};
    baseDcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle, &baseDcb)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("读取串口参数失败 (%1)")
                                .arg(formatWin32ErrorMessage(GetLastError()));
        }
        return false;
    }

    auto applyDcb = [&](DWORD dtrControl, DWORD rtsControl) -> bool {
        DCB dcb = baseDcb;
        dcb.BaudRate = static_cast<DWORD>(baudRate);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.fParity = FALSE;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = dtrControl;
        dcb.fDsrSensitivity = FALSE;
        dcb.fTXContinueOnXoff = TRUE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fErrorChar = FALSE;
        dcb.fNull = FALSE;
        dcb.fRtsControl = rtsControl;
        dcb.fAbortOnError = FALSE;
        return SetCommState(handle, &dcb) != FALSE;
    };

    if (!applyDcb(DTR_CONTROL_ENABLE, RTS_CONTROL_ENABLE) &&
        !applyDcb(DTR_CONTROL_DISABLE, RTS_CONTROL_DISABLE)) {
        const DWORD setCommError = GetLastError();
        const QString setCommText = formatWin32ErrorMessage(setCommError);
        if (setCommError == ERROR_GEN_FAILURE) {
            configureWarning = QStringLiteral("设置串口参数失败，已按设备当前默认参数继续 (%1)")
                                   .arg(setCommText);
        } else {
            if (errorMessage) {
                *errorMessage = QStringLiteral("设置串口参数失败 (%1)")
                                    .arg(setCommText);
            }
            return false;
        }
    }

    (void)SetupComm(handle, 4096u, 4096u);

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 1;
    timeouts.ReadTotalTimeoutConstant = 1;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(handle, &timeouts)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设置串口超时失败 (%1)")
                                .arg(formatWin32ErrorMessage(GetLastError()));
        }
        return false;
    }

    (void)PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    if (!configureWarning.isEmpty() && errorMessage) {
        *errorMessage = configureWarning;
    }
    return true;
}
#endif

QByteArray buildSeggerTelnetConfigString()
{
    return QByteArrayLiteral("$$SEGGER_TELNET_ConfigStr=RTTCh;0;SetRTTAddr;0x20000078;SetRTTSearchRanges;0x20000000 0x10000;$$");
}

} // namespace

WindowsSerialTransport::~WindowsSerialTransport()
{
    close();
}

bool WindowsSerialTransport::isFixtureUsbIdentity(const QString &instanceId,
                                                  const QString &productName,
                                                  const QString &serialNumber)
{
    const bool fixtureVidPid =
        instanceId.contains(QStringLiteral("VID_0483&PID_5740"), Qt::CaseInsensitive);
    const bool fixtureIdentity =
        productName.compare(QStringLiteral("Pressure Fixture CDC"), Qt::CaseInsensitive) == 0 ||
        serialNumber.startsWith(QStringLiteral("PF"), Qt::CaseInsensitive);
    return fixtureVidPid && fixtureIdentity;
}

QVector<WindowsSerialTransport::PortInfo> WindowsSerialTransport::availablePortInfos()
{
    QVector<PortInfo> ports;
#ifdef Q_OS_WIN
    QHash<QString, PortInfo> registryUsbPorts;
    HKEY usbRoot = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Enum\\USB",
                      0u,
                      KEY_READ | KEY_WOW64_64KEY,
                      &usbRoot) == ERROR_SUCCESS) {
        const auto vidPidGroups = registrySubkeys(usbRoot);
        for (const auto &vidPid : vidPidGroups) {
            HKEY vidPidKey = nullptr;
            if (RegOpenKeyExW(usbRoot,
                              reinterpret_cast<LPCWSTR>(vidPid.utf16()),
                              0u,
                              KEY_READ | KEY_WOW64_64KEY,
                              &vidPidKey) != ERROR_SUCCESS) {
                continue;
            }
            const auto serialGroups = registrySubkeys(vidPidKey);
            for (const auto &serial : serialGroups) {
                HKEY deviceKey = nullptr;
                if (RegOpenKeyExW(vidPidKey,
                                  reinterpret_cast<LPCWSTR>(serial.utf16()),
                                  0u,
                                  KEY_READ | KEY_WOW64_64KEY,
                                  &deviceKey) != ERROR_SUCCESS) {
                    continue;
                }
            PortInfo info;
            const QString friendlyName = registryStringValue(deviceKey, L"FriendlyName");
            QString deviceDescription = registryStringValue(deviceKey, L"DeviceDesc");
            HKEY parametersKey = nullptr;
            if (RegOpenKeyExW(deviceKey,
                              L"Device Parameters",
                              0u,
                              KEY_READ | KEY_WOW64_64KEY,
                              &parametersKey) == ERROR_SUCCESS) {
                info.portName = registryStringValue(parametersKey, L"PortName");
                RegCloseKey(parametersKey);
            }

            if (!info.portName.startsWith(QStringLiteral("COM"), Qt::CaseInsensitive)) {
                RegCloseKey(deviceKey);
                continue;
            }
            const int descriptionSeparator = deviceDescription.lastIndexOf(QLatin1Char(';'));
            if (descriptionSeparator >= 0) {
                deviceDescription = deviceDescription.mid(descriptionSeparator + 1);
            }
            info.instanceId = QStringLiteral("USB\\%1\\%2").arg(vidPid, serial);
            wchar_t dosTarget[512] = {};
            if (QueryDosDeviceW(reinterpret_cast<LPCWSTR>(info.portName.utf16()),
                                dosTarget,
                                static_cast<DWORD>(sizeof(dosTarget) / sizeof(dosTarget[0]))) == 0u) {
                RegCloseKey(deviceKey);
                continue;
            }
            info.serialNumber = serial;
            info.isFixtureUsbCdc = isFixtureUsbIdentity(info.instanceId,
                                                        info.productName,
                                                        info.serialNumber);
            info.isSegger = vidPid.contains(QStringLiteral("VID_1366&PID_0105"), Qt::CaseInsensitive) ||
                            friendlyName.contains(QStringLiteral("JLink"), Qt::CaseInsensitive) ||
                            friendlyName.contains(QStringLiteral("J-Link"), Qt::CaseInsensitive);
            if (info.isFixtureUsbCdc) {
                info.productName = QStringLiteral("Pressure Fixture CDC");
                info.description = info.productName;
                info.displayName = QStringLiteral("%1 - 气压检测工装 USB CDC [%2]")
                                       .arg(info.portName, info.serialNumber);
            } else if (info.isSegger) {
                info.description = QStringLiteral("SEGGER J-Link VCOM");
                info.displayName = QStringLiteral("%1 - %2").arg(info.portName, info.description);
            } else {
                info.description = !friendlyName.isEmpty() ? friendlyName : deviceDescription;
                info.displayName = info.description.isEmpty()
                    ? info.portName
                    : QStringLiteral("%1 - %2").arg(info.portName, info.description);
            }
            registryUsbPorts.insert(info.portName.toUpper(), info);
            RegCloseKey(deviceKey);
            }
            RegCloseKey(vidPidKey);
        }
        RegCloseKey(usbRoot);
    }

    if (registryUsbPorts.isEmpty()) {
        HKEY fixtureKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_0483&PID_5740\\PF202601",
                          0u,
                          KEY_READ | KEY_WOW64_64KEY,
                          &fixtureKey) == ERROR_SUCCESS) {
            HKEY parametersKey = nullptr;
            PortInfo info;
            if (RegOpenKeyExW(fixtureKey,
                              L"Device Parameters",
                              0u,
                              KEY_READ | KEY_WOW64_64KEY,
                              &parametersKey) == ERROR_SUCCESS) {
                info.portName = registryStringValue(parametersKey, L"PortName");
                RegCloseKey(parametersKey);
            }
            wchar_t dosTarget[512] = {};
            if (!info.portName.isEmpty() &&
                QueryDosDeviceW(reinterpret_cast<LPCWSTR>(info.portName.utf16()),
                                dosTarget,
                                static_cast<DWORD>(sizeof(dosTarget) / sizeof(dosTarget[0]))) != 0u) {
                info.instanceId = QStringLiteral("USB\\VID_0483&PID_5740\\PF202601");
                info.serialNumber = QStringLiteral("PF202601");
                info.productName = QStringLiteral("Pressure Fixture CDC");
                info.description = info.productName;
                info.isFixtureUsbCdc = true;
                info.displayName = QStringLiteral("%1 - 气压检测工装 USB CDC [PF202601]")
                                       .arg(info.portName);
                registryUsbPorts.insert(info.portName.toUpper(), info);
            }
            RegCloseKey(fixtureKey);
        }
    }

    for (const auto &info : registryUsbPorts) {
        ports.push_back(info);
    }

    for (int i = 1; i <= 64; ++i) {
        const QString portName = QStringLiteral("COM%1").arg(i);
        if (std::any_of(ports.cbegin(), ports.cend(), [&portName](const PortInfo &info) {
                return info.portName.compare(portName, Qt::CaseInsensitive) == 0;
            })) {
            continue;
        }
        const QString devicePath = QStringLiteral("\\\\.\\%1").arg(portName);
        const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()),
                                          GENERIC_READ | GENERIC_WRITE,
                                          0,
                                          nullptr,
                                          OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL,
                                          nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        CloseHandle(handle);

        PortInfo info = registryUsbPorts.value(portName.toUpper());
        if (info.portName.isEmpty()) {
            info.portName = portName;
            info.description = QStringLiteral("串口");
            info.displayName = portName;
        }
        ports.push_back(info);
    }
    std::stable_sort(ports.begin(), ports.end(), [](const PortInfo &a, const PortInfo &b) {
        const auto rank = [](const PortInfo &port) {
            if (port.isFixtureUsbCdc) {
                return 0;
            }
            if (!port.isSegger && !port.isRtt) {
                return 1;
            }
            return 2;
        };
        if (rank(a) != rank(b)) {
            return rank(a) < rank(b);
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
    QString lastAttemptError = QString("无法打开 %1").arg(portName);
    constexpr int kOpenRetryCount = 8;
    constexpr unsigned long kOpenRetryDelayMs = 150u;

    for (int attempt = 0; attempt < kOpenRetryCount; ++attempt) {
        m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()),
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            lastAttemptError = QStringLiteral("无法打开 %1 (%2)")
                                   .arg(portName, formatWin32ErrorMessage(GetLastError()));
        } else if (configureSerialHandle(m_handle, baudRate, &lastAttemptError)) {
            if (lastAttemptError.contains(QStringLiteral("已按设备当前默认参数继续"))) {
                emit errorOccurred(QStringLiteral("%1: %2").arg(portName, lastAttemptError));
            }
            m_pollTimer.start();
            emit openChanged(true);
            return true;
        }

        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }

        if (attempt + 1 < kOpenRetryCount) {
            QThread::msleep(kOpenRetryDelayMs);
        }
    }

    m_lastError = lastAttemptError;
    emit errorOccurred(m_lastError);
    return false;
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
    m_rttReadySeen = false;
    m_rttProtocolSeen = false;

    if (connectToRttSocket(kJlinkRttPort, 300)) {
        if (waitForRttReady(QDateTime::currentMSecsSinceEpoch() + 1000)) {
            m_portName = QStringLiteral("RTT:JLINK");
            m_rttOpen = true;
            resetRttPollFailure();
            m_pollTimer.start();
            emit openChanged(true);
            return true;
        }
        m_portName = QStringLiteral("RTT:JLINK");
        m_rttOpen = true;
        resetRttPollFailure();
        m_pollTimer.start();
        emit openChanged(true);
        return true;
    }

    const QString serverPath = QStringLiteral("C:/Program Files/SEGGER/JLink/JLinkGDBServerCL.exe");
    if (!QFileInfo::exists(serverPath)) {
        m_lastError = QStringLiteral("未找到 JLinkGDBServerCL.exe: %1")
                          .arg(QDir::toNativeSeparators(serverPath));
        emit errorOccurred(m_lastError);
        return false;
    }

    m_rttSocket.abort();

    const QString serverLogDir = QDir::temp().absoluteFilePath(QStringLiteral("pressure-fixture-jlink"));
    QDir().mkpath(serverLogDir);
    m_rttServerLogFilePath = QDir(serverLogDir).absoluteFilePath(
        QStringLiteral("jlink-gdb-server-%1.log").arg(QDateTime::currentMSecsSinceEpoch()));

    const QStringList arguments = {
        QStringLiteral("-select"), QStringLiteral("USB"),
        QStringLiteral("-device"), QStringLiteral("STM32F103ZE"),
        QStringLiteral("-if"), QStringLiteral("SWD"),
        QStringLiteral("-speed"), QString::number(kJlinkSwdSpeedKhz),
        QStringLiteral("-nohalt"),
        QStringLiteral("-port"), QString::number(kJlinkGdbPort),
        QStringLiteral("-swoport"), QString::number(kJlinkSwoPort),
        QStringLiteral("-telnetport"), QString::number(kJlinkTerminalPort),
        QStringLiteral("-RTTTelnetPort"), QString::number(kJlinkRttPort),
        QStringLiteral("-log"), m_rttServerLogFilePath
    };

    if (!QProcess::startDetached(serverPath, arguments, QString(), &m_rttServerPid) || m_rttServerPid <= 0) {
        m_lastError = QStringLiteral("J-Link GDB Server 启动失败: startDetached 返回 false");
        closeRtt();
        emit errorOccurred(m_lastError);
        return false;
    }
    m_rttServerStartedByHost = true;

    const qint64 startupDeadline = QDateTime::currentMSecsSinceEpoch() + kJlinkRttReadyWaitMs;
    while (QDateTime::currentMSecsSinceEpoch() < startupDeadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (connectToRttSocket(kJlinkRttPort, 200)) {
            if (waitForRttReady(startupDeadline)) {
                m_portName = QStringLiteral("RTT:JLINK");
                m_rttOpen = true;
                resetRttPollFailure();
                m_pollTimer.start();
                emit openChanged(true);
                return true;
            }
        }
        if (!isStartedRttServerAlive()) {
            break;
        }
        QThread::msleep(100);
    }

    const QString serverLog = m_rttServerLog.trimmed();
    if (m_rttSocket.state() == QAbstractSocket::ConnectedState && isStartedRttServerAlive()) {
        m_portName = QStringLiteral("RTT:JLINK");
        m_rttOpen = true;
        resetRttPollFailure();
        m_pollTimer.start();
        emit openChanged(true);
        return true;
    }
    const QString logHint = m_rttServerLogFilePath.isEmpty()
        ? QString()
        : QStringLiteral(" | 日志: %1").arg(QDir::toNativeSeparators(m_rttServerLogFilePath));
    m_lastError = serverLog.isEmpty()
        ? QStringLiteral("J-Link RTT socket 未就绪，请确认没有其他 RTT 客户端占用 19021 端口%1").arg(logHint)
        : QStringLiteral("J-Link RTT socket 未就绪: %1%2").arg(serverLog, logHint);

    closeRtt();
    emit errorOccurred(m_lastError);
    return false;
#endif
}

bool WindowsSerialTransport::openRttViaJlinkDll()
{
#ifndef Q_OS_WIN
    return false;
#else
    if (!loadJlinkDll()) {
        return false;
    }
    if (!initializeJlinkRtt()) {
        closeRtt();
        return false;
    }

    if (m_rttControlBlockAddress == 0u && !locateRttControlBlock(&m_rttControlBlockAddress)) {
        closeRtt();
        return false;
    }
    m_jlinkLastCycleAtMs = 0;
    m_rttUsingDll = true;
    m_rttReadySeen = false;
    if (!waitForDllRttReady(QDateTime::currentMSecsSinceEpoch() + kJlinkRttReadyWaitMs)) {
        closeRtt();
        return false;
    }
    return true;
#endif
}

bool WindowsSerialTransport::connectToRttSocket(quint16 port, int timeoutMs)
{
#ifndef Q_OS_WIN
    Q_UNUSED(port)
    Q_UNUSED(timeoutMs)
    return false;
#else
    if (m_rttSocket.state() == QAbstractSocket::ConnectedState) {
        return true;
    }

    m_rttSocket.abort();
    m_rttSocket.connectToHost(QHostAddress::LocalHost, port);
    if (!m_rttSocket.waitForConnected(timeoutMs)) {
        return false;
    }

    m_rttSocketBuffer.clear();
    const QByteArray configString = buildSeggerTelnetConfigString();
    const qint64 written = m_rttSocket.write(configString);
    if (written == configString.size()) {
        if (!m_rttSocket.waitForBytesWritten(kJlinkRttSocketConfigWriteWaitMs)) {
            emit rttTextReceived(QStringLiteral("[RTT SOCKET CONFIG] waitForBytesWritten timeout: %1")
                                     .arg(m_rttSocket.errorString()));
        } else {
            emit rttTextReceived(QStringLiteral("[RTT SOCKET CONFIG] %1")
                                     .arg(QString::fromLatin1(configString)));
        }
    } else {
        emit rttTextReceived(QStringLiteral("[RTT SOCKET CONFIG FAIL] expected=%1 actual=%2 err=%3")
                                 .arg(configString.size())
                                 .arg(written)
                                 .arg(m_rttSocket.errorString()));
    }

    return true;
#endif
}

bool WindowsSerialTransport::waitForRttReady(qint64 deadlineMs)
{
#ifndef Q_OS_WIN
    Q_UNUSED(deadlineMs)
    return false;
#else
    while (QDateTime::currentMSecsSinceEpoch() < deadlineMs) {
        if (m_rttProtocolSeen ||
            hasRttReadyMarker(m_rttSocketBuffer) ||
            m_rttServerLog.contains(QStringLiteral("RTT CB verified"), Qt::CaseInsensitive) ||
            m_rttServerLog.contains(QStringLiteral("Started data handling"), Qt::CaseInsensitive)) {
            return true;
        }
        if (m_rttSocket.state() != QAbstractSocket::ConnectedState) {
            if (!isStartedRttServerAlive()) {
                return false;
            }
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!m_rttSocket.waitForReadyRead(200)) {
            continue;
        }
        handleRttSocketReadyRead();
    }

    if (m_rttProtocolSeen ||
        hasRttReadyMarker(m_rttSocketBuffer) ||
        m_rttServerLog.contains(QStringLiteral("RTT CB verified"), Qt::CaseInsensitive) ||
        m_rttServerLog.contains(QStringLiteral("Started data handling"), Qt::CaseInsensitive)) {
        return true;
    }
    return false;
#endif
}

bool WindowsSerialTransport::isStartedRttServerAlive() const
{
#ifndef Q_OS_WIN
    return false;
#else
    if (!m_rttServerStartedByHost || m_rttServerPid <= 0) {
        return false;
    }
    HANDLE processHandle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(m_rttServerPid));
    if (processHandle == nullptr) {
        return false;
    }
    const DWORD waitResult = WaitForSingleObject(processHandle, 0);
    CloseHandle(processHandle);
    return waitResult == WAIT_TIMEOUT;
#endif
}

void WindowsSerialTransport::closeRtt()
{
#ifdef Q_OS_WIN
    if (m_rttDllPollActive) {
        m_rttDllPollWatcher.waitForFinished();
        m_rttDllPollActive = false;
    }
    if (m_rttSocket.state() != QAbstractSocket::UnconnectedState) {
        m_rttSocket.disconnectFromHost();
        if (m_rttSocket.state() != QAbstractSocket::UnconnectedState) {
            m_rttSocket.abort();
        }
    }
    m_rttSocketBuffer.clear();
    m_rttServerLog.clear();
    if (m_rttOpen) {
        m_rttOpen = false;
        emit openChanged(false);
    }
    resetRttPollFailure();
    if (m_rttServerStartedByHost) {
        if (m_rttServerPid > 0) {
            HANDLE processHandle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                                               FALSE,
                                               static_cast<DWORD>(m_rttServerPid));
            if (processHandle != nullptr) {
                if (WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT) {
                    (void)TerminateProcess(processHandle, 0);
                    (void)WaitForSingleObject(processHandle, kJlinkGdbServerShutdownWaitMs);
                }
                CloseHandle(processHandle);
            }
        }
        m_rttServerStartedByHost = false;
        m_rttServerPid = 0;
    }
    m_rttServerLogFilePath.clear();
    closeJlinkSession();
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
    m_jlinkHalt = nullptr;
    m_jlinkGo = nullptr;
    m_jlinkIsHalted = nullptr;
    m_jlinkReset = nullptr;
    m_jlinkReadMem = nullptr;
    m_jlinkReadMemU32 = nullptr;
    m_jlinkReadMemU8 = nullptr;
    m_jlinkWriteMem = nullptr;
    m_jlinkWriteU32 = nullptr;
    m_jlinkRttControl = nullptr;
    m_jlinkRttRead = nullptr;
    m_jlinkRttWrite = nullptr;
    m_rttControlBlockAddress = 0u;
    m_rttUpDescriptorAddress = 0u;
    m_rttDownDescriptorAddress = 0u;
    m_rttTerminalStarted = false;
    m_rttUsingDll = false;
    m_jlinkSessionOpen = false;
    m_jlinkLastCycleAtMs = 0;
    m_rttDllPollActive = false;
    m_rttDllPollError.clear();
    m_rttReadySeen = false;
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

    if (m_rttUsingDll) {
        const QByteArray encoded = encodeRttTransportFrame(bytes);
        if (!initializeJlinkRtt()) {
            emit errorOccurred(m_lastError);
            return false;
        }
        if (m_rttControlBlockAddress == 0u && !locateRttControlBlock(&m_rttControlBlockAddress)) {
            emit errorOccurred(m_lastError);
            return false;
        }
        if (!ensureJlinkTargetHalted()) {
            emit errorOccurred(m_lastError);
            return false;
        }
        if (!writeRttDownBuffer(kJlinkRttControlBufferIndex, encoded)) {
            emit errorOccurred(m_lastError);
            (void)ensureJlinkTargetRunning();
            return false;
        }
        if (!ensureJlinkTargetRunning()) {
            emit errorOccurred(m_lastError);
            return false;
        }
        m_jlinkLastCycleAtMs = QDateTime::currentMSecsSinceEpoch();
        return true;
    }

    if (m_rttSocket.state() != QAbstractSocket::ConnectedState) {
        m_lastError = QStringLiteral("RTT socket 未连接");
        emit errorOccurred(m_lastError);
        return false;
    }

    const QByteArray encoded = encodeRttTransportFrame(bytes);
    const qint64 written = m_rttSocket.write(encoded);
    if (written != encoded.size()) {
        m_lastError = QStringLiteral("RTT socket 写入失败: expected=%1 actual=%2")
                          .arg(encoded.size())
                          .arg(written);
        emit errorOccurred(m_lastError);
        return false;
    }
    if (!m_rttSocket.waitForBytesWritten(1000)) {
        m_lastError = QStringLiteral("RTT socket 写入超时: %1").arg(m_rttSocket.errorString());
        emit errorOccurred(m_lastError);
        return false;
    }
    return true;
#endif
}

void WindowsSerialTransport::pollRtt()
{
#ifdef Q_OS_WIN
    if (!m_rttOpen) {
        return;
    }

    if (m_rttUsingDll) {
        if (m_rttDllPollActive) {
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if ((nowMs - m_jlinkLastCycleAtMs) < kJlinkRttCycleRunMs) {
            return;
        }
        m_rttDllPollActive = true;
        m_rttDllPollError.clear();
        m_jlinkLastCycleAtMs = nowMs;
        auto future = QtConcurrent::run([this]() -> QByteArray {
            if (!initializeJlinkRtt()) {
                m_rttDllPollError = m_lastError;
                return {};
            }
            if (!ensureJlinkTargetHalted()) {
                m_rttDllPollError = m_lastError;
                return {};
            }
            QByteArray combinedRaw;
            for (int pass = 0; pass < 8; ++pass) {
                QByteArray raw;
                if (!readRttUpBuffer(kJlinkRttControlBufferIndex, &raw, 512u, false)) {
                    m_rttDllPollError = m_lastError;
                    (void)ensureJlinkTargetRunning();
                    return {};
                }
                if (raw.isEmpty()) {
                    break;
                }
                combinedRaw.append(raw);
                if (raw.size() < 512) {
                    break;
                }
            }
            if (!ensureJlinkTargetRunning()) {
                m_rttDllPollError = m_lastError;
                return {};
            }
            return combinedRaw;
        });
        m_rttDllPollWatcher.setFuture(future);
        return;
    }

    if (m_rttSocket.state() == QAbstractSocket::ConnectedState) {
        if (m_rttSocket.bytesAvailable() > 0) {
            handleRttSocketReadyRead();
        }
        return;
    }

    reportRttPollFailure(QStringLiteral("RTT socket 已断开: %1").arg(m_rttSocket.errorString()));
#endif
}

void WindowsSerialTransport::appendRttServerLog(const QByteArray &bytes)
{
#ifdef Q_OS_WIN
    const QString text = sanitizeProcessOutput(bytes);
    if (text.isEmpty()) {
        return;
    }

    if (!m_rttServerLog.isEmpty()) {
        m_rttServerLog.append(QLatin1Char('\n'));
    }
    m_rttServerLog.append(text);
#endif
}

void WindowsSerialTransport::handleRttSocketReadyRead()
{
    if (m_rttSocket.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    const QByteArray incoming = m_rttSocket.readAll();
    m_rttSocketBuffer.append(incoming);
    if (!incoming.isEmpty()) {
        emit rttTextReceived(QStringLiteral("[RTT SOCKET RAW] %1").arg(QString::fromLatin1(incoming.toHex(' ').toUpper())));
    }
    drainRttSocketBuffer();
}

void WindowsSerialTransport::handleRttSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError)
    if (!m_rttOpen) {
        return;
    }
    reportRttPollFailure(QStringLiteral("RTT 本地 socket 异常: %1").arg(m_rttSocket.errorString()));
}

void WindowsSerialTransport::handleRttServerOutput()
{
    const QByteArray bytes = m_rttServerProcess.readAllStandardOutput();
    if (!bytes.isEmpty()) {
        appendRttServerLog(bytes);
    }
}

void WindowsSerialTransport::handleRttServerErrorOutput()
{
    const QByteArray bytes = m_rttServerProcess.readAllStandardError();
    if (!bytes.isEmpty()) {
        appendRttServerLog(bytes);
    }
}

void WindowsSerialTransport::handleRttServerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_rttOpen && !m_rttServerStartedByHost) {
        return;
    }
    if (m_rttServerPid > 0) {
        return;
    }
    QString detail = QStringLiteral("J-Link GDB Server 已退出: exitCode=%1 status=%2")
                         .arg(exitCode)
                         .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash"));
    if (!m_rttServerLogFilePath.isEmpty()) {
        QFile logFile(m_rttServerLogFilePath);
        if (logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString fileLog = sanitizeProcessOutput(logFile.readAll());
            if (!fileLog.isEmpty()) {
                appendRttServerLog(fileLog.toLocal8Bit());
            }
        }
        detail.append(QStringLiteral(" | 日志: %1").arg(QDir::toNativeSeparators(m_rttServerLogFilePath)));
    }
    if (!m_rttServerLog.isEmpty()) {
        detail.append(QStringLiteral(" | 输出: %1").arg(m_rttServerLog));
    }
    m_lastError = detail;
    emit errorOccurred(m_lastError);
    close();
}

void WindowsSerialTransport::handleDllPollFinished()
{
#ifdef Q_OS_WIN
    const QByteArray raw = m_rttDllPollWatcher.result();
    m_rttDllPollActive = false;
    if (!m_rttOpen || !m_rttUsingDll) {
        return;
    }
    if (!m_rttDllPollError.isEmpty()) {
        reportRttPollFailure(m_rttDllPollError);
        m_rttDllPollError.clear();
        return;
    }
    if (!raw.isEmpty()) {
        m_rttSocketBuffer.append(raw);
        drainRttSocketBuffer();
    } else {
        emit rttTextReceived(QStringLiteral("[RTT DLL] poll raw empty"));
    }
#endif
}

void WindowsSerialTransport::drainRttSocketBuffer()
{
#ifdef Q_OS_WIN
    while (true) {
        const int newlineIndex = m_rttSocketBuffer.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QByteArray line = m_rttSocketBuffer.left(newlineIndex);
        m_rttSocketBuffer.remove(0, newlineIndex + 1);
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line == QByteArrayLiteral("RTT_READY")) {
            m_rttReadySeen = true;
        }
        if (!isHexTransportLine(line)) {
            emit rttTextReceived(QString::fromLatin1(line));
            continue;
        }

        const QByteArray decoded = QByteArray::fromHex(line);
        if (!decoded.isEmpty()) {
            m_rttProtocolSeen = true;
            resetRttPollFailure();
            emit bytesReceived(decoded);
        } else {
            emit rttTextReceived(QStringLiteral("[RTT HEX EMPTY] %1").arg(QString::fromLatin1(line)));
        }
    }
#endif
}

bool WindowsSerialTransport::waitForDllRttReady(qint64 deadlineMs)
{
#ifdef Q_OS_WIN
    QString lastTransientError;
    while (QDateTime::currentMSecsSinceEpoch() < deadlineMs) {
        if (!initializeJlinkRtt()) {
            return false;
        }
        if (!ensureJlinkTargetHalted()) {
            lastTransientError = m_lastError;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(20);
            continue;
        }

        QByteArray combinedRaw;
        for (int pass = 0; pass < 8; ++pass) {
            QByteArray raw;
            if (!readRttUpBuffer(kJlinkRttControlBufferIndex, &raw, 512u, false)) {
                lastTransientError = m_lastError;
                raw.clear();
                break;
            }
            if (raw.isEmpty()) {
                break;
            }
            combinedRaw.append(raw);
            if (raw.size() < 512) {
                break;
            }
        }
        (void)ensureJlinkTargetRunning();
        if (!combinedRaw.isEmpty()) {
            m_rttSocketBuffer.append(combinedRaw);
            drainRttSocketBuffer();
            if (m_rttReadySeen) {
                return true;
            }
        }

        // The firmware emits RTT_READY only once at startup. If we attach after that point,
        // a valid control block plus sane channel-0 descriptors is enough to start normal RTT I/O.
        JlinkRttBufferDescriptor upDescriptor;
        JlinkRttBufferDescriptor downDescriptor;
        if (readRttBufferDescriptor(kJlinkRttControlBufferIndex, true, &upDescriptor, nullptr) &&
            readRttBufferDescriptor(kJlinkRttControlBufferIndex, false, &downDescriptor, nullptr)) {
            return true;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }

    m_lastError = lastTransientError.isEmpty()
        ? QStringLiteral("DLL RTT 已连接，但在 %1ms 内未看到 RTT_READY").arg(kJlinkRttReadyWaitMs)
        : QStringLiteral("DLL RTT 等待 RTT_READY 超时，最后一次暂态错误: %1").arg(lastTransientError);
    return false;
#else
    Q_UNUSED(deadlineMs)
    return false;
#endif
}

bool WindowsSerialTransport::isHexTransportLine(const QByteArray &line)
{
    if (line.isEmpty() || (line.size() % 2) != 0) {
        return false;
    }
    for (const char ch : line) {
        const bool hex = (ch >= '0' && ch <= '9') ||
                         (ch >= 'A' && ch <= 'F') ||
                         (ch >= 'a' && ch <= 'f');
        if (!hex) {
            return false;
        }
    }
    return true;
}

bool WindowsSerialTransport::hasRttReadyMarker(const QByteArray &buffer)
{
    return buffer.contains("RTT_READY");
}

QByteArray WindowsSerialTransport::encodeRttTransportFrame(const QByteArray &bytes)
{
    return toHexLine(bytes);
}

#ifdef Q_OS_WIN
bool WindowsSerialTransport::loadJlinkDll()
{
    if (m_jlink.isLoaded()) {
        return true;
    }

    m_jlink.setFileName(QStringLiteral("C:/Program Files/SEGGER/JLink/JLink_x64.dll"));
    if (!m_jlink.load()) {
        m_lastError = QStringLiteral("加载 J-Link x64 DLL 失败: %1").arg(m_jlink.errorString());
        return false;
    }

    m_jlinkOpen = reinterpret_cast<JlinkOpenFn>(m_jlink.resolve("JLINKARM_Open"));
    m_jlinkClose = reinterpret_cast<JlinkCloseFn>(m_jlink.resolve("JLINKARM_Close"));
    m_jlinkTifSelect = reinterpret_cast<JlinkTifSelectFn>(m_jlink.resolve("JLINKARM_TIF_Select"));
    m_jlinkSetSpeed = reinterpret_cast<JlinkSetSpeedFn>(m_jlink.resolve("JLINKARM_SetSpeed"));
    m_jlinkExecCommand = reinterpret_cast<JlinkExecCommandFn>(m_jlink.resolve("JLINKARM_ExecCommand"));
    m_jlinkConnect = reinterpret_cast<JlinkConnectFn>(m_jlink.resolve("JLINKARM_Connect"));
    m_jlinkIsConnected = reinterpret_cast<JlinkIsConnectedFn>(m_jlink.resolve("JLINKARM_IsConnected"));
    m_jlinkHalt = reinterpret_cast<JlinkHaltFn>(m_jlink.resolve("JLINKARM_Halt"));
    m_jlinkGo = reinterpret_cast<JlinkGoFn>(m_jlink.resolve("JLINKARM_Go"));
    m_jlinkIsHalted = reinterpret_cast<JlinkIsHaltedFn>(m_jlink.resolve("JLINKARM_IsHalted"));
    m_jlinkReset = reinterpret_cast<JlinkResetFn>(m_jlink.resolve("JLINKARM_Reset"));
    m_jlinkReadMem = reinterpret_cast<JlinkReadMemFn>(m_jlink.resolve("JLINKARM_ReadMem"));
    m_jlinkReadMemU32 = reinterpret_cast<JlinkReadMemU32Fn>(m_jlink.resolve("JLINKARM_ReadMemU32"));
    m_jlinkReadMemU8 = reinterpret_cast<JlinkReadMemU8Fn>(m_jlink.resolve("JLINKARM_ReadMemU8"));
    m_jlinkWriteMem = reinterpret_cast<JlinkWriteMemFn>(m_jlink.resolve("JLINKARM_WriteMem"));
    m_jlinkWriteU32 = reinterpret_cast<JlinkWriteU32Fn>(m_jlink.resolve("JLINKARM_WriteU32"));
    m_jlinkRttControl = reinterpret_cast<JlinkRttControlFn>(m_jlink.resolve("JLINK_RTTERMINAL_Control"));
    m_jlinkRttRead = reinterpret_cast<JlinkRttReadFn>(m_jlink.resolve("JLINK_RTTERMINAL_Read"));
    m_jlinkRttWrite = reinterpret_cast<JlinkRttWriteFn>(m_jlink.resolve("JLINK_RTTERMINAL_Write"));

    if (!m_jlinkOpen || !m_jlinkClose || !m_jlinkTifSelect || !m_jlinkSetSpeed ||
        !m_jlinkExecCommand || !m_jlinkConnect || !m_jlinkReadMem || !m_jlinkWriteMem ||
        !m_jlinkWriteU32 || !m_jlinkRttControl || !m_jlinkRttRead ||
        !m_jlinkGo) {
        m_lastError = QStringLiteral("J-Link x64 DLL RTT 接口不完整");
        return false;
    }

    return true;
}

void WindowsSerialTransport::closeJlinkSession()
{
    if (m_rttTerminalStarted && m_jlinkRttControl) {
        (void)m_jlinkRttControl(kJlinkRttTerminalStop, nullptr);
        m_rttTerminalStarted = false;
    }
    if (m_jlinkSessionOpen && m_jlinkClose) {
        m_jlinkClose();
    }
    m_jlinkSessionOpen = false;
}

bool WindowsSerialTransport::ensureJlinkTargetHalted()
{
    if (!m_jlinkHalt || !m_jlinkIsHalted) {
        return true;
    }
    if (isJlinkTargetHaltedState(m_jlinkIsHalted())) {
        return true;
    }

    const unsigned char rc = m_jlinkHalt();
    const int haltedState = m_jlinkIsHalted();
    if (!isJlinkTargetHaltedState(haltedState)) {
        m_lastError = QStringLiteral("J-Link 暂停目标后仍未进入 halt 状态");
        return false;
    }
    Q_UNUSED(rc)
    return true;
}

bool WindowsSerialTransport::ensureJlinkTargetRunning()
{
    if (!m_jlinkIsHalted || !m_jlinkGo) {
        return true;
    }
    if (!isJlinkTargetHaltedState(m_jlinkIsHalted())) {
        return true;
    }

    const int rc = m_jlinkGo();
    if (rc < 0) {
        m_lastError = QStringLiteral("J-Link 继续运行目标失败: rc=%1").arg(rc);
        return false;
    }
    return true;
}

bool WindowsSerialTransport::initializeJlinkRtt()
{
    if (!m_jlinkOpen || !m_jlinkExecCommand || !m_jlinkTifSelect || !m_jlinkSetSpeed || !m_jlinkConnect) {
        m_lastError = QStringLiteral("J-Link RTT 初始化接口未就绪");
        return false;
    }

    if (m_jlinkSessionOpen) {
        return true;
    }

    const char *openResult = m_jlinkOpen();
    if (openResult != nullptr && openResult[0] != '\0') {
        m_lastError = QStringLiteral("打开 J-Link 失败: %1")
                          .arg(QString::fromLocal8Bit(openResult));
        return false;
    }
    m_jlinkSessionOpen = true;

    if (m_jlinkTifSelect(kJlinkTifSwd) < 0) {
        m_lastError = QStringLiteral("选择 SWD 失败");
        closeJlinkSession();
        return false;
    }
    m_jlinkSetSpeed(kJlinkSwdSpeedKhz);

    char execError[128] = {};
    if (m_jlinkExecCommand("Device = STM32F103ZE", execError, sizeof(execError)) < 0) {
        m_lastError = QStringLiteral("设置目标器件失败: %1")
                          .arg(QString::fromLocal8Bit(execError).trimmed());
        closeJlinkSession();
        return false;
    }
    if (m_jlinkExecCommand("SetRTTAddr 0x20000078", execError, sizeof(execError)) < 0) {
        m_lastError = QStringLiteral("设置 RTT 地址失败: %1")
                          .arg(QString::fromLocal8Bit(execError).trimmed());
        closeJlinkSession();
        return false;
    }
    if (m_jlinkConnect() < 0) {
        m_lastError = QStringLiteral("连接目标失败");
        closeJlinkSession();
        return false;
    }
    if (m_jlinkGo && m_jlinkIsHalted && isJlinkTargetHaltedState(m_jlinkIsHalted())) {
        const int goRc = m_jlinkGo();
        if (goRc < 0) {
            m_lastError = QStringLiteral("连接后恢复目标运行失败: rc=%1").arg(goRc);
            closeJlinkSession();
            return false;
        }
    }

    // DLL 直读模式直接访问 RTT 控制块和环形缓冲区，不能再启动 RTTERMINAL，
    // 否则 J-Link 自己会先消费 target->host 的上行数据，导致主机读到空。
    m_rttTerminalStarted = false;
    m_rttControlBlockAddress = 0u;
    m_rttUpDescriptorAddress = 0u;
    m_rttDownDescriptorAddress = 0u;
    return true;
}

bool WindowsSerialTransport::readRttMemory(uint32_t address, void *data, uint32_t size)
{
    if (size == 0u) {
        return true;
    }
    if (data == nullptr || !m_jlinkReadMem) {
        m_lastError = QStringLiteral("J-Link 内存读取接口未就绪");
        return false;
    }

    const int rc = m_jlinkReadMem(address, size, data);
    if (isSuccessfulJlinkTransferResult(rc, size)) {
        return true;
    }

    m_lastError = QStringLiteral("J-Link 内存读取失败: addr=0x%1 size=%2 rc=%3")
                      .arg(address, 8, 16, QLatin1Char('0'))
                      .arg(size)
                      .arg(rc);
    return false;
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
    if (!isSuccessfulJlinkTransferResult(rc, size)) {
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
    if (m_jlinkWriteU32) {
        const int rc = m_jlinkWriteU32(address, value);
        if (rc >= 0) {
            return true;
        }

        const QString directWriteError = QStringLiteral("J-Link 写入 U32 失败: addr=0x%1 value=%2 rc=%3")
                                             .arg(address, 8, 16, QLatin1Char('0'))
                                             .arg(value)
                                             .arg(rc);
        if (writeRttMemory(address, &value, sizeof(value))) {
            return true;
        }
        m_lastError = directWriteError + QStringLiteral("; 回退内存写也失败: %1").arg(m_lastError);
        return false;
    }

    if (writeRttMemory(address, &value, sizeof(value))) {
        return true;
    }

    m_lastError = QStringLiteral("J-Link 32 位写入接口未就绪，且回退内存写失败: addr=0x%1 value=%2; %3")
                      .arg(address, 8, 16, QLatin1Char('0'))
                      .arg(value)
                      .arg(m_lastError);
    return false;
}

bool WindowsSerialTransport::locateRttControlBlock(uint32_t *address)
{
    if (address == nullptr) {
        m_lastError = QStringLiteral("RTT 控制块输出参数错误");
        return false;
    }

    const QByteArray needle = QByteArrayLiteral("SEGGER RTT");
    QByteArray preferred(16, '\0');
    if (readRttMemory(kSeggerRttControlBlockAddress,
                      preferred.data(),
                      static_cast<uint32_t>(preferred.size())) &&
        preferred.startsWith(needle)) {
        *address = kSeggerRttControlBlockAddress;
        return true;
    }
    QByteArray legacy(16, '\0');
    if (readRttMemory(kSeggerRttLegacyPreferredControlBlockAddress,
                      legacy.data(),
                      static_cast<uint32_t>(legacy.size())) &&
        legacy.startsWith(needle)) {
        *address = kSeggerRttLegacyPreferredControlBlockAddress;
        return true;
    }
    QByteArray legacyFallback(16, '\0');
    if (readRttMemory(kSeggerRttLegacyFallbackControlBlockAddress,
                      legacyFallback.data(),
                      static_cast<uint32_t>(legacyFallback.size())) &&
        legacyFallback.startsWith(needle)) {
        *address = kSeggerRttLegacyFallbackControlBlockAddress;
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
    const QString legacyFallbackHex = QString::fromLatin1(legacyFallback.toHex(' '));
    m_lastError = QStringLiteral("未在 STM32 SRAM 中找到 SEGGER RTT 控制块，当前 map 地址 0x%1 数据=%2；兼容地址 0x%3 数据=%4；旧地址 0x%5 数据=%6。已按 SWD %7kHz 放行/复位重试，请确认已烧录最新 RTT 固件、BOOT0=0、目标板供电正常且芯片未开启读保护")
                      .arg(kSeggerRttControlBlockAddress, 8, 16, QLatin1Char('0'))
                      .arg(hex.isEmpty() ? QStringLiteral("--") : hex)
                      .arg(kSeggerRttLegacyPreferredControlBlockAddress, 8, 16, QLatin1Char('0'))
                      .arg(legacyHex.isEmpty() ? QStringLiteral("--") : legacyHex)
                      .arg(kSeggerRttLegacyFallbackControlBlockAddress, 8, 16, QLatin1Char('0'))
                      .arg(legacyFallbackHex.isEmpty() ? QStringLiteral("--") : legacyFallbackHex)
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

    auto isValidDescriptor = [](const JlinkRttBufferDescriptor &desc) -> bool {
        return isSramAddressRange(desc.bufferAddress, desc.sizeOfBuffer) &&
               desc.sizeOfBuffer >= 2u &&
               desc.writeOffset < desc.sizeOfBuffer &&
               desc.readOffset < desc.sizeOfBuffer;
    };

    const uint32_t cachedDescriptorAddress = upBuffer ? m_rttUpDescriptorAddress : m_rttDownDescriptorAddress;
    if (bufferIndex == kJlinkRttControlBufferIndex && cachedDescriptorAddress != 0u) {
        for (int attempt = 0; attempt < kRttDescriptorRetryAttempts; ++attempt) {
            if (readRttMemory(cachedDescriptorAddress, descriptor, sizeof(*descriptor))) {
                if (!isValidDescriptor(*descriptor)) {
                    Sleep(kRttLocateRetryDelayMs);
                    continue;
                }
                if (descriptorAddress != nullptr) {
                    *descriptorAddress = cachedDescriptorAddress;
                }
                return true;
            }
            Sleep(kRttLocateRetryDelayMs);
        }
    }

    if (bufferIndex == kJlinkRttControlBufferIndex && m_rttControlBlockAddress != 0u) {
        uint32_t maxUpBuffers = 0u;
        uint32_t maxDownBuffers = 0u;
        if (!readRttMemory(m_rttControlBlockAddress + kSeggerRttMaxUpOffset, &maxUpBuffers, sizeof(maxUpBuffers)) ||
            !readRttMemory(m_rttControlBlockAddress + kSeggerRttMaxDownOffset, &maxDownBuffers, sizeof(maxDownBuffers))) {
            return false;
        }
        if (maxUpBuffers == 0u || maxUpBuffers > 16u || maxDownBuffers == 0u || maxDownBuffers > 16u) {
            m_lastError = QStringLiteral("RTT 控制块 buffer 数异常: cb=0x%1 maxUp=%2 maxDown=%3")
                              .arg(m_rttControlBlockAddress, 8, 16, QLatin1Char('0'))
                              .arg(maxUpBuffers)
                              .arg(maxDownBuffers);
            return false;
        }

        const uint32_t firstDescriptor = m_rttControlBlockAddress + kSeggerRttDescriptorBaseOffset;
        const uint32_t offsetIndex = upBuffer ? bufferIndex : (maxUpBuffers + bufferIndex);
        const uint32_t directAddress = firstDescriptor + (offsetIndex * kSeggerRttBufferDescriptorSize);
        for (int attempt = 0; attempt < kRttDescriptorRetryAttempts; ++attempt) {
            if (readRttMemory(directAddress, descriptor, sizeof(*descriptor))) {
                if (!isValidDescriptor(*descriptor)) {
                    Sleep(kRttLocateRetryDelayMs);
                    continue;
                }
                if (upBuffer) {
                    m_rttUpDescriptorAddress = directAddress;
                    if (m_rttDownDescriptorAddress == 0u) {
                        m_rttDownDescriptorAddress = firstDescriptor +
                            ((maxUpBuffers + bufferIndex) * kSeggerRttBufferDescriptorSize);
                    }
                } else {
                    m_rttDownDescriptorAddress = directAddress;
                    if (m_rttUpDescriptorAddress == 0u) {
                        m_rttUpDescriptorAddress = firstDescriptor +
                            (bufferIndex * kSeggerRttBufferDescriptorSize);
                    }
                }
                if (descriptorAddress != nullptr) {
                    *descriptorAddress = directAddress;
                }
                return true;
            }
            Sleep(kRttLocateRetryDelayMs);
        }
        m_lastError = QStringLiteral("RTT 控制通道描述符未就绪: up=%1 addr=0x%2")
                          .arg(upBuffer ? 1 : 0)
                          .arg(directAddress, 8, 16, QLatin1Char('0'));
        return false;
    }

    for (int attempt = 0; attempt < kRttDescriptorRetryAttempts; ++attempt) {
        uint32_t maxUpBuffers = 0;
        uint32_t maxDownBuffers = 0;
        if (m_rttControlBlockAddress == 0u) {
            m_lastError = QStringLiteral("RTT 控制块地址未初始化");
            return false;
        }
        if (!readRttMemory(m_rttControlBlockAddress + kSeggerRttMaxUpOffset, &maxUpBuffers, sizeof(maxUpBuffers)) ||
            !readRttMemory(m_rttControlBlockAddress + kSeggerRttMaxDownOffset, &maxDownBuffers, sizeof(maxDownBuffers))) {
            Sleep(kRttLocateRetryDelayMs);
            continue;
        }

        const uint32_t maxBuffers = upBuffer ? maxUpBuffers : maxDownBuffers;
        if (maxUpBuffers == 0u || maxUpBuffers > 16u || maxDownBuffers == 0u || maxDownBuffers > 16u ||
            bufferIndex >= maxBuffers) {
            m_lastError = QStringLiteral("RTT buffer 索引无效: index=%1 up=%2 maxUp=%3 maxDown=%4")
                              .arg(bufferIndex)
                              .arg(upBuffer ? 1 : 0)
                              .arg(maxUpBuffers)
                              .arg(maxDownBuffers);
            Sleep(kRttLocateRetryDelayMs);
            continue;
        }

        const uint32_t firstDescriptor = m_rttControlBlockAddress + kSeggerRttDescriptorBaseOffset;
        const uint32_t offsetIndex = upBuffer ? bufferIndex : (maxUpBuffers + bufferIndex);
        const uint32_t address = firstDescriptor + (offsetIndex * kSeggerRttBufferDescriptorSize);
        if (!readRttMemory(address, descriptor, sizeof(*descriptor))) {
            Sleep(kRttLocateRetryDelayMs);
            continue;
        }
        if (bufferIndex == kJlinkRttControlBufferIndex) {
            if (upBuffer) {
                m_rttUpDescriptorAddress = address;
                if (maxUpBuffers > 0u && m_rttDownDescriptorAddress == 0u) {
                    m_rttDownDescriptorAddress = firstDescriptor + ((maxUpBuffers + bufferIndex) * kSeggerRttBufferDescriptorSize);
                }
            } else {
                m_rttDownDescriptorAddress = address;
                if (maxUpBuffers > 0u && m_rttUpDescriptorAddress == 0u) {
                    m_rttUpDescriptorAddress = firstDescriptor + (bufferIndex * kSeggerRttBufferDescriptorSize);
                }
            }
        }
        if (descriptorAddress != nullptr) {
            *descriptorAddress = address;
        }
        return true;
    }
    return false;
}

bool WindowsSerialTransport::readRttUpBuffer(unsigned bufferIndex, QByteArray *bytes, uint32_t maxBytes, bool emitErrors)
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
        if (emitErrors) {
            emit errorOccurred(m_lastError);
        }
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
        if (emitErrors) {
            emit errorOccurred(m_lastError);
        }
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
        if (emitErrors) {
            emit errorOccurred(m_lastError);
        }
        bytes->clear();
        return false;
    }
    if (firstChunk < bytesToRead) {
        if (!readRttMemory(descriptor.bufferAddress,
                           bytes->data() + firstChunk,
                           bytesToRead - firstChunk)) {
            if (emitErrors) {
                emit errorOccurred(m_lastError);
            }
            bytes->clear();
            return false;
        }
    }

    const uint32_t newReadOffset = (descriptor.readOffset + bytesToRead) % descriptor.sizeOfBuffer;
    if (!writeRttU32(descriptorAddress + kSeggerRttDescriptorReadOffset, newReadOffset)) {
        if (emitErrors) {
            emit errorOccurred(m_lastError);
        }
        bytes->clear();
        return false;
    }
    return true;
}

void WindowsSerialTransport::reportRttPollFailure(const QString &message)
{
    ++m_rttPollFailureCount;
    if (m_rttPollFailureCount == 1 || (m_rttPollFailureCount % kRttPollFailureWarnEvery) == 0) {
        emit errorOccurred(QStringLiteral("%1。J-Link RTT 轮询已连续失败 %2 次")
                               .arg(message)
                               .arg(m_rttPollFailureCount));
    }
    if (m_rttPollFailureCount >= kRttPollFailureCloseAfter) {
        emit errorOccurred(QStringLiteral("J-Link RTT 连续读取失败，已停止本次连接。请断开后重新连接，必要时重新插拔 J-Link/复位板子"));
        close();
    }
}

void WindowsSerialTransport::resetRttPollFailure()
{
    m_rttPollFailureCount = 0;
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
