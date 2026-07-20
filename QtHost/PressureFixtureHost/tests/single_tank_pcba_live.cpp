#include "UsbControlProtocol.h"
#include "WindowsSerialTransport.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <array>
#include <cstdio>

using namespace fixture;

static const std::array<const char *, 23> kStepNames{{
    "待机电流", "开机+进测试", "开机后电流", "读版本配置", "查低电", "记录零点",
    "趋势采样50", "标定50", "标后查询50", "趋势采样150", "标定150", "标后查询150",
    "趋势采样250", "标定250", "标后查询250", "写Flash",
    "趋势采样100", "压力查询100", "趋势采样200", "压力查询200", "趋势采样285", "压力查询285", "关机"
}};

static QString hexText(const std::array<uint8_t, 24> &raw, uint8_t len)
{
    QByteArray bytes;
    const int count = qMin<int>(len, static_cast<int>(raw.size()));
    for (int i = 0; i < count; ++i) {
        bytes.append(static_cast<char>(raw[static_cast<size_t>(i)]));
    }
    return bytes.toHex( ).toUpper();
}

static void pump(QCoreApplication &app, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    const QString portName = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("COM5");
    const bool continueOnFail = QCoreApplication::arguments().contains("--continue-on-fail");
    const bool ventOnly = QCoreApplication::arguments().contains("--vent-only");
    const bool reportOnly = QCoreApplication::arguments().contains("--report-only");
    const bool stopAfterZero = QCoreApplication::arguments().contains("--stop-after-zero");
    const bool timingOnly = QCoreApplication::arguments().contains("--timing-only");
    WindowsSerialTransport transport;
    QByteArray rxBuffer;
    bool helloOk = false;
    bool runAck = false;
    bool timingAck = false;
    bool snapshotReceived = false;
    FixtureSnapshot snapshot;
    SingleTankPcbaReport lastReport;
    PcbaTimingReport timingReport;
    int timingSnapshotCounter = 0;

    QObject::connect(&transport, &WindowsSerialTransport::bytesReceived, [&](const QByteArray &bytes) {
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
                std::fprintf(stderr, "discard: %s\n", parsed.error.toLocal8Bit().constData());
                continue;
            }
            if (parsed.frame.command == usb::Hello) {
                helloOk = true;
                std::fprintf(stdout,
                             "HELLO response seq=%u len=%lld\n",
                             parsed.frame.sequence,
                             static_cast<long long>(parsed.frame.payload.size()));
            } else if (parsed.frame.command == usb::StatusSnapshot) {
                if (usb::applyStatusSnapshot(parsed.frame.payload, snapshot)) {
                    if (timingOnly && (++timingSnapshotCounter % 5) == 0) {
                        const auto &channel = snapshot.channels[0];
                        std::fprintf(stdout,
                                     "TIMING_STATUS state=%u 5V=%d 4.5V=%d 50mA=%d rawADC=%u rawValid=%d standby=%.2fuA work=%.2fuA\n",
                                     static_cast<unsigned>(snapshot.state),
                                     snapshot.pcbaSupply5VEnabled ? 1 : 0,
                                     snapshot.pcbaSupply45VEnabled ? 1 : 0,
                                     snapshot.pcbaCurrent50mAEnabled ? 1 : 0,
                                     static_cast<unsigned>(channel.currentRawAdc),
                                     channel.currentRawAdcValid ? 1 : 0,
                                     channel.standbyCurrentUaX100 / 100.0,
                                     channel.workCurrentUaX100 / 100.0);
                    }
                    if (!snapshotReceived) {
                        std::fprintf(stdout, "PRESSURE_SNAPSHOT\n");
                        for (int sensor = 0; sensor < kPressureSensorCount; ++sensor) {
                            std::fprintf(stdout,
                                         "  P%02d pressure=%.2fmmHg valid=%d latched=%d status=0x%02X fault=%u\n",
                                         sensor + 1,
                                         snapshot.pressure001mmHg[static_cast<size_t>(sensor)] / 1000.0,
                                         snapshot.pressureValid[static_cast<size_t>(sensor)] ? 1 : 0,
                                         snapshot.pressureFaultLatched[static_cast<size_t>(sensor)] ? 1 : 0,
                                         snapshot.pressureStatusByte[static_cast<size_t>(sensor)],
                                         snapshot.pressureFaultCode[static_cast<size_t>(sensor)]);
                        }
                    }
                    snapshotReceived = true;
                }
            } else if (parsed.frame.command == usb::Ack && parsed.frame.payload.size() >= 2) {
                const uint8_t accepted = static_cast<uint8_t>(parsed.frame.payload[0]);
                const uint8_t status = static_cast<uint8_t>(parsed.frame.payload[1]);
                std::fprintf(stdout, "ACK seq=%u accepted=0x%02X status=%u\n", parsed.frame.sequence, accepted, status);
                if (accepted == usb::RunSingleTankPcba && status == 0) {
                    runAck = true;
                } else if (accepted == usb::RunPcbaTiming && status == 0) {
                    timingAck = true;
                }
            } else if (parsed.frame.command == usb::Nak && parsed.frame.payload.size() >= 2) {
                const uint8_t rejected = static_cast<uint8_t>(parsed.frame.payload[0]);
                const uint8_t error = static_cast<uint8_t>(parsed.frame.payload[1]);
                std::fprintf(stderr,
                             "NAK seq=%u rejected=0x%02X error=%u\n",
                             parsed.frame.sequence,
                             rejected,
                             error);
            } else if (parsed.frame.command == usb::GetPcbaTiming) {
                PcbaTimingReport report;
                if (usb::parsePcbaTimingReport(parsed.frame.payload, report)) {
                    timingReport = report;
                    std::fprintf(stdout,
                                 "TIMING_REPORT running=%d done=%d count=%u/10 final=%d\n",
                                 report.running ? 1 : 0,
                                 report.done ? 1 : 0,
                                 static_cast<unsigned>(report.count),
                                 report.finalPass ? 1 : 0);
                }
            } else if (parsed.frame.command == usb::GetSingleTankPcba) {
                SingleTankPcbaReport report;
                if (usb::parseSingleTankPcbaReport(parsed.frame.payload, report)) {
                    lastReport = report;
                    std::fprintf(stdout,
                                 "REPORT running=%d done=%d count=%u/23 final=%d standby=%.2fuA work=%.2fuA\n",
                                 report.running ? 1 : 0,
                                 report.done ? 1 : 0,
                                 static_cast<unsigned>(report.count),
                                 report.finalPass ? 1 : 0,
                                 report.standbyCurrentUaX100 / 100.0,
                                 report.workCurrentUaX100 / 100.0);
                    if (report.count > 0 && report.count <= report.entries.size()) {
                        const auto &e = report.entries[report.count - 1];
                        std::fprintf(stdout,
                                     "  last %02u %s ok=%d kind=%u cmd=0x%02X elapsed=%.3fms parsed=%u raw=%s\n",
                                     static_cast<unsigned>(report.count),
                                     kStepNames[report.count - 1],
                                     e.ok ? 1 : 0,
                                     static_cast<unsigned>(e.kind),
                                     static_cast<unsigned>(e.command),
                                     e.elapsedUs / 1000.0,
                                     e.parsedValue,
                                     hexText(e.rawResponse, e.rawResponseLength).toLocal8Bit().constData());
                    }
                } else {
                    std::fprintf(stderr,
                                 "GET_SINGLE_TANK_PCBA parse failed len=%lld\n",
                                 static_cast<long long>(parsed.frame.payload.size()));
                }
            }
        }
    });

    QObject::connect(&transport, &WindowsSerialTransport::errorOccurred, [&](const QString &message) {
        std::fprintf(stderr, "transport error: %s\n", message.toLocal8Bit().constData());
    });

    if (!transport.open(portName, 115200)) {
        std::fprintf(stderr, "open %s failed: %s\n", portName.toLocal8Bit().constData(), transport.lastError().toLocal8Bit().constData());
        return 2;
    }
    std::fprintf(stdout, "open %s ok\n", portName.toLocal8Bit().constData());
    pump(app, 1000);

    transport.writeBytes(usb::buildHello(1));
    pump(app, 1500);
    if (!helloOk) {
        std::fprintf(stderr, "HELLO timeout\n");
        transport.close();
        return 3;
    }

    transport.writeBytes(usb::buildFrame(usb::Request, 2, usb::GetStatus));
    pump(app, 1500);
    if (!snapshotReceived) {
        std::fprintf(stderr, "GET_STATUS timeout\n");
        transport.close();
        return 4;
    }

    if (timingOnly) {
        uint16_t seq = 3;
        transport.writeBytes(usb::buildRunPcbaTiming(seq++, false));
        pump(app, 500);
        if (!timingAck) {
            std::fprintf(stderr,
                         "RUN_PCBA_TIMING ACK not observed; polling the MCU-owned report\n");
        }
        for (int attempt = 0; attempt < 40 && !timingReport.done; ++attempt) {
            transport.writeBytes(usb::buildGetPcbaTiming(seq++));
            pump(app, 300);
        }
        std::fprintf(stdout, "TIMING_FINAL\n");
        for (uint8_t i = 0u; i < timingReport.count && i < timingReport.entries.size(); ++i) {
            const auto &entry = timingReport.entries[i];
            std::fprintf(stdout,
                         "%02u kind=%u cmd=0x%02X ok=%d response=0x%02X len=%u elapsed=%.3fms raw=%s\n",
                         static_cast<unsigned>(i + 1u),
                         static_cast<unsigned>(entry.kind),
                         static_cast<unsigned>(entry.command),
                         entry.ok ? 1 : 0,
                         static_cast<unsigned>(entry.responseCommandOrByte),
                         static_cast<unsigned>(entry.responseLength),
                         entry.elapsedUs / 1000.0,
                         hexText(entry.rawResponse, entry.rawResponseLength).toLocal8Bit().constData());
        }
        transport.close();
        return timingReport.done && timingReport.count == timingReport.entries.size() ? 0 : 1;
    }

    if (ventOnly) {
        uint16_t seq = 3;
        transport.writeBytes(usb::buildSensorCalibrationEnter(seq++, false, 1));
        pump(app, 1000);
        transport.writeBytes(usb::buildSensorCalibrationSimpleAction(
            seq++, usb::SensorCalibrationStartAutoVent));
        for (int attempt = 0; attempt < 60; ++attempt) {
            pump(app, 500);
            if ((attempt % 4) == 0) {
                std::fprintf(stdout,
                             "VENT P01=%.2fmmHg valid=%d\n",
                             snapshot.pressure001mmHg[0] / 1000.0,
                             snapshot.pressureValid[0] ? 1 : 0);
            }
            if (snapshot.pressureValid[0] && snapshot.pressure001mmHg[0] <= 100) {
                break;
            }
        }
        const bool vented = snapshot.pressureValid[0] && snapshot.pressure001mmHg[0] <= 100;
        transport.writeBytes(usb::buildSensorCalibrationSimpleAction(
            seq++, usb::SensorCalibrationExit));
        pump(app, 1000);
        std::fprintf(stdout,
                     "VENT_RESULT %s P01=%.2fmmHg\n",
                     vented ? "PASS" : "FAIL",
                     snapshot.pressure001mmHg[0] / 1000.0);
        transport.close();
        return vented ? 0 : 1;
    }

    if (!reportOnly) {
        transport.writeBytes(usb::buildRunSingleTankPcba(3, continueOnFail));
        pump(app, 1500);
        if (!runAck) {
            std::fprintf(stderr, "RUN_SINGLE_TANK_PCBA ACK timeout\n");
        }
    }

    uint16_t seq = 4;
    const int reportAttempts = reportOnly ? 3 : 420;
    for (int i = 0; i < reportAttempts; ++i) {
        transport.writeBytes(usb::buildGetSingleTankPcba(seq++));
        pump(app, 1200);
        if (lastReport.done || (reportOnly && lastReport.count > 0) ||
            (stopAfterZero && lastReport.count >= 6u)) {
            break;
        }
    }

    const bool zeroVentOk = stopAfterZero && lastReport.count >= 6u &&
                            lastReport.entries[5].kind == 1u &&
                            lastReport.entries[5].parsedValue <= 100u;
    if (stopAfterZero) {
        transport.writeBytes(usb::buildFrame(usb::Request, seq++, usb::Stop));
        pump(app, 1000);
    }

    std::fprintf(stdout, "FINAL\n");
    for (uint8_t i = 0; i < lastReport.count && i < lastReport.entries.size(); ++i) {
        const auto &e = lastReport.entries[i];
        const char *name = i < kStepNames.size() ? kStepNames[i] : "?";
        if (e.kind == 4u) {
            std::fprintf(stdout,
                         "%02u %s ok=%d kind=%u failure=0x%02X observe=%.3fms predicted=%.3fmmHg slope=%.3fmmHg/s residual=%.3fmmHg samples=%u\n",
                         static_cast<unsigned>(i + 1),
                         name,
                         e.ok ? 1 : 0,
                         static_cast<unsigned>(e.kind),
                         static_cast<unsigned>(e.command),
                         e.trendObservationUs / 1000.0,
                         e.trendPredictedPressure001mmHg / 1000.0,
                         e.trendSlope001mmHgPerSecond / 1000.0,
                         e.trendMaxResidual001mmHg / 1000.0,
                         static_cast<unsigned>(e.trendSampleCount));
            continue;
        }
        if (e.kind == 1u && e.command == 0x11u &&
            e.parsedValue != UINT32_MAX && e.comparisonPressure001mmHg != 0u) {
            const double errorMmHg =
                (static_cast<int64_t>(e.parsedValue) -
                 static_cast<int64_t>(e.comparisonPressure001mmHg)) / 1000.0;
            std::fprintf(stdout,
                         "%02u %s ok=%d cmd=0x11 elapsed=%.3fms PCBA=%.3fmmHg MPRLS1=%.3fmmHg error=%+.3fmmHg raw=%s\n",
                         static_cast<unsigned>(i + 1),
                         name,
                         e.ok ? 1 : 0,
                         e.elapsedUs / 1000.0,
                         e.parsedValue / 1000.0,
                         e.comparisonPressure001mmHg / 1000.0,
                         errorMmHg,
                         hexText(e.rawResponse, e.rawResponseLength).toLocal8Bit().constData());
            continue;
        }
        std::fprintf(stdout,
                     "%02u %s ok=%d kind=%u cmd=0x%02X elapsed=%.3fms current=%.2fuA parsed=%u raw=%s\n",
                     static_cast<unsigned>(i + 1),
                     name,
                     e.ok ? 1 : 0,
                     static_cast<unsigned>(e.kind),
                     static_cast<unsigned>(e.command),
                     e.elapsedUs / 1000.0,
                     e.currentUaX100 / 100.0,
                     e.parsedValue,
                     hexText(e.rawResponse, e.rawResponseLength).toLocal8Bit().constData());
    }
    snapshotReceived = false;
    transport.writeBytes(usb::buildFrame(usb::Request, seq++, usb::GetStatus));
    pump(app, 1500);
    transport.close();
    if (reportOnly) {
        return lastReport.count > 0 ? 0 : 1;
    }
    if (stopAfterZero) {
        std::fprintf(stdout,
                     "ZERO_VENT_BEFORE_RECORD %s pressure=%.3fmmHg\n",
                     zeroVentOk ? "PASS" : "FAIL",
                     lastReport.count >= 6u ?
                         lastReport.entries[5].parsedValue / 1000.0 : 0.0);
        return zeroVentOk ? 0 : 1;
    }
    const bool expectStep2Timeout = QCoreApplication::arguments().contains("--expect-step2-timeout");
    if (expectStep2Timeout) {
        const bool timeoutOk = lastReport.done && !lastReport.finalPass && lastReport.count == 2 &&
                               lastReport.entries[1].kind == 1u &&
                               lastReport.entries[1].command == 0x00u &&
                               !lastReport.entries[1].ok &&
                               lastReport.entries[1].rawResponseLength == 0u &&
                               lastReport.entries[1].elapsedUs >= 1900000u &&
                               lastReport.entries[1].elapsedUs <= 2100000u;
        std::fprintf(stdout, "EXPECTED_STEP2_TIMEOUT %s\n", timeoutOk ? "PASS" : "FAIL");
        return timeoutOk ? 0 : 1;
    }
    return lastReport.done && lastReport.finalPass ? 0 : 1;
}
