#include "WindowsSerialTransport.h"

#include <QByteArray>

WindowsSerialTransport::WindowsSerialTransport(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(20);
    connect(&m_pollTimer, &QTimer::timeout, this, &WindowsSerialTransport::pollRead);
}

WindowsSerialTransport::~WindowsSerialTransport()
{
    close();
}

QStringList WindowsSerialTransport::availablePorts()
{
    QStringList ports;
#ifdef Q_OS_WIN
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
            ports << name;
            CloseHandle(h);
        }
    }
#endif
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
    return m_handle != INVALID_HANDLE_VALUE;
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
