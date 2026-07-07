#include "UsbControlProtocol.h"
#include "WindowsSerialTransport.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDebug>
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

    WindowsSerialTransport transport;
    QByteArray rxBuffer;
    bool gotHello = false;
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
            std::fprintf(stdout, "RX frame cmd=%u seq=%u\n",
                         static_cast<unsigned>(parsed.frame.command),
                         static_cast<unsigned>(parsed.frame.sequence));
            if (parsed.frame.command == usb::Hello) {
                gotHello = true;
                app.quit();
                return;
            }
        }
    });

    QObject::connect(&transport, &WindowsSerialTransport::rttTextReceived, [&](const QString &text) {
        std::fprintf(stdout, "RTT text: %s\n", text.toLocal8Bit().constData());
    });

    QObject::connect(&transport, &WindowsSerialTransport::errorOccurred, [&](const QString &message) {
        errors.push_back(message);
        std::fprintf(stderr, "ERRSIG: %s\n", message.toLocal8Bit().constData());
    });

    std::fprintf(stdout, "stage: before open\n");
    if (!transport.open(QStringLiteral("RTT:JLINK"), 115200)) {
        std::fprintf(stderr, "open failed: %s\n", transport.lastError().toLocal8Bit().constData());
        return 2;
    }
    std::fprintf(stdout, "open ok\n");

    const QByteArray hello = usb::buildHello(1);
    std::fprintf(stdout, "stage: before write\n");
    if (!transport.writeBytes(hello)) {
        std::fprintf(stderr, "write failed: %s\n", transport.lastError().toLocal8Bit().constData());
        transport.close();
        return 3;
    }
    std::fprintf(stdout, "write ok\n");

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000 && !gotHello) {
        std::fprintf(stdout, "stage: waiting %lld\n", static_cast<long long>(timer.elapsed()));
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(100);
    }

    std::fprintf(stdout, "stage: before close\n");
    transport.close();
    std::fprintf(stdout, "stage: after close\n");

    if (!gotHello) {
        std::fprintf(stderr, "HELLO timeout\n");
        for (const auto &message : errors) {
            std::fprintf(stderr, "ERR: %s\n", message.toLocal8Bit().constData());
        }
        return 4;
    }

    std::fprintf(stdout, "HELLO ok\n");
    return 0;
}
