#pragma once

#include <QObject>
#include <QTimer>
#include <QStringList>
#include <QVector>
#include <cstdint>

#ifdef Q_OS_WIN
#include <QLibrary>
#include <windows.h>
#endif

class WindowsSerialTransport : public QObject {
    Q_OBJECT

public:
    struct PortInfo {
        QString portName;
        QString displayName;
        QString description;
        bool isSegger = false;
        bool isRtt = false;
    };

    explicit WindowsSerialTransport(QObject *parent = nullptr);
    ~WindowsSerialTransport() override;

    static QVector<PortInfo> availablePortInfos();
    static QStringList availablePorts();
    bool open(const QString &portName, int baudRate = 115200);
    void close();
    bool isOpen() const;
    QString portName() const;
    QString lastError() const;
    bool writeBytes(const QByteArray &bytes);

signals:
    void bytesReceived(const QByteArray &bytes);
    void errorOccurred(const QString &message);
    void openChanged(bool open);

private slots:
    void pollRead();

private:
    bool openSerial(const QString &portName, int baudRate);
    bool openRtt();
    void closeRtt();
    bool writeRtt(const QByteArray &bytes);
    void pollRtt();

    QString m_portName;
    QString m_lastError;
    QTimer m_pollTimer;
    bool m_rttOpen = false;

#ifdef Q_OS_WIN
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    QLibrary m_jlink;

    using JlinkOpenFn = const char *(*)();
    using JlinkCloseFn = void (*)();
    using JlinkTifSelectFn = int (*)(int);
    using JlinkSetSpeedFn = void (*)(int);
    using JlinkExecCommandFn = int (*)(const char *, char *, int);
    using JlinkConnectFn = int (*)();
    using JlinkIsConnectedFn = char (*)();
    using JlinkGoFn = void (*)();
    using JlinkResetFn = void (*)();
    using JlinkReadMemFn = int (*)(uint32_t, uint32_t, void *);
    using JlinkWriteMemFn = int (*)(uint32_t, uint32_t, const void *);
    using JlinkWriteU32Fn = int (*)(uint32_t, uint32_t);
    using JlinkRttControlFn = int (*)(int, void *);
    using JlinkRttReadFn = int (*)(unsigned, char *, unsigned);
    using JlinkRttWriteFn = int (*)(unsigned, const char *, unsigned);

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

    struct JlinkRttBufferDescriptor {
        uint32_t nameAddress = 0;
        uint32_t bufferAddress = 0;
        uint32_t sizeOfBuffer = 0;
        uint32_t writeOffset = 0;
        uint32_t readOffset = 0;
        uint32_t flags = 0;
    };

    JlinkOpenFn m_jlinkOpen = nullptr;
    JlinkCloseFn m_jlinkClose = nullptr;
    JlinkTifSelectFn m_jlinkTifSelect = nullptr;
    JlinkSetSpeedFn m_jlinkSetSpeed = nullptr;
    JlinkExecCommandFn m_jlinkExecCommand = nullptr;
    JlinkConnectFn m_jlinkConnect = nullptr;
    JlinkIsConnectedFn m_jlinkIsConnected = nullptr;
    JlinkGoFn m_jlinkGo = nullptr;
    JlinkResetFn m_jlinkReset = nullptr;
    JlinkReadMemFn m_jlinkReadMem = nullptr;
    JlinkWriteMemFn m_jlinkWriteMem = nullptr;
    JlinkWriteU32Fn m_jlinkWriteU32 = nullptr;
    JlinkRttControlFn m_jlinkRttControl = nullptr;
    JlinkRttReadFn m_jlinkRttRead = nullptr;
    JlinkRttWriteFn m_jlinkRttWrite = nullptr;
    bool m_rttTerminalStarted = false;

    bool readRttMemory(uint32_t address, void *data, uint32_t size);
    bool writeRttMemory(uint32_t address, const void *data, uint32_t size);
    bool writeRttU32(uint32_t address, uint32_t value);
    bool locateRttControlBlock(uint32_t *address);
    bool readRttBufferDescriptor(unsigned bufferIndex, bool upBuffer, JlinkRttBufferDescriptor *descriptor, uint32_t *descriptorAddress = nullptr);
    bool readRttUpBuffer(unsigned bufferIndex, QByteArray *bytes, uint32_t maxBytes);
    bool writeRttDownBuffer(unsigned bufferIndex, const QByteArray &bytes);
    uint32_t m_rttControlBlockAddress = 0;
#endif
};
