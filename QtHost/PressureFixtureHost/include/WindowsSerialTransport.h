#pragma once

#include <QObject>
#include <QProcess>
#include <QFutureWatcher>
#include <QTcpSocket>
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
        QString productName;
        QString serialNumber;
        QString instanceId;
        bool isFixtureUsbCdc = false;
        bool isSegger = false;
        bool isRtt = false;
    };

    explicit WindowsSerialTransport(QObject *parent = nullptr);
    ~WindowsSerialTransport() override;

    static QVector<PortInfo> availablePortInfos();
    static QStringList availablePorts();
    static bool isFixtureUsbIdentity(const QString &instanceId,
                                     const QString &productName,
                                     const QString &serialNumber);
    bool open(const QString &portName, int baudRate = 115200);
    void close();
    bool isOpen() const;
    QString portName() const;
    QString lastError() const;
    bool writeBytes(const QByteArray &bytes);

signals:
    void bytesReceived(const QByteArray &bytes);
    void rttTextReceived(const QString &text);
    void errorOccurred(const QString &message);
    void openChanged(bool open);

private slots:
    void pollRead();
    void handleRttSocketReadyRead();
    void handleRttSocketError(QAbstractSocket::SocketError socketError);
    void handleRttServerOutput();
    void handleRttServerErrorOutput();
    void handleRttServerFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleDllPollFinished();

private:
    bool openSerial(const QString &portName, int baudRate);
    bool openRtt();
    bool openRttViaJlinkDll();
    bool connectToRttSocket(quint16 port, int timeoutMs);
    bool waitForRttReady(qint64 deadlineMs);
    bool isStartedRttServerAlive() const;
    void closeRtt();
    bool writeRtt(const QByteArray &bytes);
    void pollRtt();
    void drainRttSocketBuffer();
    void appendRttServerLog(const QByteArray &bytes);
    static bool isHexTransportLine(const QByteArray &line);
    static bool hasRttReadyMarker(const QByteArray &buffer);
    static QByteArray encodeRttTransportFrame(const QByteArray &bytes);
    bool loadJlinkDll();
    bool initializeJlinkRtt();
    bool ensureJlinkTargetHalted();
    bool ensureJlinkTargetRunning();
    void closeJlinkSession();

    QString m_portName;
    QString m_lastError;
    QTimer m_pollTimer;
    bool m_rttOpen = false;
    QProcess m_rttServerProcess;
    QTcpSocket m_rttSocket;
    QFutureWatcher<QByteArray> m_rttDllPollWatcher;
    QByteArray m_rttSocketBuffer;
    bool m_rttServerStartedByHost = false;
    QString m_rttServerLog;
    QString m_rttServerLogFilePath;
    qint64 m_rttServerPid = 0;

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
    using JlinkHaltFn = unsigned char (*)();
    using JlinkGoFn = int (*)();
    using JlinkIsHaltedFn = int (*)();
    using JlinkResetFn = void (*)();
    using JlinkReadMemFn = int (*)(uint32_t, uint32_t, void *);
    using JlinkReadMemU32Fn = int (*)(uint32_t, uint32_t, uint32_t *, void *);
    using JlinkReadMemU8Fn = int (*)(uint32_t, uint32_t, void *);
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
    JlinkHaltFn m_jlinkHalt = nullptr;
    JlinkGoFn m_jlinkGo = nullptr;
    JlinkIsHaltedFn m_jlinkIsHalted = nullptr;
    JlinkResetFn m_jlinkReset = nullptr;
    JlinkReadMemFn m_jlinkReadMem = nullptr;
    JlinkReadMemU32Fn m_jlinkReadMemU32 = nullptr;
    JlinkReadMemU8Fn m_jlinkReadMemU8 = nullptr;
    JlinkWriteMemFn m_jlinkWriteMem = nullptr;
    JlinkWriteU32Fn m_jlinkWriteU32 = nullptr;
    JlinkRttControlFn m_jlinkRttControl = nullptr;
    JlinkRttReadFn m_jlinkRttRead = nullptr;
    JlinkRttWriteFn m_jlinkRttWrite = nullptr;
    bool m_rttTerminalStarted = false;
    bool m_rttUsingDll = false;
    bool m_jlinkSessionOpen = false;
    qint64 m_jlinkLastCycleAtMs = 0;
    bool m_rttDllPollActive = false;
    QString m_rttDllPollError;
    bool m_rttReadySeen = false;
    bool m_rttProtocolSeen = false;

    bool readRttMemory(uint32_t address, void *data, uint32_t size);
    bool writeRttMemory(uint32_t address, const void *data, uint32_t size);
    bool writeRttU32(uint32_t address, uint32_t value);
    bool locateRttControlBlock(uint32_t *address);
    bool readRttBufferDescriptor(unsigned bufferIndex, bool upBuffer, JlinkRttBufferDescriptor *descriptor, uint32_t *descriptorAddress = nullptr);
    bool readRttUpBuffer(unsigned bufferIndex, QByteArray *bytes, uint32_t maxBytes, bool emitErrors = true);
    bool writeRttDownBuffer(unsigned bufferIndex, const QByteArray &bytes);
    void reportRttPollFailure(const QString &message);
    void resetRttPollFailure();
    bool waitForDllRttReady(qint64 deadlineMs);
    uint32_t m_rttControlBlockAddress = 0;
    uint32_t m_rttUpDescriptorAddress = 0;
    uint32_t m_rttDownDescriptorAddress = 0;
    int m_rttPollFailureCount = 0;
#endif
};
