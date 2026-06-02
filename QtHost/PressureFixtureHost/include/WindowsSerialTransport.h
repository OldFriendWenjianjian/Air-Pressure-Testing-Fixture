#pragma once

#include <QObject>
#include <QTimer>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class WindowsSerialTransport : public QObject {
    Q_OBJECT

public:
    explicit WindowsSerialTransport(QObject *parent = nullptr);
    ~WindowsSerialTransport() override;

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
    QString m_portName;
    QString m_lastError;
    QTimer m_pollTimer;

#ifdef Q_OS_WIN
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#endif
};
