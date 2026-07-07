#include "UsbControlProtocol.h"
#include "WindowsSerialTransport.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <cstdio>

using namespace fixture;

static void dumpBytesHex(const QByteArray &bytes)
{
    std::fprintf(stdout, "RX bytes len=%d hex=%s\n",
                 bytes.size(),
                 bytes.toHex().toUpper().constData());
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    const QString portName = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("COM5");

    WindowsSerialTransport transport;
    QByteArray rxBuffer;
    bool gotHello = false;
    bool gotStatus = false;
    QStringList errors;

    QObject::connect(&transport, &WindowsSerialTransport::bytesReceived, [&](const QByteArray &bytes) {
        dumpBytesHex(bytes);
        rxBuffer.append(bytes);
        while (!rxBuffer.isEmpty()) {
            const auto parsed = usb::parseOne(rxBuffer);
            if (parsed.needMore) {
                break;
            }
            if (parsed.consumed > 0) {
                rxBuffer.remove(0, static_cast<int>(parsed.consumed));
            }
            if (!parsed.ok) {
                std::fprintf(stderr, "RX discard: %s\n", parsed.error.toLocal8Bit().constData());
                continue;
            }
            std::fprintf(stdout, "RX frame cmd=%u seq=%u len=%d\n",
                         static_cast<unsigned>(parsed.frame.command),
                         static_cast<unsigned>(parsed.frame.sequence),
                         parsed.frame.payload.size());
            if (parsed.frame.command == usb::Hello) {
                gotHello = true;
            } else if (parsed.frame.command == usb::StatusSnapshot) {
                gotStatus = true;
            }
            if (gotHello && gotStatus) {
                app.quit();
                return;
            }
        }
    });

    QObject::connect(&transport, &WindowsSerialTransport::errorOccurred, [&](const QString &message) {
        errors.push_back(message);
        std::fprintf(stderr, "ERRSIG: %s\n", message.toLocal8Bit().constData());
    });

    std::fprintf(stdout, "stage: before open %s\n", portName.toLocal8Bit().constData());
    if (!transport.open(portName, 115200)) {
        std::fprintf(stderr, "open failed: %s\n", transport.lastError().toLocal8Bit().constData());
        return 2;
    }
    std::fprintf(stdout, "open ok\n");

    QElapsedTimer warmupTimer;
    warmupTimer.start();
    while (warmupTimer.elapsed() < 1000) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(50);
    }

    const QByteArray hello = usb::buildHello(1);
    std::fprintf(stdout, "stage: before write HELLO\n");
    if (!transport.writeBytes(hello)) {
        std::fprintf(stderr, "write failed: %s\n", transport.lastError().toLocal8Bit().constData());
        transport.close();
        return 3;
    }
    std::fprintf(stdout, "write ok\n");

    const QByteArray getStatus = usb::buildFrame(usb::Request, 2, usb::GetStatus);
    std::fprintf(stdout, "stage: before write GET_STATUS\n");
    if (!transport.writeBytes(getStatus)) {
        std::fprintf(stderr, "write get status failed: %s\n", transport.lastError().toLocal8Bit().constData());
        transport.close();
        return 4;
    }
    std::fprintf(stdout, "write get status ok\n");

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !(gotHello && gotStatus)) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(100);
    }

    transport.close();

    if (!(gotHello && gotStatus)) {
        std::fprintf(stderr, "serial smoke timeout: hello=%d status=%d\n", gotHello ? 1 : 0, gotStatus ? 1 : 0);
        for (const auto &message : errors) {
            std::fprintf(stderr, "ERR: %s\n", message.toLocal8Bit().constData());
        }
        return 5;
    }

    std::fprintf(stdout, "serial smoke ok\n");
    return 0;
}
