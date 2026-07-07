#include "MainWindow.h"

#include "ArchitectureView.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QSplitter>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QThread>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QFont>
#include <cmath>

using namespace fixture;

namespace {

class DiagramScrollArea : public QScrollArea {
public:
    explicit DiagramScrollArea(ArchitectureView *view, QWidget *parent = nullptr)
        : QScrollArea(parent)
        , m_view(view)
    {
        setWidget(m_view);
        setWidgetResizable(false);
        setFrameShape(QFrame::NoFrame);
        setBackgroundRole(QPalette::Window);
        setMouseTracking(true);
        viewport()->installEventFilter(this);
        m_view->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != viewport() && watched != m_view) {
            return QScrollArea::eventFilter(watched, event);
        }

        if (event->type() == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            zoomAt(viewportPositionFor(watched, wheel->position()), wheel->angleDelta().y());
            wheel->accept();
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::RightButton) {
                m_panning = true;
                m_lastPanGlobalPos = mouse->globalPosition().toPoint();
                setPanCursor(Qt::ClosedHandCursor);
                mouse->accept();
                return true;
            }
        }

        if (event->type() == QEvent::MouseMove && m_panning) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint current = mouse->globalPosition().toPoint();
            const QPoint delta = current - m_lastPanGlobalPos;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            m_lastPanGlobalPos = current;
            mouse->accept();
            return true;
        }

        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::RightButton && m_panning) {
                m_panning = false;
                setPanCursor(Qt::ArrowCursor);
                mouse->accept();
                return true;
            }
        }

        return QScrollArea::eventFilter(watched, event);
    }

private:
    QPointF viewportPositionFor(QObject *watched, const QPointF &position) const
    {
        if (watched == m_view) {
            return QPointF(m_view->mapTo(viewport(), position.toPoint()));
        }
        return position;
    }

    void zoomAt(const QPointF &viewportPosition, int wheelDelta)
    {
        if (!m_view || wheelDelta == 0) {
            return;
        }

        const double oldZoom = m_view->zoom();
        const QPointF logicalPosition((horizontalScrollBar()->value() + viewportPosition.x()) / oldZoom,
                                      (verticalScrollBar()->value() + viewportPosition.y()) / oldZoom);
        const double factor = std::pow(1.15, wheelDelta / 120.0);
        m_view->setZoom(oldZoom * factor);
        const double newZoom = m_view->zoom();

        horizontalScrollBar()->setValue(static_cast<int>(std::lround(logicalPosition.x() * newZoom - viewportPosition.x())));
        verticalScrollBar()->setValue(static_cast<int>(std::lround(logicalPosition.y() * newZoom - viewportPosition.y())));
    }

    void setPanCursor(Qt::CursorShape cursor)
    {
        viewport()->setCursor(cursor);
        if (m_view) {
            m_view->setCursor(cursor);
        }
    }

    ArchitectureView *m_view = nullptr;
    bool m_panning = false;
    QPoint m_lastPanGlobalPos;
};

bool isDebugOnlyState(RuntimeState state)
{
    switch (state) {
    case RuntimeState::UsbMsc:
    case RuntimeState::PcbaCurrentTest:
    case RuntimeState::RtcDebug:
    case RuntimeState::PcbaTimingDiagnostic:
    case RuntimeState::SingleTankPcbaDiagnostic:
    case RuntimeState::SinglePcbaFlow:
        return true;
    default:
        return false;
    }
}

bool stateVisibleForMode(RuntimeState state, bool debugMode)
{
    return debugMode ? isDebugOnlyState(state) : !isDebugOnlyState(state);
}

constexpr int kLeftKindRole = Qt::UserRole;
constexpr int kLeftValueRole = Qt::UserRole + 1;
constexpr int kHelloRetryLimit = 3;
constexpr int kPortNotReadyRetryRounds = 2;
constexpr int kPortNotReadyRetryDelayMs = 1200;
constexpr int kPcbaTimingStepCount = 10;
constexpr int kSingleTankPcbaStepCount = 17;
constexpr int kPcbaTimingLimitUs = 10000;
constexpr uint8_t kSingleTankPcbaFlag5V = 0x01;
constexpr uint8_t kSingleTankPcbaFlag45V = 0x02;
constexpr uint8_t kSingleTankPcbaFlag50mA = 0x04;
constexpr uint8_t kSingleTankPcbaFlagCurrent = 0x08;

QString pcbaSupplyVoltageText(const FixtureSnapshot &snapshot)
{
    if (snapshot.pcbaSupply5VEnabled && !snapshot.pcbaSupply45VEnabled) {
        return "5V";
    }
    if (snapshot.pcbaSupply45VEnabled && !snapshot.pcbaSupply5VEnabled) {
        return "4.5V";
    }
    if (snapshot.pcbaSupply5VEnabled && snapshot.pcbaSupply45VEnabled) {
        return "5V/4.5V同时打开";
    }
    return "未供电";
}

struct SinglePcbaCommandSpec {
    const char *name;
    uint8_t command;
    int pressure001mmHg;
    bool wakeByte;
};

const std::array<SinglePcbaCommandSpec, kPcbaTimingStepCount> &singlePcbaCommandSpecs()
{
    static const std::array<SinglePcbaCommandSpec, kPcbaTimingStepCount> specs{{
        {"唤醒", 0x00, -1, true},
        {"单板上电", 0x00, -1, false},
        {"低功耗查询", 0x03, -1, false},
        {"正常功耗查询", 0x04, -1, false},
        {"记录零点", 0x05, -1, false},
        {"压力查询", 0x11, -1, false},
        {"50mmHg标定", 0x10, 500, false},
        {"150mmHg标定", 0x10, 1500, false},
        {"250mmHg标定", 0x10, 2500, false},
        {"写Flash", 0x20, -1, false},
    }};
    return specs;
}

struct SingleTankPcbaStepSpec {
    const char *name;
    uint8_t command;
    uint32_t payloadValue;
    uint8_t payloadLength;
    bool currentOnly;
    bool skipOnly;
    const char *action;
};

const std::array<SingleTankPcbaStepSpec, kSingleTankPcbaStepCount> &singleTankPcbaStepSpecs()
{
    static const std::array<SingleTankPcbaStepSpec, kSingleTankPcbaStepCount> specs{{
        {"待机电流", 0x00, 0u, 0u, true, false, "先0V 1s使PCBA彻底关机，再切5V等1s，10uA档记录待机电流"},
        {"开机+进测试", 0x00, 0u, 0u, false, false, "切50mA档，同一条开机+进测试命令发送2次，第二次前间隔0.5s，收到任意回包都记录"},
        {"开机后电流", 0x00, 0u, 0u, true, false, "开机+进测试后记录50mA档运行电流"},
        {"读版本配置", 0x01, 0u, 0u, false, false, "期望版本配置回0A 0A"},
        {"查低电", 0x03, 0u, 0u, false, false, "供电切到4.5V后等待1s再查询低电，期望ACK YES"},
        {"查正常", 0x04, 0u, 0u, false, false, "供电切回5V后等待0.5s再查询正常，期望ACK YES"},
        {"记录零点", 0x05, 0u, 0u, false, false, "上压前记录零点，期望ACK YES"},
        {"开泵", 0x06, 1u, 1u, false, false, "发送开泵，期望ACK YES"},
        {"关泵", 0x06, 0u, 1u, false, false, "发送关泵，期望ACK YES"},
        {"开阀", 0x07, 1u, 1u, false, false, "发送开阀，期望ACK YES"},
        {"关阀", 0x07, 0u, 1u, false, false, "发送关阀，期望ACK YES"},
        {"LCD全显", 0x08, 1u, 1u, false, false, "LCD全显，期望ACK YES"},
        {"LCD恢复", 0x08, 0u, 1u, false, false, "LCD恢复，期望ACK YES"},
        {"标定50mmHg", 0x10, 500u, 4u, false, false, "写入50mmHg标定点，期望ACK YES"},
        {"标定150mmHg", 0x10, 1500u, 4u, false, false, "写入150mmHg标定点，期望ACK YES"},
        {"标定250mmHg", 0x10, 2500u, 4u, false, true, "逻辑预留：250mmHg标定流程后续补充，本次不发送串口指令"},
        {"关机", 0x21, 0u, 0u, false, false, "发送关机，期望ACK YES"},
    }};
    return specs;
}

QString bytesToHexText(const QByteArray &bytes)
{
    return bytes.toHex(' ').toUpper();
}

template <size_t N>
QString rawBytesToHexText(const std::array<uint8_t, N> &bytes, uint8_t length)
{
    QByteArray raw;
    const int count = qMin<int>(length, static_cast<int>(bytes.size()));
    raw.reserve(count);
    for (int i = 0; i < count; ++i) {
        raw.append(static_cast<char>(bytes[static_cast<size_t>(i)]));
    }
    return bytesToHexText(raw);
}

void appendLe16(QByteArray &bytes, uint16_t value)
{
    bytes.append(static_cast<char>(value & 0xFF));
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
}

void appendLe32(QByteArray &bytes, uint32_t value)
{
    bytes.append(static_cast<char>(value & 0xFF));
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
    bytes.append(static_cast<char>((value >> 16) & 0xFF));
    bytes.append(static_cast<char>((value >> 24) & 0xFF));
}

uint16_t pcbaCrc16(const QByteArray &bytes)
{
    uint16_t crc = 0xFFFF;
    for (const char c : bytes) {
        crc ^= static_cast<uint8_t>(c);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

QByteArray buildPcbaFrame(uint8_t command, uint8_t channel, uint32_t payloadValue, uint8_t payloadLength)
{
    QByteArray frame;
    frame.append(static_cast<char>(0x55));
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(command));
    frame.append(static_cast<char>(channel));
    appendLe16(frame, payloadLength);
    if (payloadLength == 1) {
        frame.append(static_cast<char>(payloadValue & 0xFFu));
    } else if (payloadLength == 4) {
        appendLe32(frame, payloadValue);
    } else {
        for (uint8_t i = 0; i < payloadLength; ++i) {
            frame.append(static_cast<char>((payloadValue >> (8u * i)) & 0xFFu));
        }
    }
    appendLe16(frame, pcbaCrc16(frame.mid(2)));
    return frame;
}

QByteArray buildPcbaPressureFrame(uint8_t command, uint8_t channel, int pressure001mmHg)
{
    return pressure001mmHg >= 0
        ? buildPcbaFrame(command, channel, static_cast<uint32_t>(pressure001mmHg), 4)
        : buildPcbaFrame(command, channel, 0u, 0u);
}

QByteArray buildPcbaFrameFromData(uint8_t command, uint8_t channel, const std::array<uint8_t, 4> &data, uint8_t dataLen)
{
    QByteArray frame;
    frame.append(static_cast<char>(0x55));
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(command));
    frame.append(static_cast<char>(channel));
    appendLe16(frame, dataLen);
    for (uint8_t i = 0; i < dataLen && i < data.size(); ++i) {
        frame.append(static_cast<char>(data[static_cast<size_t>(i)]));
    }
    appendLe16(frame, pcbaCrc16(frame.mid(2)));
    return frame;
}

QString singlePcbaTxText(int index)
{
    const auto &specs = singlePcbaCommandSpecs();
    if (index < 0 || index >= static_cast<int>(specs.size())) {
        return "--";
    }
    const auto &spec = specs[static_cast<size_t>(index)];
    if (spec.wakeByte) {
        return "00";
    }
    const QString tx = bytesToHexText(buildPcbaPressureFrame(spec.command, 0, spec.pressure001mmHg));
    if (index == 1) {
        return QString("%1；重复发送2次，间隔0.5s").arg(tx);
    }
    return tx;
}

QString singlePcbaStepText(int index)
{
    const auto &specs = singlePcbaCommandSpecs();
    if (index < 0 || index >= static_cast<int>(specs.size())) {
        return QString("步骤%1").arg(index + 1);
    }
    return QString("%1 0x%2")
        .arg(QString::fromUtf8(specs[static_cast<size_t>(index)].name))
        .arg(specs[static_cast<size_t>(index)].command, 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString singleTankPcbaStepText(int index)
{
    const auto &specs = singleTankPcbaStepSpecs();
    if (index < 0 || index >= static_cast<int>(specs.size())) {
        return QString("步骤%1").arg(index + 1);
    }
    return QString("%1. %2")
        .arg(index + 1)
        .arg(QString::fromUtf8(specs[static_cast<size_t>(index)].name));
}

QString singleTankPcbaTxText(int index)
{
    const auto &specs = singleTankPcbaStepSpecs();
    if (index < 0 || index >= static_cast<int>(specs.size())) {
        return "--";
    }
    const auto &spec = specs[static_cast<size_t>(index)];
    if (spec.currentOnly) {
        return "--";
    }
    if (spec.skipOnly) {
        return "预留，暂不发送";
    }
    const QString tx = bytesToHexText(buildPcbaFrame(spec.command, 0, spec.payloadValue, spec.payloadLength));
    if (index == 1) {
        return QString("%1；重复发送2次，间隔0.5s").arg(tx);
    }
    return tx;
}

QString singleTankPcbaActionText(int index, uint8_t flags)
{
    const auto &specs = singleTankPcbaStepSpecs();
    QStringList parts;
    if ((flags & kSingleTankPcbaFlag45V) != 0) {
        parts << "4.5V";
    }
    if ((flags & kSingleTankPcbaFlag5V) != 0) {
        parts << "5V";
    }
    parts << (((flags & kSingleTankPcbaFlag50mA) != 0) ? "50mA档" : "10uA档");
    if (index >= 0 && index < static_cast<int>(specs.size())) {
        parts << QString::fromUtf8(specs[static_cast<size_t>(index)].action);
    }
    return parts.join(" | ");
}

uint8_t expectedSingleTankPcbaFlags(int index)
{
    if (index == 0) {
        return kSingleTankPcbaFlag5V | kSingleTankPcbaFlagCurrent;
    }
    if (index == 2) {
        return kSingleTankPcbaFlag5V | kSingleTankPcbaFlag50mA | kSingleTankPcbaFlagCurrent;
    }
    if (index == 4) {
        return kSingleTankPcbaFlag45V | kSingleTankPcbaFlag50mA;
    }
    return kSingleTankPcbaFlag5V | kSingleTankPcbaFlag50mA;
}

QString singleTankPcbaRxText(const SingleTankPcbaEntry &entry)
{
    if (entry.kind == 2 || entry.kind == 3) {
        return "--";
    }
    if (entry.rawResponseLength > 0) {
        return rawBytesToHexText(entry.rawResponse, entry.rawResponseLength);
    }
    if (!entry.ok) {
        return "未收到有效回包";
    }
    if (entry.kind == 0) {
        return QString("%1").arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return bytesToHexText(buildPcbaFrameFromData(entry.responseCommandOrByte,
                                                 entry.responseChannel,
                                                 entry.responseData,
                                                 entry.responseLength));
}

QString singleTankPcbaParsedText(int index, const SingleTankPcbaEntry &entry)
{
    if (entry.kind == 2) {
        return QString("电流=%1").arg(formatCurrentUaX100(entry.currentUaX100, entry.ok, 2, true));
    }
    if (entry.kind == 3) {
        return "250mmHg标定预留，未发送串口指令";
    }
    if (entry.kind == 0) {
        return entry.ok ? "唤醒回00" : "未收到00唤醒回包";
    }
    const auto &specs = singleTankPcbaStepSpecs();
    const uint8_t command = (index >= 0 && index < static_cast<int>(specs.size()))
        ? specs[static_cast<size_t>(index)].command
        : entry.command;
    if (command == 0x10) {
        return QString("标定目标=%1").arg(formatPressure001mmHg(static_cast<int>(entry.parsedValue), true, 1, true));
    }
    if (command == 0x01) {
        return QString("版本配置=0x%1，期望0A 0A")
            .arg(entry.parsedValue, 4, 16, QLatin1Char('0'))
            .toUpper();
    }
    if (index == 1) {
        return QString("开机回包长度=%1字节，允许00/00 00/ACK/其他").arg(entry.parsedValue);
    }
    if (command == 0x06 || command == 0x07 || command == 0x08) {
        return QString("控制值=%1，ACK数据=%2").arg(entry.parsedValue).arg(entry.responseLength > 0 ? entry.responseData[0] : 0);
    }
    return QString("ACK数据=%1").arg(entry.parsedValue);
}

QString singleTankPcbaReasonText(const SingleTankPcbaEntry &entry)
{
    if (entry.kind == 3) {
        return "预留步骤，未执行";
    }
    if (entry.ok && (entry.kind == 2 || entry.elapsedUs <= kPcbaTimingLimitUs)) {
        return "通过";
    }
    if (entry.kind == 2) {
        return "电流采样无效";
    }
    if (entry.ok) {
        return QString("通过，回包耗时%1ms（超过10ms仅记录）").arg(entry.elapsedUs / 1000.0, 0, 'f', 3);
    }
    if (entry.elapsedUs == 0) {
        return "未执行或固件未返回该步骤结果";
    }
    return QString("未收到符合流程的有效回包，等待到%1ms").arg(entry.elapsedUs / 1000.0, 0, 'f', 3);
}

QString singlePcbaRxText(const PcbaTimingEntry &entry)
{
    if (entry.rawResponseLength > 0) {
        return rawBytesToHexText(entry.rawResponse, entry.rawResponseLength);
    }
    if (!entry.ok && entry.elapsedUs == 0) {
        return "--";
    }
    if (entry.kind == 0) {
        return QString("0x%1").arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0')).toUpper();
    }
    QString text = QString("cmd=0x%1 ch=%2 len=%3")
        .arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0'))
        .arg(entry.responseChannel)
        .arg(entry.responseLength)
        .toUpper();
    if (entry.responseLength > 0) {
        QByteArray data;
        const int bytes = qMin<int>(entry.responseLength, static_cast<int>(entry.responseData.size()));
        for (int i = 0; i < bytes; ++i) {
            data.append(static_cast<char>(entry.responseData[static_cast<size_t>(i)]));
        }
        text += " data=" + bytesToHexText(data);
    }
    return text;
}

QString singlePcbaReasonText(const PcbaTimingEntry &entry)
{
    if (entry.ok && entry.elapsedUs <= kPcbaTimingLimitUs) {
        return "通过";
    }
    if (entry.ok) {
        return QString("回包超出10ms规范，耗时%1ms").arg(entry.elapsedUs / 1000.0, 0, 'f', 3);
    }
    if (entry.elapsedUs == 0) {
        return "未执行或固件未返回该步骤结果";
    }
    return QString("未收到有效回包，等待到%1ms超时").arg(entry.elapsedUs / 1000.0, 0, 'f', 3);
}

QString responseDataText(const std::array<uint8_t, 4> &data, uint8_t length)
{
    QByteArray bytes;
    const int count = qMin<int>(length, static_cast<int>(data.size()));
    bytes.reserve(count);
    for (int i = 0; i < count; ++i) {
        bytes.append(static_cast<char>(data[static_cast<size_t>(i)]));
    }
    return bytes.isEmpty() ? "--" : bytesToHexText(bytes);
}

QString pcbaTimingParsedDetail(const PcbaTimingEntry &entry)
{
    if (entry.kind == 0) {
        return QString("单字节唤醒回包=0x%1").arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return QString("字段解析 cmd=0x%1 ch=%2 len=%3 data=%4")
        .arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0'))
        .arg(entry.responseChannel)
        .arg(entry.responseLength)
        .arg(responseDataText(entry.responseData, entry.responseLength))
        .toUpper();
}

QString pcbaTimingSerialLogText(const PcbaTimingReport &report)
{
    QStringList lines;
    lines << "说明：这里只显示 MCU UART1 <-> PCBA 的串口收发。CRC 和通道号只记录，不作为丢包条件。";
    const int visibleRows = (report.done || report.running)
        ? kPcbaTimingStepCount
        : qMin(kPcbaTimingStepCount, static_cast<int>(report.count));
    if (visibleRows == 0) {
        lines << "未开始：等待 MCU 接受启动命令。";
        return lines.join('\n');
    }
    for (int row = 0; row < visibleRows; ++row) {
        const bool hasEntry = row < report.count;
        const PcbaTimingEntry entry = hasEntry ? report.entries[static_cast<size_t>(row)] : PcbaTimingEntry{};
        lines << QString("[%1] %2").arg(row + 1, 2, 10, QLatin1Char('0')).arg(singlePcbaStepText(row));
        if (hasEntry) {
            lines << QString("  MCU -> PCBA: %1").arg(singlePcbaTxText(row));
            lines << QString("  PCBA -> MCU: %1").arg(singlePcbaRxText(entry));
            lines << QString("  解析: %1").arg(pcbaTimingParsedDetail(entry));
            lines << QString("  耗时: %1 ms | 判定: %2")
                         .arg(entry.elapsedUs / 1000.0, 0, 'f', 3)
                         .arg(singlePcbaReasonText(entry));
        } else if (report.done && report.count < kPcbaTimingStepCount) {
            lines << "  已在前序失败后停止，后续步骤未执行";
        } else {
            lines << QString("  当前动作: MCU 即将/正在执行该步骤");
            lines << QString("  MCU -> PCBA: %1").arg(singlePcbaTxText(row));
            lines << "  正在等待: PCBA 回包或 MCU 返回该步骤超时结果";
        }
    }
    return lines.join('\n');
}

QString singleTankPcbaParsedDetail(int index, const SingleTankPcbaEntry &entry)
{
    if (entry.kind == 2) {
        return QString("电流采样=%1").arg(formatCurrentUaX100(entry.currentUaX100, entry.ok, 2, true));
    }
    if (entry.kind == 0) {
        return QString("单字节唤醒回包=0x%1").arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return QString("%1 | 字段解析 cmd=0x%2 ch=%3 len=%4 data=%5")
        .arg(singleTankPcbaParsedText(index, entry))
        .arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0'))
        .arg(entry.responseChannel)
        .arg(entry.responseLength)
        .arg(responseDataText(entry.responseData, entry.responseLength))
        .toUpper();
}

QString singleTankPcbaSerialLogText(const SingleTankPcbaReport &report)
{
    QStringList lines;
    lines << "说明：这里只显示 MCU UART1 <-> PCBA 的串口收发和单罐流程动作。PCBA协议CRC从命令字节开始算，不包含55 AA；通道号只记录，不作为丢包条件。";
    const int visibleRows = (report.running || report.done || report.count > 0)
        ? kSingleTankPcbaStepCount
        : 0;
    if (visibleRows == 0) {
        lines << "未开始：等待 MCU 接受启动命令。";
        return lines.join('\n');
    }
    for (int row = 0; row < visibleRows; ++row) {
        const bool hasEntry = row < report.count;
        const SingleTankPcbaEntry entry = hasEntry ? report.entries[static_cast<size_t>(row)] : SingleTankPcbaEntry{};
        lines << QString("[%1] %2").arg(row + 1, 2, 10, QLatin1Char('0')).arg(singleTankPcbaStepText(row));
        if (hasEntry) {
            lines << QString("  已执行动作/档位: %1").arg(singleTankPcbaActionText(row, entry.flags));
            lines << QString("  MCU -> PCBA: %1").arg(singleTankPcbaTxText(row));
            lines << QString("  PCBA -> MCU: %1").arg(singleTankPcbaRxText(entry));
            lines << QString("  解析: %1").arg(singleTankPcbaParsedDetail(row, entry));
            lines << QString("  耗时: %1 | 判定: %2")
                         .arg(entry.kind == 2 ? "--" : QString("%1 ms").arg(entry.elapsedUs / 1000.0, 0, 'f', 3))
                         .arg(singleTankPcbaReasonText(entry));
        } else {
            lines << QString("  当前动作/档位: %1").arg(singleTankPcbaActionText(row, expectedSingleTankPcbaFlags(row)));
            lines << QString("  MCU -> PCBA: %1").arg(singleTankPcbaTxText(row));
            lines << ((row == 0 || row == 2)
                          ? "  正在等待: MCU 完成供电切换/电流采样并返回该步骤结果"
                          : "  正在等待: PCBA 回包或 MCU 返回该步骤超时结果");
        }
    }
    return lines.join('\n');
}

QString singlePcbaProgressText(const PcbaTimingReport &report)
{
    if (report.done) {
        if (report.finalPass) {
            return "已完成，全部通过";
        }
        if (report.count < kPcbaTimingStepCount) {
            return QString("已完成，遇到失败已停止于第%1步").arg(report.count);
        }
        return "已完成，存在失败项";
    }
    if (report.running) {
        const int next = qMin<int>(report.count, kPcbaTimingStepCount - 1);
        return QString("正在执行第%1步：%2").arg(next + 1).arg(singlePcbaStepText(next));
    }
    return "未启动";
}

QString singleTankPcbaProgressText(const SingleTankPcbaReport &report)
{
    if (report.done) {
        return report.finalPass ? "已完成，全部通过" : "已完成，存在失败项";
    }
    if (report.running) {
        const int next = qMin<int>(report.count, kSingleTankPcbaStepCount - 1);
        return QString("正在执行第%1步：%2").arg(next + 1).arg(singleTankPcbaStepText(next));
    }
    return "未启动";
}

QString pcbaDiagnosticStateText(RuntimeState snapshotState,
                                RuntimeState expectedState,
                                const QString &expectedText,
                                bool active)
{
    const QString actualText = stateDisplayName(snapshotState);
    if (!active) {
        return actualText;
    }
    if (snapshotState == expectedState) {
        return expectedText;
    }
    return QString("%1 | MCU上报: %2（不符合预期，可能是旧固件或状态未切换）")
        .arg(expectedText)
        .arg(actualText);
}

QString debugToolDisplayName(int tool)
{
    switch (tool) {
    case 0: return "U盘维护模式";
    case 1: return "PCBA电流测试";
    case 2: return "单PCBA全流程测试";
    case 3: return "单罐单PCBA测试";
    case 4: return "单罐体闭环测试";
    case 5: return "阈值与手动阀";
    case 6: return "ADC实时基准";
    case 7: return "RTC时钟调试模式";
    case 8: return "固件烧录";
    default: return "未知调试项";
    }
}

QString usbErrorDisplayName(uint8_t code)
{
    switch (code) {
    case 0x00: return "成功";
    case 0x01: return "长度错误";
    case 0x02: return "状态错误";
    case 0x03: return "参数错误";
    case 0x04: return "设备忙";
    case 0x05: return "命令不支持";
    case 0x06: return "CRC错误";
    case 0x07: return "协议版本错误";
    default: return QString("未知错误 0x%1").arg(code, 2, 16, QLatin1Char('0')).toUpper();
    }
}

QString sensorFaultUiText(const FixtureSnapshot &snapshot, int sensorIndex)
{
    const QString reason = pressureSensorFaultReasonText(snapshot, sensorIndex);
    if (pressureSensorFaultLatched(snapshot, sensorIndex)) {
        return reason.isEmpty() ? QStringLiteral("故障锁定，需重新上电")
                                : QStringLiteral("故障锁定，需重新上电 | %1").arg(reason);
    }
    if (!pressureSensorValid(snapshot, sensorIndex) && !reason.isEmpty()) {
        return QStringLiteral("无有效读数 | %1").arg(reason);
    }
    return {};
}

} // namespace

SensorCalibrationDialog::SensorCalibrationDialog(int sensorNumber, QWidget *parent)
    : QDialog(parent)
    , m_sensorNumber(sensorNumber)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString("压力检测%1 标定").arg(sensorNumber));
    resize(520, 360);

    auto *layout = new QVBoxLayout(this);

    m_titleLabel = new QLabel(QString("压力检测%1 传感器标定").arg(sensorNumber), this);
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #0f172a;");
    layout->addWidget(m_titleLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #475569;");
    layout->addWidget(m_statusLabel);

    m_pointTable = new QTableWidget(3, 3, this);
    m_pointTable->setHorizontalHeaderLabels({"标定点", "采集读数", "状态"});
    m_pointTable->verticalHeader()->hide();
    m_pointTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pointTable->setSelectionMode(QAbstractItemView::SingleSelection);
    const QStringList targets{"0 mmHg", "100 mmHg", "285 mmHg"};
    for (int row = 0; row < targets.size(); ++row) {
        auto *target = new QTableWidgetItem(targets[row]);
        target->setFlags(target->flags() & ~Qt::ItemIsEditable);
        m_pointTable->setItem(row, 0, target);
        m_pointTable->setItem(row, 1, new QTableWidgetItem("--"));
        m_pointTable->setItem(row, 2, new QTableWidgetItem("待采集"));
    }
    m_pointTable->setCurrentCell(0, 0);
    layout->addWidget(m_pointTable, 1);

    auto *buttonLayout = new QHBoxLayout();
    auto *captureButton = new QPushButton("采集当前读数", this);
    auto *saveButton = new QPushButton("保存标定", this);
    auto *closeButton = new QPushButton("关闭", this);
    buttonLayout->addWidget(captureButton);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);

    connect(captureButton, &QPushButton::clicked, this, &SensorCalibrationDialog::captureSelectedPoint);
    connect(saveButton, &QPushButton::clicked, this, &SensorCalibrationDialog::saveCalibration);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
}

int SensorCalibrationDialog::sensorNumber() const
{
    return m_sensorNumber;
}

void SensorCalibrationDialog::setSnapshot(const FixtureSnapshot &snapshot)
{
    m_snapshot = snapshot;
    const int index = m_sensorNumber - 1;
    const bool faultLatched = pressureSensorFaultLatched(m_snapshot, index);
    const bool valid = pressureSensorValid(m_snapshot, index);
    const QString sensorStatus = sensorFaultUiText(m_snapshot, index);
    m_statusLabel->setText(QString("状态: %1 | 当前读数: %2")
                               .arg(sensorStatus.isEmpty() ? (valid ? "已连接" : "未连接") : sensorStatus)
                               .arg(sensorPressureText(m_snapshot, index, 2, true)));
    m_statusLabel->setStyleSheet(faultLatched ? "color: #b91c1c;" :
                                              (valid ? "color: #0f766e;" : "color: #b45309;"));
}

void SensorCalibrationDialog::captureSelectedPoint()
{
    const int index = m_sensorNumber - 1;
    if (pressureSensorFaultLatched(m_snapshot, index)) {
        QMessageBox::warning(this, "传感器故障",
                             QString("压力检测%1 已故障锁定，需重新上电后再继续。").arg(m_sensorNumber));
        return;
    }
    if (!pressureSensorValid(m_snapshot, index)) {
        QMessageBox::warning(this, "传感器未连接", QString("压力检测%1 当前无有效读数。").arg(m_sensorNumber));
        return;
    }

    int row = m_pointTable->currentRow();
    if (row < 0) {
        row = 0;
    }

    m_pointTable->setItem(row, 1, new QTableWidgetItem(sensorPressureText(m_snapshot, index, 2)));
    m_pointTable->setItem(row, 2, new QTableWidgetItem("已采集"));
}

void SensorCalibrationDialog::saveCalibration()
{
    for (int row = 0; row < m_pointTable->rowCount(); ++row) {
        const auto *item = m_pointTable->item(row, 1);
        if (item == nullptr || item->text() == "--") {
            QMessageBox::warning(this, "标定未完成", "还有标定点未采集。");
            return;
        }
    }

    QMessageBox::information(this, "标定已记录", QString("压力检测%1 的标定点已记录到上位机界面。").arg(m_sensorNumber));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    const QFileInfo exeInfo(QCoreApplication::applicationFilePath());
    const QString buildVersion = QCoreApplication::applicationVersion();
    const QString exeStamp = exeInfo.exists()
        ? exeInfo.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("unknown");
    setWindowTitle(QStringLiteral("气压检测工装上位机 [USB CDC版 %1 | exe %2]")
                       .arg(buildVersion.isEmpty() ? QStringLiteral("no-version") : buildVersion,
                            exeStamp));
    m_architectureView = new ArchitectureView(this);
    connect(m_architectureView, &ArchitectureView::valveClicked, this, &MainWindow::toggleValveFromDiagram);
    connect(m_architectureView, &ArchitectureView::sensorClicked, this, &MainWindow::openSensorCalibration);
    auto *architectureScroll = new DiagramScrollArea(m_architectureView, this);

    auto *splitter = new QSplitter(this);
    splitter->setOpaqueResize(false);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(architectureScroll);
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({240, 780, 580});
    setCentralWidget(splitter);

    connect(&m_transport, &WindowsSerialTransport::bytesReceived, this, &MainWindow::handleSerialBytes);
    connect(&m_transport, &WindowsSerialTransport::errorOccurred, this, &MainWindow::handleSerialError);
    connect(&m_transport, &WindowsSerialTransport::openChanged, this, [this](bool open) {
        m_connectButton->setText(open ? "断开" : "连接");
        m_snapshot.linkMode = open ? LinkMode::UsbCdc : LinkMode::Disconnected;
        if (!open) {
            if (m_singleTankRunning) {
                m_singleTankTimer.stop();
                m_singleTankRunning = false;
                resetSingleTankLoopControl();
                updateSingleTankPanel();
            }
            m_singleTankPcbaStartPending = false;
            m_singleTankPcbaRunning = false;
            m_singleTankPcbaPollTimer.stop();
            m_handshakeTimer.stop();
            m_waitingForHello = false;
            m_architectureView->clearPendingValveCommands();
            applySnapshot(FixtureSnapshot{});
            appendLog("已断开连接");
        }
    });
    connect(&m_firmwareProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        appendFirmwareLog(QString::fromLocal8Bit(m_firmwareProcess.readAllStandardOutput()));
    });
    connect(&m_firmwareProcess, &QProcess::readyReadStandardError, this, [this]() {
        appendFirmwareLog(QString::fromLocal8Bit(m_firmwareProcess.readAllStandardError()));
    });
    connect(&m_firmwareProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &MainWindow::handleFirmwareDownloadFinished);
    connect(&m_firmwareProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Q_UNUSED(error)
        appendFirmwareLog("J-Link 进程启动或执行失败: " + m_firmwareProcess.errorString());
        if (m_firmwareStatusLabel) {
            m_firmwareStatusLabel->setText("烧录失败: " + m_firmwareProcess.errorString());
        }
        if (m_firmwareDownloadButton) {
            m_firmwareDownloadButton->setEnabled(true);
        }
        m_firmwareDownloadRunning = false;
    });
    m_handshakeTimer.setSingleShot(true);
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this]() {
        if (m_transport.isOpen() && m_waitingForHello) {
            ++m_helloRetryCount;
            const QString currentDisplay =
                m_connectCandidateDisplays.value(m_connectCandidateIndex,
                                                 m_transport.portName().isEmpty() ? QStringLiteral("当前入口")
                                                                                  : m_transport.portName());
            if (m_helloRetryCount >= kHelloRetryLimit) {
                appendLog(QString("%1 连续 %2 次握手无响应，尝试下一个入口")
                              .arg(currentDisplay)
                              .arg(kHelloRetryLimit));
                m_waitingForHello = false;
                m_handshakeTimer.stop();
                m_transport.close();
                if (!tryOpenNextConnectCandidate()) {
                    statusBar()->showMessage("所有连接入口都已尝试，仍未收到 MCU 响应", 5000);
                }
                return;
            }

            appendLog(QString("HELLO 未收到响应，重试 %1/%2 (%3)")
                          .arg(m_helloRetryCount)
                          .arg(kHelloRetryLimit)
                          .arg(currentDisplay));
            sendFrame(usb::buildHello(nextSequence()), "HELLO");
            m_handshakeTimer.start(1500);
        }
    });
    m_singleTankTimer.setInterval(400);
    connect(&m_singleTankTimer, &QTimer::timeout, this, &MainWindow::serviceSingleTankLoop);
    m_singlePcbaTimingPollTimer.setInterval(500);
    connect(&m_singlePcbaTimingPollTimer, &QTimer::timeout, this, &MainWindow::requestSinglePcbaTimingReport);
    m_singleTankPcbaPollTimer.setInterval(500);
    connect(&m_singleTankPcbaPollTimer, &QTimer::timeout, this, &MainWindow::requestSingleTankPcbaReport);

    refreshPorts();
    FixtureSnapshot initialSnapshot;
    initialSnapshot.linkMode = LinkMode::Disconnected;
    applySnapshot(initialSnapshot);
    updateModeUi();
    appendLog(QString("上位机版本: %1 | exe时间: %2 | 路径: %3")
                  .arg(QCoreApplication::applicationVersion(),
                       exeStamp,
                       QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
}

QWidget *MainWindow::buildLeftPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);

    m_stateLabel = new QLabel(panel);
    m_stateLabel->setWordWrap(true);
    m_stateLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #0f172a;");
    layout->addWidget(m_stateLabel);

    m_linkLabel = new QLabel(panel);
    m_linkLabel->setStyleSheet("color: #475569;");
    layout->addWidget(m_linkLabel);

    auto *modeBox = new QGroupBox("运行模式", panel);
    auto *modeLayout = new QVBoxLayout(modeBox);
    m_modeHintLabel = new QLabel(modeBox);
    m_modeHintLabel->setWordWrap(true);
    m_modeHintLabel->setStyleSheet("color: #475569;");
    modeLayout->addWidget(m_modeHintLabel);
    auto *modeButtonLayout = new QHBoxLayout();
    m_productionModeButton = new QPushButton("生产模式", modeBox);
    m_debugModeButton = new QPushButton("调试模式", modeBox);
    modeButtonLayout->addWidget(m_productionModeButton);
    modeButtonLayout->addWidget(m_debugModeButton);
    modeLayout->addLayout(modeButtonLayout);
    connect(m_productionModeButton, &QPushButton::clicked, this, &MainWindow::selectProductionMode);
    connect(m_debugModeButton, &QPushButton::clicked, this, &MainWindow::selectDebugMode);
    layout->addWidget(modeBox);

    m_flowList = new QListWidget(panel);
    connect(m_flowList, &QListWidget::currentItemChanged, this, [this]() {
        handleLeftItemChanged();
    });
    connect(m_flowList, &QListWidget::itemDoubleClicked, this, &MainWindow::sendSelectedState);
    layout->addWidget(m_flowList, 1);

    m_flowHintLabel = new QLabel(panel);
    m_flowHintLabel->setWordWrap(true);
    m_flowHintLabel->setStyleSheet("color: #64748b;");
    layout->addWidget(m_flowHintLabel);

    return panel;
}

QWidget *MainWindow::buildRightPanel()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *panel = new QWidget(scroll);
    auto *layout = new QVBoxLayout(panel);

    auto *usbBox = new QGroupBox("USB / 虚拟串口连接", panel);
    auto *usbLayout = new QGridLayout(usbBox);
    m_portCombo = new QComboBox(usbBox);
    m_baudCombo = new QComboBox(usbBox);
    m_baudCombo->addItems({"9600", "115200"});
    m_baudCombo->setCurrentText("9600");
    auto *refreshButton = new QPushButton("刷新", usbBox);
    m_connectButton = new QPushButton("连接", usbBox);
    usbLayout->addWidget(m_portCombo, 0, 0, 1, 2);
    usbLayout->addWidget(refreshButton, 0, 2);
    usbLayout->addWidget(new QLabel("波特率"), 1, 0);
    usbLayout->addWidget(m_baudCombo, 1, 1, 1, 2);
    usbLayout->addWidget(m_connectButton, 2, 0, 1, 3);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectOrDisconnect);
    layout->addWidget(usbBox);

    m_modeTabs = new QTabWidget(panel);
    auto *productionPage = new QWidget(m_modeTabs);
    auto *productionPageLayout = new QVBoxLayout(productionPage);
    auto *debugPage = new QWidget(m_modeTabs);
    auto *debugPageLayout = new QVBoxLayout(debugPage);

    m_productionBox = new QGroupBox("生产流程", productionPage);
    auto *productionLayout = new QGridLayout(m_productionBox);
    auto *productionStartButton = new QPushButton("开始生产流程", m_productionBox);
    auto *productionStopButton = new QPushButton("停止", m_productionBox);
    auto *productionPauseButton = new QPushButton("暂停", m_productionBox);
    auto *productionResumeButton = new QPushButton("继续", m_productionBox);
    productionLayout->addWidget(productionStartButton, 0, 0, 1, 2);
    productionLayout->addWidget(productionStopButton, 1, 0);
    productionLayout->addWidget(productionPauseButton, 1, 1);
    productionLayout->addWidget(productionResumeButton, 2, 0, 1, 2);
    connect(productionStartButton, &QPushButton::clicked, this, &MainWindow::sendProductionStart);
    connect(productionStopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    connect(productionPauseButton, &QPushButton::clicked, this, &MainWindow::sendPause);
    connect(productionResumeButton, &QPushButton::clicked, this, &MainWindow::sendResume);
    productionPageLayout->addWidget(m_productionBox);
    productionPageLayout->addStretch(1);

    m_debugFlowBox = new QGroupBox("U盘维护模式", debugPage);
    auto *controlLayout = new QGridLayout(m_debugFlowBox);
    auto *stopButton = new QPushButton("安全停止", m_debugFlowBox);
    auto *mscButton = new QPushButton("重启到U盘", m_debugFlowBox);
    controlLayout->addWidget(stopButton, 0, 0);
    controlLayout->addWidget(mscButton, 0, 1);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    connect(mscButton, &QPushButton::clicked, this, &MainWindow::sendEnterMsc);
    debugPageLayout->addWidget(m_debugFlowBox);

    m_debugCurrentBox = new QGroupBox("PCBA电流测试", debugPage);
    auto *currentLayout = new QVBoxLayout(m_debugCurrentBox);
    auto *currentButtonLayout = new QHBoxLayout();
    auto *currentStartButton = new QPushButton("进入PCBA电流测试", m_debugCurrentBox);
    auto *currentStopButton = new QPushButton("停止", m_debugCurrentBox);
    currentButtonLayout->addWidget(currentStartButton);
    currentButtonLayout->addWidget(currentStopButton);
    currentLayout->addLayout(currentButtonLayout);
    auto *currentPowerLayout = new QHBoxLayout();
    currentPowerLayout->addWidget(new QLabel("PCBA供电电压", m_debugCurrentBox));
    m_pcbaSupplyVoltageCombo = new QComboBox(m_debugCurrentBox);
    m_pcbaSupplyVoltageCombo->addItem("5V", 50);
    m_pcbaSupplyVoltageCombo->addItem("4.5V", 45);
    currentPowerLayout->addWidget(m_pcbaSupplyVoltageCombo);
    currentPowerLayout->addStretch(1);
    currentLayout->addLayout(currentPowerLayout);
    m_pcbaCurrent50mACheck = new QCheckBox("PB1共享低阻采样支路（0.2R+NMOS，mA模式）", m_debugCurrentBox);
    currentLayout->addWidget(m_pcbaCurrent50mACheck);
    m_pcbaCurrentStatusLabel = new QLabel(m_debugCurrentBox);
    m_pcbaCurrentStatusLabel->setWordWrap(true);
    m_pcbaCurrentStatusLabel->setStyleSheet("color: #475569;");
    currentLayout->addWidget(m_pcbaCurrentStatusLabel);
    m_pcbaCurrentTable = new QTableWidget(kChannelCount, 4, m_debugCurrentBox);
    m_pcbaCurrentTable->setHorizontalHeaderLabels({"通道", "电流 uA(已矫正)", "ADC原始码(未矫正)", "内部基准矫正系数"});
    m_pcbaCurrentTable->verticalHeader()->hide();
    m_pcbaCurrentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pcbaCurrentTable->setMaximumHeight(240);
    currentLayout->addWidget(m_pcbaCurrentTable);
    connect(currentStartButton, &QPushButton::clicked, this, &MainWindow::enterPcbaCurrentTest);
    connect(currentStopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    connect(m_pcbaSupplyVoltageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::handlePcbaSupplyVoltageChanged);
    connect(m_pcbaCurrent50mACheck, &QCheckBox::toggled, this, &MainWindow::setPcbaCurrent50mAEnabled);
    debugPageLayout->addWidget(m_debugCurrentBox);

    m_debugSinglePcbaBox = new QGroupBox("单PCBA全流程测试", debugPage);
    auto *singlePcbaLayout = new QVBoxLayout(m_debugSinglePcbaBox);
    auto *singlePcbaHint = new QLabel("固定使用 1号位 UART1；该调试模式不等待工装压合/压合开关。页面只展示 MCU 与 PCBA 的串口测试链路：每条指令的真实下发、等待内容、PCBA回包、延迟和失败原因。", m_debugSinglePcbaBox);
    singlePcbaHint->setWordWrap(true);
    singlePcbaHint->setStyleSheet("color: #475569;");
    singlePcbaLayout->addWidget(singlePcbaHint);
    auto *singlePcbaButtonLayout = new QHBoxLayout();
    auto *singlePcbaStartButton = new QPushButton("开始单PCBA指令测试", m_debugSinglePcbaBox);
    auto *singlePcbaStopButton = new QPushButton("停止", m_debugSinglePcbaBox);
    singlePcbaButtonLayout->addWidget(singlePcbaStartButton);
    singlePcbaButtonLayout->addWidget(singlePcbaStopButton);
    singlePcbaLayout->addLayout(singlePcbaButtonLayout);
    m_singlePcbaStopOnFailCheck = new QCheckBox("遇到失败的检测项先停下来", m_debugSinglePcbaBox);
    m_singlePcbaStopOnFailCheck->setChecked(true);
    singlePcbaLayout->addWidget(m_singlePcbaStopOnFailCheck);
    m_singlePcbaStatusLabel = new QLabel(m_debugSinglePcbaBox);
    m_singlePcbaStatusLabel->setWordWrap(true);
    m_singlePcbaStatusLabel->setStyleSheet("color: #475569;");
    singlePcbaLayout->addWidget(m_singlePcbaStatusLabel);
    m_singlePcbaSummaryLabel = new QLabel(m_debugSinglePcbaBox);
    m_singlePcbaSummaryLabel->setWordWrap(true);
    m_singlePcbaSummaryLabel->setStyleSheet("font-weight: 700; color: #0f172a;");
    singlePcbaLayout->addWidget(m_singlePcbaSummaryLabel);
    m_singlePcbaCommandTable = new QTableWidget(kPcbaTimingStepCount, 6, m_debugSinglePcbaBox);
    m_singlePcbaCommandTable->setHorizontalHeaderLabels({"测试项目", "发送给PCBA", "PCBA回包", "延迟", "判定", "失败原因"});
    m_singlePcbaCommandTable->verticalHeader()->hide();
    m_singlePcbaCommandTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_singlePcbaCommandTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_singlePcbaCommandTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_singlePcbaCommandTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_singlePcbaCommandTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_singlePcbaCommandTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_singlePcbaCommandTable->setMinimumHeight(300);
    singlePcbaLayout->addWidget(m_singlePcbaCommandTable);
    singlePcbaLayout->addWidget(new QLabel("MCU <-> PCBA 串口明细", m_debugSinglePcbaBox));
    m_singlePcbaSerialLog = new QPlainTextEdit(m_debugSinglePcbaBox);
    m_singlePcbaSerialLog->setReadOnly(true);
    m_singlePcbaSerialLog->setMaximumBlockCount(500);
    m_singlePcbaSerialLog->setMinimumHeight(180);
    m_singlePcbaSerialLog->setFont(QFont("Cascadia Mono", 9));
    singlePcbaLayout->addWidget(m_singlePcbaSerialLog);
    connect(singlePcbaStartButton, &QPushButton::clicked, this, &MainWindow::startSinglePcbaFlow);
    connect(singlePcbaStopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    debugPageLayout->addWidget(m_debugSinglePcbaBox);

    m_debugSingleTankPcbaBox = new QGroupBox("单罐单PCBA测试", debugPage);
    auto *singleTankPcbaLayout = new QVBoxLayout(m_debugSingleTankPcbaBox);
    auto *singleTankPcbaHint = new QLabel("按固件内置单PCBA全流程执行：先0V 1s使PCBA彻底关机，再切5V等1s并用10uA档记录待机电流；随后切50mA档，开机+进测试命令发送2次，第二次前等待0.5s，记录开机后电流；低电前切4.5V等1s，正常前切回5V等0.5s，再按表格逐条记录MCU下发、PCBA回包、解析信息和实际延迟；回包最长等2s，250mmHg标定暂按预留步骤显示。", m_debugSingleTankPcbaBox);
    singleTankPcbaHint->setWordWrap(true);
    singleTankPcbaHint->setStyleSheet("color: #475569;");
    singleTankPcbaLayout->addWidget(singleTankPcbaHint);
    auto *singleTankPcbaButtonLayout = new QHBoxLayout();
    auto *singleTankPcbaStartButton = new QPushButton("开始单罐单PCBA测试", m_debugSingleTankPcbaBox);
    auto *singleTankPcbaStopButton = new QPushButton("停止", m_debugSingleTankPcbaBox);
    singleTankPcbaButtonLayout->addWidget(singleTankPcbaStartButton);
    singleTankPcbaButtonLayout->addWidget(singleTankPcbaStopButton);
    singleTankPcbaLayout->addLayout(singleTankPcbaButtonLayout);
    m_singleTankPcbaStatusLabel = new QLabel(m_debugSingleTankPcbaBox);
    m_singleTankPcbaStatusLabel->setWordWrap(true);
    m_singleTankPcbaStatusLabel->setStyleSheet("color: #475569;");
    singleTankPcbaLayout->addWidget(m_singleTankPcbaStatusLabel);
    m_singleTankPcbaSummaryLabel = new QLabel(m_debugSingleTankPcbaBox);
    m_singleTankPcbaSummaryLabel->setWordWrap(true);
    m_singleTankPcbaSummaryLabel->setStyleSheet("font-weight: 700; color: #0f172a;");
    singleTankPcbaLayout->addWidget(m_singleTankPcbaSummaryLabel);
    m_singleTankPcbaTable = new QTableWidget(0, 8, m_debugSingleTankPcbaBox);
    m_singleTankPcbaTable->setHorizontalHeaderLabels({"测试项目", "动作/档位", "发送给PCBA", "PCBA回包", "解析信息", "延迟", "电流", "判定/原因"});
    m_singleTankPcbaTable->verticalHeader()->hide();
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_singleTankPcbaTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    m_singleTankPcbaTable->setMinimumHeight(360);
    singleTankPcbaLayout->addWidget(m_singleTankPcbaTable);
    singleTankPcbaLayout->addWidget(new QLabel("MCU <-> PCBA 串口明细", m_debugSingleTankPcbaBox));
    m_singleTankPcbaSerialLog = new QPlainTextEdit(m_debugSingleTankPcbaBox);
    m_singleTankPcbaSerialLog->setReadOnly(true);
    m_singleTankPcbaSerialLog->setMaximumBlockCount(700);
    m_singleTankPcbaSerialLog->setMinimumHeight(220);
    m_singleTankPcbaSerialLog->setFont(QFont("Cascadia Mono", 9));
    singleTankPcbaLayout->addWidget(m_singleTankPcbaSerialLog);
    connect(singleTankPcbaStartButton, &QPushButton::clicked, this, &MainWindow::startSingleTankPcbaFlow);
    connect(singleTankPcbaStopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    debugPageLayout->addWidget(m_debugSingleTankPcbaBox);

    m_debugSingleTankBox = new QGroupBox("单罐体闭环测试", debugPage);
    auto *singleTankLayout = new QGridLayout(m_debugSingleTankBox);
    m_singleTankCombo = new QComboBox(m_debugSingleTankBox);
    for (int i = 0; i < kTankCount; ++i) {
        const auto &tank = tankSpecs()[i];
        m_singleTankCombo->addItem(tank.name, i);
    }
    m_singleTankTargetSpin = new QDoubleSpinBox(m_debugSingleTankBox);
    m_singleTankTargetSpin->setRange(0.0, 350.0);
    m_singleTankTargetSpin->setDecimals(1);
    m_singleTankTargetSpin->setSuffix(" mmHg");
    m_singleTankToleranceSpin = new QDoubleSpinBox(m_debugSingleTankBox);
    m_singleTankToleranceSpin->setRange(0.1, 20.0);
    m_singleTankToleranceSpin->setDecimals(1);
    m_singleTankToleranceSpin->setValue(3.0);
    m_singleTankToleranceSpin->setSuffix(" mmHg");
    m_singleTankStartButton = new QPushButton("启动单罐闭环", m_debugSingleTankBox);
    m_singleTankStopButton = new QPushButton("停止单罐闭环", m_debugSingleTankBox);
    m_singleTankStatusLabel = new QLabel(m_debugSingleTankBox);
    m_singleTankStatusLabel->setWordWrap(true);
    m_singleTankStatusLabel->setStyleSheet("color: #475569;");
    singleTankLayout->addWidget(new QLabel("罐体"), 0, 0);
    singleTankLayout->addWidget(m_singleTankCombo, 0, 1, 1, 2);
    singleTankLayout->addWidget(new QLabel("目标"), 1, 0);
    singleTankLayout->addWidget(m_singleTankTargetSpin, 1, 1);
    singleTankLayout->addWidget(new QLabel("容差"), 1, 2);
    singleTankLayout->addWidget(m_singleTankToleranceSpin, 1, 3);
    singleTankLayout->addWidget(m_singleTankStartButton, 2, 0, 1, 2);
    singleTankLayout->addWidget(m_singleTankStopButton, 2, 2, 1, 2);
    singleTankLayout->addWidget(m_singleTankStatusLabel, 3, 0, 1, 4);
    connect(m_singleTankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::handleSingleTankSelectionChanged);
    connect(m_singleTankStartButton, &QPushButton::clicked, this, &MainWindow::startSingleTankLoop);
    connect(m_singleTankStopButton, &QPushButton::clicked, this, &MainWindow::stopSingleTankLoop);
    handleSingleTankSelectionChanged(m_singleTankCombo->currentIndex());
    debugPageLayout->addWidget(m_debugSingleTankBox);

    m_debugManualBox = new QGroupBox("阈值与手动阀", debugPage);
    auto *paramLayout = new QGridLayout(m_debugManualBox);
    m_thresholdSpin = new QDoubleSpinBox(m_debugManualBox);
    m_thresholdSpin->setRange(0.1, 30.0);
    m_thresholdSpin->setDecimals(2);
    m_thresholdSpin->setValue(3.0);
    m_thresholdSpin->setSuffix(" mmHg");
    auto *thresholdButton = new QPushButton("下发阈值", m_debugManualBox);
    m_valveSpin = new QSpinBox(m_debugManualBox);
    m_valveSpin->setRange(1, kValveCount);
    m_valveActionCombo = new QComboBox(m_debugManualBox);
    m_valveActionCombo->addItems({"关闭", "打开"});
    auto *valveButton = new QPushButton("执行阀门", m_debugManualBox);
    paramLayout->addWidget(new QLabel("压力误差阈值"), 0, 0);
    paramLayout->addWidget(m_thresholdSpin, 0, 1);
    paramLayout->addWidget(thresholdButton, 0, 2);
    paramLayout->addWidget(new QLabel("阀号"), 1, 0);
    paramLayout->addWidget(m_valveSpin, 1, 1);
    paramLayout->addWidget(m_valveActionCombo, 1, 2);
    paramLayout->addWidget(valveButton, 2, 0, 1, 3);
    connect(thresholdButton, &QPushButton::clicked, this, &MainWindow::sendThreshold);
    connect(valveButton, &QPushButton::clicked, this, &MainWindow::sendManualValve);
    debugPageLayout->addWidget(m_debugManualBox);

    m_debugAdcBox = new QGroupBox("ADC实时基准", debugPage);
    auto *adcLayout = new QGridLayout(m_debugAdcBox);
    m_adcReferenceStatusLabel = new QLabel(m_debugAdcBox);
    m_adcReferenceVddaLabel = new QLabel(m_debugAdcBox);
    m_adcReferenceRawLabel = new QLabel(m_debugAdcBox);
    m_adcReferenceScaleLabel = new QLabel(m_debugAdcBox);
    auto *adcCalibrateButton = new QPushButton("立即刷新内部基准", m_debugAdcBox);
    adcLayout->addWidget(new QLabel("状态"), 0, 0);
    adcLayout->addWidget(m_adcReferenceStatusLabel, 0, 1, 1, 2);
    adcLayout->addWidget(new QLabel("VDDA"), 1, 0);
    adcLayout->addWidget(m_adcReferenceVddaLabel, 1, 1, 1, 2);
    adcLayout->addWidget(new QLabel("VREFINT raw"), 2, 0);
    adcLayout->addWidget(m_adcReferenceRawLabel, 2, 1, 1, 2);
    adcLayout->addWidget(new QLabel("实时修正系数"), 3, 0);
    adcLayout->addWidget(m_adcReferenceScaleLabel, 3, 1, 1, 2);
    adcLayout->addWidget(adcCalibrateButton, 4, 0, 1, 3);
    connect(adcCalibrateButton, &QPushButton::clicked, this, &MainWindow::sendAdcCalibration);
    debugPageLayout->addWidget(m_debugAdcBox);

    m_debugRtcBox = new QGroupBox("RTC时钟调试模式", debugPage);
    auto *rtcLayout = new QGridLayout(m_debugRtcBox);
    m_rtcTimeLabel = new QLabel(m_debugRtcBox);
    m_rtcBatteryLabel = new QLabel(m_debugRtcBox);
    m_rtcOscillatorLabel = new QLabel(m_debugRtcBox);
    m_rtcDateTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime(), m_debugRtcBox);
    m_rtcDateTimeEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    m_rtcDateTimeEdit->setCalendarPopup(true);
    auto *rtcNowButton = new QPushButton("设为电脑当前时间", m_debugRtcBox);
    auto *rtcSendButton = new QPushButton("下发RTC时间", m_debugRtcBox);
    rtcLayout->addWidget(new QLabel("当前RTC"), 0, 0);
    rtcLayout->addWidget(m_rtcTimeLabel, 0, 1, 1, 2);
    rtcLayout->addWidget(new QLabel("电子电池"), 1, 0);
    rtcLayout->addWidget(m_rtcBatteryLabel, 1, 1, 1, 2);
    rtcLayout->addWidget(new QLabel("晶振"), 2, 0);
    rtcLayout->addWidget(m_rtcOscillatorLabel, 2, 1, 1, 2);
    rtcLayout->addWidget(new QLabel("设置时间"), 3, 0);
    rtcLayout->addWidget(m_rtcDateTimeEdit, 3, 1, 1, 2);
    rtcLayout->addWidget(rtcNowButton, 4, 0, 1, 2);
    rtcLayout->addWidget(rtcSendButton, 4, 2);
    connect(rtcNowButton, &QPushButton::clicked, this, &MainWindow::setRtcEditorToComputerTime);
    connect(rtcSendButton, &QPushButton::clicked, this, &MainWindow::sendRtcTime);
    debugPageLayout->addWidget(m_debugRtcBox);

    m_debugFirmwareBox = new QGroupBox("固件烧录", debugPage);
    auto *firmwareLayout = new QGridLayout(m_debugFirmwareBox);
    m_firmwareHexEdit = new QLineEdit(defaultFirmwareHexPath(), m_debugFirmwareBox);
    m_jlinkPathEdit = new QLineEdit(defaultJLinkPath(), m_debugFirmwareBox);
    m_firmwareStatusLabel = new QLabel("等待烧录", m_debugFirmwareBox);
    m_firmwareStatusLabel->setWordWrap(true);
    m_firmwareStatusLabel->setStyleSheet("color: #475569;");
    m_firmwareLog = new QPlainTextEdit(m_debugFirmwareBox);
    m_firmwareLog->setReadOnly(true);
    m_firmwareLog->setMaximumBlockCount(400);
    m_firmwareLog->setMinimumHeight(180);
    auto *firmwareBrowseButton = new QPushButton("选择HEX", m_debugFirmwareBox);
    m_firmwareDownloadButton = new QPushButton("下载到板子", m_debugFirmwareBox);
    firmwareLayout->addWidget(new QLabel("烧录文件"), 0, 0);
    firmwareLayout->addWidget(m_firmwareHexEdit, 0, 1);
    firmwareLayout->addWidget(firmwareBrowseButton, 0, 2);
    firmwareLayout->addWidget(new QLabel("J-Link"), 1, 0);
    firmwareLayout->addWidget(m_jlinkPathEdit, 1, 1, 1, 2);
    firmwareLayout->addWidget(m_firmwareStatusLabel, 2, 0, 1, 3);
    firmwareLayout->addWidget(m_firmwareDownloadButton, 3, 0, 1, 3);
    firmwareLayout->addWidget(m_firmwareLog, 4, 0, 1, 3);
    connect(firmwareBrowseButton, &QPushButton::clicked, this, &MainWindow::browseFirmwareHex);
    connect(m_firmwareDownloadButton, &QPushButton::clicked, this, &MainWindow::startFirmwareDownload);
    debugPageLayout->addWidget(m_debugFirmwareBox);
    debugPageLayout->addStretch(1);

    m_modeTabs->addTab(productionPage, "生产模式");
    m_modeTabs->addTab(debugPage, "调试模式");
    m_modeTabs->setCurrentIndex(static_cast<int>(HostRunMode::Production));
    m_modeTabs->tabBar()->hide();
    connect(m_modeTabs, &QTabWidget::currentChanged, this, &MainWindow::handleModeChanged);
    layout->addWidget(m_modeTabs);

    m_valveTable = new QTableWidget(kValveCount, 2, panel);
    m_valveTable->setHorizontalHeaderLabels({"阀门", "状态"});
    m_valveTable->verticalHeader()->hide();
    m_valveTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_valveTable->setMaximumHeight(96);
    layout->addWidget(m_valveTable);

    m_pressureTable = new QTableWidget(kPressureSensorCount, 2, panel);
    m_pressureTable->setHorizontalHeaderLabels({"压力检测", "mmHg"});
    m_pressureTable->verticalHeader()->hide();
    m_pressureTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pressureTable->setMaximumHeight(110);
    connect(m_pressureTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        openSensorCalibration(row + 1);
    });
    layout->addWidget(m_pressureTable);

    m_pcbaTable = new QTableWidget(kChannelCount, 5, panel);
    m_pcbaTable->setHorizontalHeaderLabels({"通道", "连接", "夹具", "PCBA", "判定"});
    m_pcbaTable->verticalHeader()->hide();
    m_pcbaTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pcbaTable->setMaximumHeight(120);
    layout->addWidget(m_pcbaTable);
    refreshStatusTablesVisibility();

    m_log = new QPlainTextEdit(panel);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setMinimumHeight(340);
    m_log->setPlaceholderText("上位机控制日志");
    layout->addWidget(m_log, 1);

    scroll->setWidget(panel);
    return scroll;
}

MainWindow::HostRunMode MainWindow::currentMode() const
{
    if (!m_modeTabs) {
        return HostRunMode::Production;
    }
    return m_modeTabs->currentIndex() == static_cast<int>(HostRunMode::Debug)
        ? HostRunMode::Debug
        : HostRunMode::Production;
}

bool MainWindow::isDebugMode() const
{
    return currentMode() == HostRunMode::Debug;
}

void MainWindow::handleModeChanged(int index)
{
    Q_UNUSED(index)
    updateModeUi();
    appendLog(QString("切换到%1").arg(isDebugMode() ? "调试模式" : "生产模式"));
}

void MainWindow::selectProductionMode()
{
    if (m_modeTabs) {
        m_modeTabs->setCurrentIndex(static_cast<int>(HostRunMode::Production));
    }
}

void MainWindow::selectDebugMode()
{
    if (m_modeTabs) {
        m_modeTabs->setCurrentIndex(static_cast<int>(HostRunMode::Debug));
    }
}

void MainWindow::handleLeftItemChanged()
{
    if (!m_flowList || !m_flowList->currentItem()) {
        return;
    }

    if (isDebugMode()) {
        const DebugTool tool = selectedDebugTool();
        showDebugTool(tool);
        if (m_transport.isOpen()) {
            if (tool == DebugTool::PcbaCurrent && m_snapshot.state != RuntimeState::PcbaCurrentTest) {
                enterPcbaCurrentTest();
            } else if (tool == DebugTool::RtcDebug && m_snapshot.state != RuntimeState::RtcDebug) {
                sendFrame(usb::buildSetState(nextSequence(), RuntimeState::RtcDebug),
                          "SET_STATE " + stateDisplayName(RuntimeState::RtcDebug));
            }
        }
    }

    updateFlowList();
    refreshStatusTablesVisibility();
}

void MainWindow::updateModeUi()
{
    const bool debugMode = isDebugMode();
    if (!debugMode && m_singleTankRunning) {
        stopSingleTankLoop();
    }

    rebuildFlowList();
    if (m_modeHintLabel) {
        m_modeHintLabel->setText(debugMode
            ? "当前控制页: 调试模式"
            : "当前控制页: 生产模式");
    }
    if (m_productionModeButton && m_debugModeButton) {
        m_productionModeButton->setEnabled(debugMode);
        m_debugModeButton->setEnabled(!debugMode);
        m_productionModeButton->setStyleSheet(debugMode
            ? QString()
            : "font-weight: 700; background: #dbeafe; color: #0f172a;");
        m_debugModeButton->setStyleSheet(debugMode
            ? "font-weight: 700; background: #fde68a; color: #92400e;"
            : QString());
    }
    if (m_flowHintLabel) {
        m_flowHintLabel->setText(debugMode
            ? "左侧高亮表示当前打开的调试工具；“运行中”表示 MCU 当前状态。"
            : "生产模式下仅显示生产自动流程。");
    }

    if (m_modeTabs) {
        m_modeTabs->setCurrentIndex(static_cast<int>(debugMode ? HostRunMode::Debug : HostRunMode::Production));
    }
    if (debugMode) {
        showDebugTool(selectedDebugTool());
    }
    updateSingleTankPanel();
    refreshStatusTablesVisibility();
}

void MainWindow::rebuildFlowList()
{
    if (!m_flowList) {
        return;
    }

    const QSignalBlocker blocker(m_flowList);
    const bool debugMode = isDebugMode();
    int previousKind = -1;
    int previousValue = -1;
    if (m_flowList->currentItem()) {
        previousKind = m_flowList->currentItem()->data(kLeftKindRole).toInt();
        previousValue = m_flowList->currentItem()->data(kLeftValueRole).toInt();
    }

    m_flowList->clear();
    if (debugMode) {
        const DebugTool tools[] = {
            DebugTool::UsbMsc,
            DebugTool::PcbaCurrent,
            DebugTool::SinglePcbaFlow,
            DebugTool::SingleTankPcba,
            DebugTool::SingleTank,
            DebugTool::ManualValve,
            DebugTool::AdcReference,
            DebugTool::RtcDebug,
            DebugTool::FirmwareDownload
        };
        for (const DebugTool tool : tools) {
            const int value = static_cast<int>(tool);
            auto *item = new QListWidgetItem(debugToolDisplayName(value), m_flowList);
            item->setData(kLeftKindRole, static_cast<int>(LeftItemKind::DebugTool));
            item->setData(kLeftValueRole, value);
        }
    } else {
        for (int i = 0; i < stateIndex(RuntimeState::Count); ++i) {
            const RuntimeState state = stateFromIndex(i);
            if (!stateVisibleForMode(state, false)) {
                continue;
            }
            auto *item = new QListWidgetItem(stateDisplayName(state), m_flowList);
            item->setData(kLeftKindRole, static_cast<int>(LeftItemKind::RuntimeState));
            item->setData(kLeftValueRole, i);
        }
    }

    int rowToSelect = -1;
    for (int row = 0; row < m_flowList->count(); ++row) {
        const auto *item = m_flowList->item(row);
        const auto kind = static_cast<LeftItemKind>(item->data(kLeftKindRole).toInt());
        const int value = item->data(kLeftValueRole).toInt();
        if (debugMode && kind == LeftItemKind::DebugTool) {
            if (rowToSelect < 0 &&
                previousKind == static_cast<int>(LeftItemKind::DebugTool) &&
                previousValue == value) {
                rowToSelect = row;
            }
        } else if (!debugMode && kind == LeftItemKind::RuntimeState) {
            const RuntimeState state = stateFromIndex(value);
            if (state == m_snapshot.state) {
                rowToSelect = row;
                break;
            }
            if (rowToSelect < 0 &&
                previousKind == static_cast<int>(LeftItemKind::RuntimeState) &&
                previousValue == value) {
                rowToSelect = row;
            }
        }
    }
    if (rowToSelect < 0 && m_flowList->count() > 0) {
        rowToSelect = 0;
    }
    if (rowToSelect >= 0) {
        m_flowList->setCurrentRow(rowToSelect);
    }

    updateFlowList();
}

RuntimeState MainWindow::selectedFlowState() const
{
    if (!m_flowList || !m_flowList->currentItem()) {
        return RuntimeState::Ready;
    }
    const auto kind = static_cast<LeftItemKind>(m_flowList->currentItem()->data(kLeftKindRole).toInt());
    const int value = m_flowList->currentItem()->data(kLeftValueRole).toInt();
    if (kind == LeftItemKind::RuntimeState) {
        return stateFromIndex(value);
    }
    switch (static_cast<DebugTool>(value)) {
    case DebugTool::UsbMsc: return RuntimeState::UsbMsc;
    case DebugTool::PcbaCurrent: return RuntimeState::PcbaCurrentTest;
    case DebugTool::SinglePcbaFlow: return RuntimeState::SinglePcbaFlow;
    case DebugTool::SingleTankPcba:
    case DebugTool::SingleTank:
    case DebugTool::ManualValve:
    case DebugTool::AdcReference:
    case DebugTool::FirmwareDownload:
        return RuntimeState::Ready;
    case DebugTool::RtcDebug: return RuntimeState::RtcDebug;
    }
    return RuntimeState::Ready;
}

MainWindow::DebugTool MainWindow::selectedDebugTool() const
{
    if (!m_flowList || !m_flowList->currentItem()) {
        return DebugTool::UsbMsc;
    }
    const auto kind = static_cast<LeftItemKind>(m_flowList->currentItem()->data(kLeftKindRole).toInt());
    if (kind != LeftItemKind::DebugTool) {
        return DebugTool::UsbMsc;
    }
    const int value = m_flowList->currentItem()->data(kLeftValueRole).toInt();
    if (value < static_cast<int>(DebugTool::UsbMsc) || value > static_cast<int>(DebugTool::FirmwareDownload)) {
        return DebugTool::UsbMsc;
    }
    return static_cast<DebugTool>(value);
}

void MainWindow::showDebugTool(DebugTool tool)
{
    const bool debugMode = isDebugMode();
    if (m_debugFlowBox) {
        m_debugFlowBox->setVisible(debugMode && tool == DebugTool::UsbMsc);
    }
    if (m_debugCurrentBox) {
        m_debugCurrentBox->setVisible(debugMode && tool == DebugTool::PcbaCurrent);
    }
    if (m_debugSinglePcbaBox) {
        m_debugSinglePcbaBox->setVisible(debugMode && tool == DebugTool::SinglePcbaFlow);
    }
    if (m_debugSingleTankPcbaBox) {
        m_debugSingleTankPcbaBox->setVisible(debugMode && tool == DebugTool::SingleTankPcba);
    }
    if (m_debugSingleTankBox) {
        m_debugSingleTankBox->setVisible(debugMode && tool == DebugTool::SingleTank);
    }
    if (m_debugManualBox) {
        m_debugManualBox->setVisible(debugMode && tool == DebugTool::ManualValve);
    }
    if (m_debugAdcBox) {
        m_debugAdcBox->setVisible(debugMode && tool == DebugTool::AdcReference);
    }
    if (m_debugRtcBox) {
        m_debugRtcBox->setVisible(debugMode && tool == DebugTool::RtcDebug);
    }
    if (m_debugFirmwareBox) {
        m_debugFirmwareBox->setVisible(debugMode && tool == DebugTool::FirmwareDownload);
    }
    refreshStatusTablesVisibility();
}

bool MainWindow::activateSelectedLeftItem()
{
    if (!m_flowList || !m_flowList->currentItem()) {
        statusBar()->showMessage("未选择左侧项目", 3000);
        return false;
    }

    const auto kind = static_cast<LeftItemKind>(m_flowList->currentItem()->data(kLeftKindRole).toInt());
    if (kind == LeftItemKind::RuntimeState) {
        if (!isDebugMode()) {
            appendLog("生产模式禁止直接切换流程状态");
            statusBar()->showMessage("请切换到调试模式后再切换流程状态", 3000);
            return false;
        }
        const RuntimeState state = selectedFlowState();
        sendFrame(usb::buildSetState(nextSequence(), state), "SET_STATE " + stateDisplayName(state));
        return true;
    }

    if (!isDebugMode()) {
        selectDebugMode();
    }

    const DebugTool tool = selectedDebugTool();
    showDebugTool(tool);
    switch (tool) {
    case DebugTool::UsbMsc:
        sendEnterMsc();
        return true;
    case DebugTool::PcbaCurrent:
        enterPcbaCurrentTest();
        return true;
    case DebugTool::SinglePcbaFlow:
        statusBar()->showMessage("已打开单PCBA全流程测试", 3000);
        return true;
    case DebugTool::SingleTankPcba:
        requestSingleTankPcbaReport();
        statusBar()->showMessage("已打开单罐单PCBA测试", 3000);
        return true;
    case DebugTool::SingleTank:
        statusBar()->showMessage("已打开单罐体闭环测试", 3000);
        return true;
    case DebugTool::ManualValve:
        statusBar()->showMessage("已打开阈值与手动阀", 3000);
        return true;
    case DebugTool::AdcReference:
        statusBar()->showMessage("已打开ADC实时基准", 3000);
        return true;
    case DebugTool::RtcDebug:
        sendFrame(usb::buildSetState(nextSequence(), RuntimeState::RtcDebug),
                  "SET_STATE " + stateDisplayName(RuntimeState::RtcDebug));
        return true;
    case DebugTool::FirmwareDownload:
        statusBar()->showMessage("已打开固件烧录", 3000);
        return true;
    }
    return false;
}

void MainWindow::updateSingleTankPanel()
{
    if (!m_singleTankCombo || !m_singleTankTargetSpin || !m_singleTankToleranceSpin ||
        !m_singleTankStartButton || !m_singleTankStopButton || !m_singleTankStatusLabel) {
        return;
    }

    const bool enabled = isDebugMode();
    const bool editable = enabled && !m_singleTankRunning;
    m_singleTankCombo->setEnabled(editable);
    m_singleTankTargetSpin->setEnabled(editable);
    m_singleTankToleranceSpin->setEnabled(editable);
    m_singleTankStartButton->setEnabled(editable);
    m_singleTankStopButton->setEnabled(enabled && m_singleTankRunning);

    if (!m_singleTankRunning) {
        const int index = m_singleTankCombo->currentIndex();
        if (index >= 0 && index < kTankCount) {
            const auto &tank = tankSpecs()[index];
            m_singleTankStatusLabel->setText(QString("%1 | 目标 %2 mmHg | 入口阀%3 / 出口阀%4 / 泄压阀%5 / 压力检测%6")
                                                 .arg(tank.name)
                                                 .arg(m_singleTankTargetSpin->value(), 0, 'f', 1)
                                                 .arg(tank.inletValve)
                                                 .arg(tank.outletValve)
                                                 .arg(tank.reliefValve)
                                                 .arg(tank.pressureSensor));
        } else {
            m_singleTankStatusLabel->setText("未选择罐体");
        }
    }
}

void MainWindow::handleSingleTankSelectionChanged(int index)
{
    if (index >= 0 && index < kTankCount && m_singleTankTargetSpin) {
        m_singleTankTargetSpin->setValue(tankSpecs()[index].targetMmHg);
    }
    updateSingleTankPanel();
}

void MainWindow::resetSingleTankLoopControl()
{
    m_singleTankAwaitingReady = false;
}

bool MainWindow::sendSingleTankLoopCommand(uint8_t tankIndex,
                                           double targetMmHg,
                                           double toleranceMmHg,
                                           bool enable,
                                           const QString &description)
{
    return sendFrame(usb::buildSingleTankLoop(nextSequence(),
                                              tankIndex,
                                              targetMmHg,
                                              toleranceMmHg,
                                              enable),
                     description);
}

void MainWindow::startSingleTankLoop()
{
    if (!isDebugMode()) {
        statusBar()->showMessage("请先切换到调试模式", 3000);
        appendLog("生产模式下未启动单罐闭环");
        return;
    }
    if (!m_transport.isOpen()) {
        statusBar()->showMessage("未连接 MCU，单罐闭环未启动", 3000);
        appendLog("未连接，单罐闭环未启动");
        return;
    }

    const int index = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
    if (index < 0 || index >= kTankCount) {
        QMessageBox::warning(this, "未选择罐体", "请选择要测试的罐体。");
        return;
    }

    resetSingleTankLoopControl();
    const auto &tank = tankSpecs()[index];
    if (sendSingleTankLoopCommand(static_cast<uint8_t>(index),
                                  m_singleTankTargetSpin->value(),
                                  m_singleTankToleranceSpin->value(),
                                  true,
                                  QString("单罐闭环启动: %1 目标 %2mmHg 容差 %3mmHg")
                                      .arg(tank.name)
                                      .arg(m_singleTankTargetSpin->value(), 0, 'f', 1)
                                      .arg(m_singleTankToleranceSpin->value(), 0, 'f', 1))) {
        m_singleTankRunning = true;
        m_singleTankAwaitingReady = true;
        m_singleTankStatusLabel->setText(QString("%1 | 已下发到 MCU，等待进入单罐闭环状态")
                                             .arg(tank.name));
        updateSingleTankPanel();
        m_singleTankTimer.start();
    }
}

void MainWindow::stopSingleTankLoop()
{
    const bool wasRunning = m_singleTankRunning;
    m_singleTankTimer.stop();

    const int index = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
    if (wasRunning && index >= 0 && index < kTankCount) {
        const auto &tank = tankSpecs()[index];
        if (sendSingleTankLoopCommand(static_cast<uint8_t>(index),
                                      m_singleTankTargetSpin ? m_singleTankTargetSpin->value() : tank.targetMmHg,
                                      m_singleTankToleranceSpin ? m_singleTankToleranceSpin->value() : 3.0,
                                      false,
                                      QString("单罐闭环停止: %1").arg(tank.name))) {
            appendLog(QString("单罐闭环停止: %1").arg(tank.name));
        }
    }

    m_singleTankRunning = false;
    resetSingleTankLoopControl();
    updateSingleTankPanel();
}

void MainWindow::serviceSingleTankLoop()
{
    if (!m_singleTankRunning) {
        return;
    }

    if (!m_transport.isOpen()) {
        m_singleTankTimer.stop();
        m_singleTankRunning = false;
        updateSingleTankPanel();
        appendLog("连接断开，单罐闭环已停止");
        return;
    }

    const int index = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
    if (index < 0 || index >= kTankCount) {
        stopSingleTankLoop();
        return;
    }

    const auto &tank = tankSpecs()[index];
    const int sensorIndex = tank.pressureSensor - 1;
    const bool inLoopState = m_snapshot.state == RuntimeState::SingleTankLoop;

    if (pressureSensorFaultLatched(m_snapshot, sensorIndex)) {
        m_singleTankStatusLabel->setText(QString("%1 | 压力检测%2%3 | MCU状态=%4")
                                             .arg(tank.name)
                                             .arg(tank.pressureSensor)
                                             .arg(sensorFaultUiText(m_snapshot, sensorIndex))
                                             .arg(stateDisplayName(m_snapshot.state)));
        return;
    }
    if (!pressureSensorValid(m_snapshot, sensorIndex)) {
        m_singleTankStatusLabel->setText(QString("%1 | 压力检测%2%3 | MCU状态=%4")
                                             .arg(tank.name)
                                             .arg(tank.pressureSensor)
                                             .arg(sensorFaultUiText(m_snapshot, sensorIndex).isEmpty()
                                                      ? QStringLiteral("无有效读数")
                                                      : sensorFaultUiText(m_snapshot, sensorIndex))
                                             .arg(stateDisplayName(m_snapshot.state)));
        return;
    }

    const double current = toMmHg(m_snapshot.pressure001mmHg[sensorIndex]);
    const double target = m_singleTankTargetSpin->value();
    const double tolerance = m_singleTankToleranceSpin->value();
    if (inLoopState) {
        m_singleTankAwaitingReady = false;
        m_singleTankStatusLabel->setText(QString("%1 | 实时 %2 mmHg / 目标 %3±%4 mmHg | MCU单罐闭环运行中")
                                         .arg(tank.name)
                                         .arg(current, 0, 'f', 1)
                                         .arg(target, 0, 'f', 1)
                                         .arg(tolerance, 0, 'f', 1));
        return;
    }

    if (!m_singleTankAwaitingReady) {
        appendLog(QString("单罐闭环等待进入状态: 当前状态=%1").arg(stateDisplayName(m_snapshot.state)));
    }
    m_singleTankAwaitingReady = true;
    m_singleTankStatusLabel->setText(QString("%1 | 实时 %2 mmHg / 目标 %3±%4 mmHg | 等待 MCU 进入单罐闭环，当前 %5")
                                         .arg(tank.name)
                                         .arg(current, 0, 'f', 1)
                                         .arg(target, 0, 'f', 1)
                                         .arg(tolerance, 0, 'f', 1)
                                         .arg(stateDisplayName(m_snapshot.state)));
}

void MainWindow::refreshPorts()
{
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();
    const auto ports = WindowsSerialTransport::availablePortInfos();
    int preferredIndex = -1;
    bool preferredIsSegger = false;
    bool preferredIsRtt = false;
    int usbCdcIndex = -1;
    int jlinkVcomIndex = -1;
    int anyIndex = -1;
    for (const auto &port : ports) {
        m_portCombo->addItem(port.displayName, port.portName);
        if (port.displayName == current || port.portName == current) {
            preferredIndex = m_portCombo->count() - 1;
            preferredIsSegger = port.isSegger;
            preferredIsRtt = port.isRtt;
        }
        if (usbCdcIndex < 0 && !port.isSegger && !port.isRtt) {
            usbCdcIndex = m_portCombo->count() - 1;
        }
        if (jlinkVcomIndex < 0 && port.isSegger && !port.isRtt) {
            jlinkVcomIndex = m_portCombo->count() - 1;
        }
        if (anyIndex < 0) {
            anyIndex = m_portCombo->count() - 1;
        }
    }
    if (preferredIndex >= 0 &&
        !(usbCdcIndex >= 0 && !m_transport.isOpen() && (preferredIsSegger || preferredIsRtt))) {
        m_portCombo->setCurrentIndex(preferredIndex);
    } else if (usbCdcIndex >= 0) {
        m_portCombo->setCurrentIndex(usbCdcIndex);
    } else if (jlinkVcomIndex >= 0) {
        m_portCombo->setCurrentIndex(jlinkVcomIndex);
    } else if (anyIndex >= 0) {
        m_portCombo->setCurrentIndex(anyIndex);
    }
    appendLog(QString("发现 %1 个连接入口").arg(ports.size()));
}

void MainWindow::resetConnectAttempts()
{
    m_connectCandidatePorts.clear();
    m_connectCandidateDisplays.clear();
    m_deferredConnectPorts.clear();
    m_deferredConnectDisplays.clear();
    m_connectCandidateIndex = -1;
    m_connectDeferredRound = 0;
    m_helloRetryCount = 0;
}

void MainWindow::buildConnectCandidates(const QString &selectedPortName)
{
    resetConnectAttempts();

    const QVector<WindowsSerialTransport::PortInfo> ports = WindowsSerialTransport::availablePortInfos();
    auto appendCandidate = [this](const WindowsSerialTransport::PortInfo &candidate) {
        if (candidate.portName.isEmpty()) {
            return;
        }
        if (m_connectCandidatePorts.contains(candidate.portName)) {
            return;
        }
        m_connectCandidatePorts.push_back(candidate.portName);
        m_connectCandidateDisplays.push_back(candidate.displayName.isEmpty() ? candidate.portName
                                                                             : candidate.displayName);
    };

    for (const auto &port : ports) {
        if (port.portName == selectedPortName) {
            appendCandidate(port);
            break;
        }
    }
    for (const auto &port : ports) {
        if (!port.isSegger && !port.isRtt) {
            appendCandidate(port);
        }
    }
    for (const auto &port : ports) {
        appendCandidate(port);
    }

    if (m_connectCandidatePorts.isEmpty() && !selectedPortName.isEmpty()) {
        m_connectCandidatePorts.push_back(selectedPortName);
        m_connectCandidateDisplays.push_back(m_portCombo->currentText().isEmpty() ? selectedPortName
                                                                                   : m_portCombo->currentText());
    }
}

bool MainWindow::tryOpenNextConnectCandidate()
{
    const int baudRate = m_baudCombo->currentText().toInt();
    while (true) {
        while (++m_connectCandidateIndex < m_connectCandidatePorts.size()) {
            const QString portName = m_connectCandidatePorts.at(m_connectCandidateIndex);
            const QString displayName = m_connectCandidateDisplays.value(m_connectCandidateIndex, portName);
            appendLog(QString("尝试连接 %1").arg(displayName));
            if (m_transport.open(portName, baudRate)) {
                const int comboIndex = m_portCombo->findData(portName);
                if (comboIndex >= 0) {
                    m_portCombo->setCurrentIndex(comboIndex);
                }
                m_rxBuffer.clear();
                m_rxDiscardBurstCount = 0;
                m_waitingForHello = true;
                m_helloRetryCount = 0;
                appendLog(QString("已打开 %1，等待 MCU HELLO 响应").arg(displayName));
                sendFrame(usb::buildHello(nextSequence()), "HELLO");
                m_handshakeTimer.start(1500);
                return true;
            }

            const QString errorText = m_transport.lastError();
            const bool deviceNotReady = errorText.contains(QStringLiteral("Win32=31")) ||
                                        errorText.contains(QStringLiteral("设备没有发挥作用"));
            appendLog(QString("打开 %1 失败: %2").arg(displayName, errorText));
            if (deviceNotReady &&
                !m_deferredConnectPorts.contains(portName) &&
                m_connectDeferredRound < kPortNotReadyRetryRounds) {
                m_deferredConnectPorts.push_back(portName);
                m_deferredConnectDisplays.push_back(displayName);
                appendLog(QString("%1 可能刚完成枚举，稍后再重试").arg(displayName));
            }
        }

        if (m_deferredConnectPorts.isEmpty() || m_connectDeferredRound >= kPortNotReadyRetryRounds) {
            break;
        }

        ++m_connectDeferredRound;
        appendLog(QString("等待 %1 ms 后重试未就绪入口，第 %2/%3 轮")
                      .arg(kPortNotReadyRetryDelayMs)
                      .arg(m_connectDeferredRound)
                      .arg(kPortNotReadyRetryRounds));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(static_cast<unsigned long>(kPortNotReadyRetryDelayMs));
        refreshPorts();
        m_connectCandidatePorts = m_deferredConnectPorts;
        m_connectCandidateDisplays = m_deferredConnectDisplays;
        m_connectCandidateIndex = -1;
        m_deferredConnectPorts.clear();
        m_deferredConnectDisplays.clear();
    }

    m_waitingForHello = false;
    m_handshakeTimer.stop();
    appendLog("所有连接入口都已尝试，仍未连上目标 MCU");
    resetConnectAttempts();
    return false;
}

void MainWindow::connectOrDisconnect()
{
    if (m_transport.isOpen()) {
        resetConnectAttempts();
        m_transport.close();
        return;
    }
    if (m_portCombo->currentData().toString().isEmpty()) {
        QMessageBox::information(this, "未找到连接入口", "当前没有可打开的串口。请确认目标板 USB 已接入电脑，或虚拟串口驱动已正常枚举。");
        return;
    }
    const QString selectedPortName = m_portCombo->currentData().toString();
    buildConnectCandidates(selectedPortName);
    if (!tryOpenNextConnectCandidate()) {
        QMessageBox::warning(this, "连接失败",
                             "所有已发现的连接入口都尝试过了，但还没有收到目标 MCU 的响应。请确认 USB 线、固件枚举和当前模式。");
    }
}

void MainWindow::handleSerialBytes(const QByteArray &bytes)
{
    m_rxBuffer.append(bytes);
    while (!m_rxBuffer.isEmpty()) {
        const auto parsed = usb::parseOne(m_rxBuffer);
        if (parsed.needMore) {
            return;
        }
        const QByteArray droppedBytes = parsed.consumed > 0
            ? m_rxBuffer.left(static_cast<int>(parsed.consumed))
            : QByteArray();
        if (parsed.consumed > 0) {
            m_rxBuffer.remove(0, static_cast<int>(parsed.consumed));
        }
        if (!parsed.ok) {
            if (!parsed.error.isEmpty()) {
                ++m_rxDiscardBurstCount;
                if (m_rxDiscardBurstCount <= 3 || (m_rxDiscardBurstCount % 20) == 0) {
                    appendLog(QString("RX 丢弃(%1): %2")
                                  .arg(m_rxDiscardBurstCount)
                                  .arg(parsed.error));
                }
            }
            continue;
        }
        if (m_rxDiscardBurstCount > 3) {
            appendLog(QString("RX 丢弃汇总: 连续 %1 次，随后已重新同步到有效帧").arg(m_rxDiscardBurstCount));
        }
        m_rxDiscardBurstCount = 0;
        appendLog("RX " + usb::frameSummary(parsed.frame));
        if (parsed.frame.command == usb::Hello) {
            m_waitingForHello = false;
            m_handshakeTimer.stop();
            m_helloRetryCount = 0;
            resetConnectAttempts();
            sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::GetStatus), "GET_STATUS");
        }
        if (parsed.frame.command == usb::Ack && parsed.frame.payload.size() >= 2) {
            const uint8_t acceptedCommand = static_cast<uint8_t>(parsed.frame.payload[0]);
            if (acceptedCommand == usb::RunPcbaTiming && m_singlePcbaStartPending && m_singlePcbaStatusLabel) {
                m_singlePcbaStatusLabel->setText("命令已确认 | 单PCBA指令诊断，不等待工装压合，正在对 1号位 UART1 测试。");
                m_singlePcbaTimingRunning = true;
                m_singlePcbaTimingPollTimer.start();
            }
            if (acceptedCommand == usb::RunSingleTankPcba && m_singleTankPcbaStartPending && m_singleTankPcbaStatusLabel) {
                m_singleTankPcbaStatusLabel->setText("命令已确认 | MCU 正在按单罐单PCBA流程测试 1号位。");
                m_singleTankPcbaRunning = true;
                m_singleTankPcbaPollTimer.start();
            }
            if (acceptedCommand == usb::SingleTankLoop && m_singleTankRunning && m_singleTankStatusLabel) {
                const int index = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
                const QString tankName = (index >= 0 && index < kTankCount) ? tankSpecs()[index].name : QString("单罐闭环");
                m_singleTankStatusLabel->setText(QString("%1 | MCU已确认启动命令，等待进入单罐闭环状态").arg(tankName));
            }
        }
        if (parsed.frame.command == usb::Nak && parsed.frame.payload.size() >= 2) {
            const uint8_t rejectedCommand = static_cast<uint8_t>(parsed.frame.payload[0]);
            const uint8_t errorCode = static_cast<uint8_t>(parsed.frame.payload[1]);
            const QString errorText = usbErrorDisplayName(errorCode);
            appendLog(QString("RX NAK %1 | %2").arg(commandName(rejectedCommand), errorText));
            if (rejectedCommand == usb::RunPcbaTiming && m_singlePcbaStartPending) {
                m_singlePcbaStartPending = false;
                m_singlePcbaTimingRunning = false;
                m_singlePcbaTimingPollTimer.stop();
                if (m_singlePcbaStatusLabel) {
                    m_singlePcbaStatusLabel->setText(
                        QString("启动失败 | MCU拒绝“单PCBA指令计时测试”：%1。请确认 MCU 固件已包含 RUN_PCBA_TIMING。")
                            .arg(errorText));
                }
                statusBar()->showMessage(QString("单PCBA指令测试启动失败：%1").arg(errorText), 6000);
            }
            if (rejectedCommand == usb::RunSingleTankPcba && m_singleTankPcbaStartPending) {
                m_singleTankPcbaStartPending = false;
                m_singleTankPcbaRunning = false;
                m_singleTankPcbaPollTimer.stop();
                if (m_singleTankPcbaStatusLabel) {
                    m_singleTankPcbaStatusLabel->setText(
                        QString("启动失败 | MCU拒绝“单罐单PCBA测试”：%1。请确认 MCU 固件已包含 RUN_SINGLE_TANK_PCBA。")
                            .arg(errorText));
                }
                statusBar()->showMessage(QString("单罐单PCBA测试启动失败：%1").arg(errorText), 6000);
            }
            if (rejectedCommand == usb::SingleTankLoop) {
                m_singleTankTimer.stop();
                m_singleTankRunning = false;
                resetSingleTankLoopControl();
                updateSingleTankPanel();
                if (m_singleTankStatusLabel) {
                    m_singleTankStatusLabel->setText(QString("单罐闭环启动失败 | MCU拒绝命令：%1").arg(errorText));
                }
                statusBar()->showMessage(QString("单罐闭环命令被 MCU 拒绝：%1").arg(errorText), 5000);
            }
        }
        if (parsed.frame.command == usb::StatusSnapshot) {
            FixtureSnapshot incoming = m_snapshot;
            incoming.linkMode = LinkMode::UsbCdc;
            if (usb::applyStatusSnapshot(parsed.frame.payload, incoming)) {
                applySnapshot(incoming);
            } else {
                appendLog("STATUS_SNAPSHOT 长度不足，等待固件接入完整快照");
            }
        }
        if (parsed.frame.command == usb::GetPcbaTiming) {
            PcbaTimingReport report;
            if (usb::parsePcbaTimingReport(parsed.frame.payload, report)) {
                m_singlePcbaTimingReport = report;
                m_singlePcbaStartPending = report.running;
                m_singlePcbaTimingRunning = report.running && !report.done;
                if (report.done || !report.running) {
                    m_singlePcbaTimingPollTimer.stop();
                }
                updateSinglePcbaTimingTable();
            } else {
                appendLog("PCBA_TIMING_REPORT 长度或格式不对");
            }
        }
        if (parsed.frame.command == usb::GetSingleTankPcba) {
            SingleTankPcbaReport report;
            if (usb::parseSingleTankPcbaReport(parsed.frame.payload, report)) {
                m_singleTankPcbaReport = report;
                m_singleTankPcbaStartPending = report.running;
                m_singleTankPcbaRunning = report.running && !report.done;
                if (report.done || !report.running) {
                    m_singleTankPcbaPollTimer.stop();
                }
                updateSingleTankPcbaTable();
            } else {
                appendLog("SINGLE_TANK_PCBA_REPORT 长度或格式不对");
            }
        }
    }
}

void MainWindow::handleSerialError(const QString &message)
{
    appendLog("连接错误: " + message);
}

void MainWindow::applySnapshot(const FixtureSnapshot &snapshot)
{
    const FixtureSnapshot previous = m_snapshot;
    m_snapshot = snapshot;
    if (m_transport.isOpen()) {
        m_snapshot.linkMode = LinkMode::UsbCdc;
    }
    for (int sensorIndex = 0; sensorIndex < kPressureSensorCount; ++sensorIndex) {
        const bool previousFault = pressureSensorFaultLatched(previous, sensorIndex);
        const bool currentFault = pressureSensorFaultLatched(m_snapshot, sensorIndex);
        const QString previousReason = pressureSensorFaultReasonText(previous, sensorIndex);
        const QString currentReason = pressureSensorFaultReasonText(m_snapshot, sensorIndex);
        if ((!previousFault && currentFault) ||
            (currentFault && (previousReason != currentReason ||
                              previous.pressureStatusByte[sensorIndex] != m_snapshot.pressureStatusByte[sensorIndex] ||
                              previous.pressureFaultCode[sensorIndex] != m_snapshot.pressureFaultCode[sensorIndex]))) {
            appendLog(QString("压力检测%1 故障: %2")
                          .arg(sensorIndex + 1)
                          .arg(currentReason.isEmpty() ? QStringLiteral("故障锁定") : currentReason));
        }
    }
    m_architectureView->setSnapshot(m_snapshot);
    bool anyPressureFault = false;
    for (bool faultLatched : m_snapshot.pressureFaultLatched) {
        if (faultLatched) {
            anyPressureFault = true;
            break;
        }
    }
    const bool singlePcbaDiagnosticActive = m_singlePcbaTimingRunning || m_singlePcbaStartPending;
    const bool singleTankPcbaDiagnosticActive = m_singleTankPcbaRunning || m_singleTankPcbaStartPending;
    QString stateText = stateDisplayName(m_snapshot.state);
    if (singlePcbaDiagnosticActive) {
        stateText = pcbaDiagnosticStateText(m_snapshot.state,
                                            RuntimeState::PcbaTimingDiagnostic,
                                            QStringLiteral("单PCBA指令诊断"),
                                            true);
    } else if (singleTankPcbaDiagnosticActive) {
        stateText = pcbaDiagnosticStateText(m_snapshot.state,
                                            RuntimeState::SingleTankPcbaDiagnostic,
                                            QStringLiteral("单罐单PCBA测试"),
                                            true);
    }
    m_stateLabel->setText(anyPressureFault ? stateText + " | 传感器故障锁定" : stateText);
    m_linkLabel->setText(QString("%1 | seq %2 | elapsed %3ms")
                             .arg(m_snapshot.linkMode == LinkMode::UsbCdc ? "USB CDC 联机" : "未连接")
                             .arg(m_snapshot.sequence)
                             .arg(m_snapshot.elapsedMs));
    updateFlowList();
    updateTables();
    updateCalibrationDialog();
}

void MainWindow::sendProductionStart()
{
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    if (!m_transport.isOpen()) {
        appendLog("未连接，生产流程未启动");
        statusBar()->showMessage("未连接 MCU，生产流程未启动", 3000);
        return;
    }

    appendLog("生产流程启动: 清场后从初始化罐体闭环开始");
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Stop), "STOP 生产流程清场");
    sendFrame(usb::buildStart(nextSequence(), 285), "START 生产流程");
}

void MainWindow::sendStart()
{
    if (!isDebugMode()) {
        sendProductionStart();
        return;
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    sendFrame(usb::buildStart(nextSequence(), 285), "START 调试流程");
}

void MainWindow::sendStop()
{
    m_singlePcbaStartPending = false;
    m_singlePcbaTimingRunning = false;
    m_singlePcbaTimingPollTimer.stop();
    m_singleTankPcbaStartPending = false;
    m_singleTankPcbaRunning = false;
    m_singleTankPcbaPollTimer.stop();
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Stop), "STOP");
}

void MainWindow::sendPause()
{
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Pause), "PAUSE");
}

void MainWindow::sendResume()
{
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Resume), "RESUME");
}

void MainWindow::sendSelectedState()
{
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    activateSelectedLeftItem();
}

void MainWindow::enterPcbaCurrentTest()
{
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    sendFrame(usb::buildSetState(nextSequence(), RuntimeState::PcbaCurrentTest),
              "SET_STATE " + stateDisplayName(RuntimeState::PcbaCurrentTest));
    const bool enable50mA = m_pcbaCurrent50mACheck && m_pcbaCurrent50mACheck->isChecked();
    const bool enable5V = !m_pcbaSupplyVoltageCombo || m_pcbaSupplyVoltageCombo->currentData().toInt() != 45;
    sendFrame(usb::buildSetPcbaSupplyVoltage(nextSequence(), enable5V),
              QString("SET_PCBA_SUPPLY_VOLTAGE %1").arg(enable5V ? "5V" : "4.5V"));
    sendFrame(usb::buildSetPcbaCurrentRange(nextSequence(), enable50mA),
              QString("SET_PCBA_CURRENT_RANGE %1").arg(enable50mA ? "50mA" : "uA"));
}

void MainWindow::startSinglePcbaFlow()
{
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    m_singlePcbaTimingReport = PcbaTimingReport{};
    m_singlePcbaStartPending = true;
    m_singlePcbaTimingRunning = true;
    const bool stopOnFail = m_singlePcbaStopOnFailCheck && m_singlePcbaStopOnFailCheck->isChecked();
    if (m_singlePcbaStatusLabel) {
        m_singlePcbaStatusLabel->setText(QString("启动中 | 已下发单PCBA指令诊断，不等待工装压合，等待 MCU 从 1号位 UART1 开始执行。失败即停：%1")
                                             .arg(stopOnFail ? "开启" : "关闭"));
    }
    updateSinglePcbaTimingTable();
    if (sendFrame(usb::buildRunPcbaTiming(nextSequence(), stopOnFail),
                  QString("RUN_PCBA_TIMING 单PCBA指令计时测试 | 失败即停=%1").arg(stopOnFail ? "1" : "0"))) {
        m_singlePcbaTimingPollTimer.start();
    } else {
        m_singlePcbaStartPending = false;
        m_singlePcbaTimingRunning = false;
        updateSinglePcbaTimingTable();
    }
}

void MainWindow::requestSinglePcbaTimingReport()
{
    if (!m_transport.isOpen()) {
        m_singlePcbaTimingPollTimer.stop();
        m_singlePcbaTimingRunning = false;
        return;
    }
    sendFrame(usb::buildGetPcbaTiming(nextSequence()), "GET_PCBA_TIMING 单PCBA指令结果");
}

void MainWindow::startSingleTankPcbaFlow()
{
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    m_singleTankPcbaReport = SingleTankPcbaReport{};
    m_singleTankPcbaStartPending = true;
    m_singleTankPcbaRunning = true;
    if (m_singleTankPcbaStatusLabel) {
        m_singleTankPcbaStatusLabel->setText("启动中 | 已下发单罐单PCBA测试，等待 MCU 从4.5V/10uA档开始执行。");
    }
    updateSingleTankPcbaTable();
    if (sendFrame(usb::buildRunSingleTankPcba(nextSequence()), "RUN_SINGLE_TANK_PCBA 单罐单PCBA测试")) {
        m_singleTankPcbaPollTimer.start();
    } else {
        m_singleTankPcbaStartPending = false;
        m_singleTankPcbaRunning = false;
    }
}

void MainWindow::requestSingleTankPcbaReport()
{
    if (!m_transport.isOpen()) {
        m_singleTankPcbaPollTimer.stop();
        m_singleTankPcbaRunning = false;
        return;
    }
    sendFrame(usb::buildGetSingleTankPcba(nextSequence()), "GET_SINGLE_TANK_PCBA 单罐单PCBA结果");
}

void MainWindow::handlePcbaSupplyVoltageChanged(int index)
{
    if (index < 0 || !m_pcbaSupplyVoltageCombo) {
        return;
    }
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    if (m_snapshot.state != RuntimeState::PcbaCurrentTest) {
        sendFrame(usb::buildSetState(nextSequence(), RuntimeState::PcbaCurrentTest),
                  "SET_STATE " + stateDisplayName(RuntimeState::PcbaCurrentTest));
    }
    const bool enable5V = m_pcbaSupplyVoltageCombo->itemData(index).toInt() != 45;
    sendFrame(usb::buildSetPcbaSupplyVoltage(nextSequence(), enable5V),
              QString("SET_PCBA_SUPPLY_VOLTAGE %1").arg(enable5V ? "5V" : "4.5V"));
}

void MainWindow::setPcbaCurrent50mAEnabled(bool enabled)
{
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    if (m_snapshot.state != RuntimeState::PcbaCurrentTest) {
        sendFrame(usb::buildSetState(nextSequence(), RuntimeState::PcbaCurrentTest),
                  "SET_STATE " + stateDisplayName(RuntimeState::PcbaCurrentTest));
    }
    sendFrame(usb::buildSetPcbaCurrentRange(nextSequence(), enabled),
              QString("SET_PCBA_CURRENT_RANGE %1").arg(enabled ? "50mA" : "uA"));
}

void MainWindow::sendAdcCalibration()
{
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    sendFrame(usb::buildCalibrateAdc(nextSequence()), "CALIBRATE_ADC");
    statusBar()->showMessage("已要求MCU刷新内部基准，等待状态回传", 3000);
}

void MainWindow::setRtcEditorToComputerTime()
{
    if (m_rtcDateTimeEdit) {
        m_rtcDateTimeEdit->setDateTime(QDateTime::currentDateTime());
    }
}

void MainWindow::sendRtcTime()
{
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    if (!m_rtcDateTimeEdit) {
        return;
    }

    const uint32_t epochSeconds = static_cast<uint32_t>(m_rtcDateTimeEdit->dateTime().toSecsSinceEpoch());
    sendFrame(usb::buildSetRtcTime(nextSequence(), epochSeconds),
              QString("SET_RTC_TIME %1").arg(m_rtcDateTimeEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss")));
}

void MainWindow::browseFirmwareHex()
{
    const QString startPath = m_firmwareHexEdit && !m_firmwareHexEdit->text().trimmed().isEmpty()
        ? QFileInfo(m_firmwareHexEdit->text().trimmed()).absolutePath()
        : QDir::currentPath();
    const QString file = QFileDialog::getOpenFileName(this,
                                                      "选择烧录HEX文件",
                                                      startPath,
                                                      "Firmware HEX (*.hex);;All files (*.*)");
    if (!file.isEmpty() && m_firmwareHexEdit) {
        m_firmwareHexEdit->setText(QDir::toNativeSeparators(file));
    }
}

void MainWindow::startFirmwareDownload()
{
    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    if (m_firmwareDownloadRunning) {
        statusBar()->showMessage("固件正在烧录中", 3000);
        return;
    }

    const QString hexPath = m_firmwareHexEdit ? m_firmwareHexEdit->text().trimmed() : QString();
    const QString jlinkPath = m_jlinkPathEdit ? m_jlinkPathEdit->text().trimmed() : QString();
    if (hexPath.isEmpty() || !QFileInfo::exists(hexPath)) {
        QMessageBox::warning(this, "烧录文件不存在", "请选择有效的 HEX 烧录文件。");
        return;
    }
    if (jlinkPath.isEmpty() || !QFileInfo::exists(jlinkPath)) {
        QMessageBox::warning(this, "J-Link不存在", "请确认 J-Link.exe 路径。");
        return;
    }

    const QString message = QString("将使用 J-Link 以 SWD 100kHz 下载到 STM32F103ZE。\n\n文件:\n%1\n\n继续会擦除并重新写入目标板内部 Flash。")
                                .arg(QDir::toNativeSeparators(hexPath));
    if (QMessageBox::question(this, "确认烧录固件", message) != QMessageBox::Yes) {
        return;
    }

    const QString commandPath = QDir::temp().absoluteFilePath(
        QString("pressure-fixture-download-%1.jlink").arg(QDateTime::currentMSecsSinceEpoch()));
    QFile commandFile(commandPath);
    if (!commandFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "创建脚本失败", "无法创建临时 J-Link 命令文件。");
        return;
    }
    const QString escapedHex = QDir::fromNativeSeparators(hexPath);
    commandFile.write("r\n");
    commandFile.write("h\n");
    commandFile.write("erase\n");
    commandFile.write(QString("loadfile \"%1\"\n").arg(escapedHex).toLocal8Bit());
    commandFile.write("r\n");
    commandFile.write("g\n");
    commandFile.write("exit\n");
    commandFile.close();

    m_firmwareCommandFile = commandPath;
    m_firmwareDownloadRunning = true;
    if (m_firmwareDownloadButton) {
        m_firmwareDownloadButton->setEnabled(false);
    }
    if (m_firmwareStatusLabel) {
        m_firmwareStatusLabel->setText("正在烧录: SWD 100kHz");
    }
    m_firmwareDownloadSawError = false;
    if (m_firmwareLog) {
        m_firmwareLog->clear();
    }
    appendFirmwareLog("开始烧录: " + QDir::toNativeSeparators(hexPath));
    appendFirmwareLog("J-Link: " + QDir::toNativeSeparators(jlinkPath));
    appendLog("开始固件烧录: " + QDir::toNativeSeparators(hexPath));

    const QStringList args{
        "-Device", "STM32F103ZE",
        "-If", "SWD",
        "-Speed", "100",
        "-NoGui", "1",
        "-CommandFile", commandPath
    };
    m_firmwareProcess.start(jlinkPath, args);
}

void MainWindow::handleFirmwareDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool ok = exitStatus == QProcess::NormalExit && exitCode == 0 && !m_firmwareDownloadSawError;
    appendFirmwareLog(ok ? "烧录完成" : QString("烧录失败，退出码 %1").arg(exitCode));
    appendLog(ok ? "固件烧录完成" : QString("固件烧录失败，退出码 %1").arg(exitCode));
    if (m_firmwareStatusLabel) {
        m_firmwareStatusLabel->setText(ok ? "烧录完成" : QString("烧录失败，退出码 %1").arg(exitCode));
    }
    statusBar()->showMessage(ok ? "固件烧录完成" : "固件烧录失败，请查看J-Link日志", 5000);
    if (m_firmwareDownloadButton) {
        m_firmwareDownloadButton->setEnabled(true);
    }
    if (!m_firmwareCommandFile.isEmpty()) {
        QFile::remove(m_firmwareCommandFile);
        m_firmwareCommandFile.clear();
    }
    m_firmwareDownloadRunning = false;
}

void MainWindow::sendThreshold()
{
    if (!isDebugMode()) {
        appendLog("生产模式下未下发调试阈值");
        statusBar()->showMessage("请切换到调试模式后再下发调试阈值", 3000);
        return;
    }
    const double threshold = m_thresholdSpin->value();
    sendFrame(usb::buildSetThreshold(nextSequence(), threshold), QString("SET_THRESHOLD %1mmHg").arg(threshold));
}

void MainWindow::sendManualValve()
{
    if (!isDebugMode()) {
        appendLog("生产模式禁止手动阀控制");
        statusBar()->showMessage("请切换到调试模式后再手动控制阀门", 3000);
        return;
    }
    if (m_singleTankRunning) {
        statusBar()->showMessage("单罐闭环运行中，请先停止再手动控制阀门", 3000);
        appendLog("单罐闭环运行中，手动阀命令被忽略");
        return;
    }
    const int valve = m_valveSpin->value();
    const bool open = m_valveActionCombo->currentIndex() == 1;
    const QString description = QString("MANUAL_VALVE 阀%1 %2").arg(valve).arg(open ? "打开" : "关闭");
    sendManualValveCommand(valve, open, description);
}

void MainWindow::toggleValveFromDiagram(int valveNumber)
{
    if (valveNumber < 1 || valveNumber > kValveCount) {
        return;
    }
    if (!isDebugMode()) {
        appendLog("生产模式禁止从架构图手动切阀");
        statusBar()->showMessage("请切换到调试模式后再手动控制阀门", 3000);
        return;
    }
    if (m_singleTankRunning) {
        statusBar()->showMessage("单罐闭环运行中，请先停止再手动控制阀门", 3000);
        appendLog("单罐闭环运行中，架构图手动阀命令被忽略");
        return;
    }

    const bool open = !m_snapshot.valvesOpen[valveNumber];
    m_valveSpin->setValue(valveNumber);
    m_valveActionCombo->setCurrentIndex(open ? 1 : 0);
    const QString description = QString("MANUAL_VALVE 阀%1 %2").arg(valveNumber).arg(open ? "打开" : "关闭");
    sendManualValveCommand(valveNumber, open, description);
}

void MainWindow::openSensorCalibration(int sensorNumber)
{
    if (sensorNumber < 1 || sensorNumber > kPressureSensorCount) {
        return;
    }
    if (!isDebugMode()) {
        appendLog("生产模式未打开传感器标定");
        statusBar()->showMessage("请切换到调试模式后再标定传感器", 3000);
        return;
    }

    if (m_calibrationDialog && m_calibrationDialog->sensorNumber() != sensorNumber) {
        m_calibrationDialog->close();
        m_calibrationDialog = nullptr;
    }
    if (!m_calibrationDialog) {
        m_calibrationDialog = new SensorCalibrationDialog(sensorNumber, this);
        connect(m_calibrationDialog, &QObject::destroyed, this, [this]() {
            m_calibrationDialog = nullptr;
        });
    }

    m_calibrationDialog->setSnapshot(m_snapshot);
    m_calibrationDialog->show();
    m_calibrationDialog->raise();
    m_calibrationDialog->activateWindow();
    appendLog(QString("打开压力检测%1标定界面").arg(sensorNumber));
}

void MainWindow::sendEnterMsc()
{
    if (!isDebugMode()) {
        appendLog("生产模式禁止进入 U 盘维护模式");
        statusBar()->showMessage("请切换到调试模式后再进入 U 盘维护模式", 3000);
        return;
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    const auto answer = QMessageBox::question(this, "重启到 U 盘维护模式",
                                              "该命令会要求 MCU 关闭输出并重启到 USB MSC U 盘维护模式。继续？");
    if (answer != QMessageBox::Yes) {
        return;
    }
    sendFrame(usb::buildEnterMscReboot(nextSequence()), "ENTER_MSC_REBOOT");
}

bool MainWindow::sendManualValveCommand(int valveNumber, bool open, const QString &description)
{
    if (valveNumber < 1 || valveNumber > kValveCount) {
        return false;
    }

    if (sendFrame(usb::buildManualValve(nextSequence(), static_cast<uint8_t>(valveNumber), open), description)) {
        m_architectureView->setPendingValveCommand(valveNumber, open);
        statusBar()->showMessage(QString("已下发阀%1%2，等待 MCU 快照确认")
                                     .arg(valveNumber)
                                     .arg(open ? "打开" : "关闭"),
                                 3000);
        return true;
    }
    return false;
}

bool MainWindow::sendValveMaskCommand(uint32_t valveMask, uint32_t openMask, const QString &description)
{
    if (valveMask == 0u) {
        return false;
    }
    if (sendFrame(usb::buildSetValveMask(nextSequence(), valveMask, openMask), description)) {
        for (int valve = 1; valve <= kValveCount; ++valve) {
            const uint32_t bit = 1u << (valve - 1);
            if ((valveMask & bit) != 0u) {
                m_architectureView->setPendingValveCommand(valve, (openMask & bit) != 0u);
            }
        }
        statusBar()->showMessage(QString("已下发批量阀命令，等待 MCU 快照确认"), 3000);
        return true;
    }
    return false;
}

bool MainWindow::sendFrame(const QByteArray &frame, const QString &description)
{
    if (m_transport.isOpen()) {
        if (m_transport.writeBytes(frame)) {
            appendLog("TX " + description);
            return true;
        }
        appendLog("TX 失败: " + description);
        statusBar()->showMessage("发送失败: " + description, 3000);
        return false;
    }
    appendLog("未连接，未发送: " + description);
    statusBar()->showMessage("未连接 MCU，命令未发送: " + description, 3000);
    return false;
}

uint16_t MainWindow::nextSequence()
{
    return m_sequence++;
}

void MainWindow::appendLog(const QString &line)
{
    if (!m_log) {
        return;
    }
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + line);
}

QString MainWindow::defaultFirmwareHexPath() const
{
    const QString relativePath = "Firmware/STM32F103ZET6_MDK_HAL/MDK-ARM/Objects/PressureFixture_STM32F103ZET6.hex";
    const QStringList baseDirs{
        QDir::currentPath(),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../../"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../")
    };
    for (const QString &baseDir : baseDirs) {
        const QString candidate = QDir(baseDir).absoluteFilePath(relativePath);
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(QFileInfo(candidate).absoluteFilePath());
        }
    }
    return QDir::toNativeSeparators(QDir(baseDirs.first()).absoluteFilePath(relativePath));
}

QString MainWindow::defaultJLinkPath() const
{
    const QStringList candidates{
        "C:/Program Files/SEGGER/JLink/JLink.exe",
        "C:/Program Files/SEGGER/JLink_V926/JLink.exe",
        "C:/Program Files/SEGGER/JLink_V794e/JLink.exe"
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }
    return "JLink.exe";
}

void MainWindow::appendFirmwareLog(const QString &line)
{
    if (!m_firmwareLog) {
        return;
    }
    const QString normalized = line.trimmed();
    if (!normalized.isEmpty()) {
        const QString lower = normalized.toLower();
        if (lower.contains("failed") ||
            lower.contains("error") ||
            lower.contains("cannot") ||
            lower.contains("can not") ||
            lower.contains("could not")) {
            m_firmwareDownloadSawError = true;
        }
        m_firmwareLog->appendPlainText(normalized);
    }
}

void MainWindow::updateTables()
{
    const bool pcbaCurrent50mA = m_snapshot.pcbaCurrent50mAEnabled;
    if (m_pcbaCurrent50mACheck) {
        const QSignalBlocker blocker(m_pcbaCurrent50mACheck);
        m_pcbaCurrent50mACheck->setChecked(pcbaCurrent50mA);
    }
    if (m_pcbaSupplyVoltageCombo) {
        const QSignalBlocker blocker(m_pcbaSupplyVoltageCombo);
        const int targetVoltage = m_snapshot.pcbaSupply45VEnabled && !m_snapshot.pcbaSupply5VEnabled ? 45 : 50;
        const int voltageIndex = m_pcbaSupplyVoltageCombo->findData(targetVoltage);
        if (voltageIndex >= 0) {
            m_pcbaSupplyVoltageCombo->setCurrentIndex(voltageIndex);
        }
    }
    if (m_pcbaCurrentTable) {
        m_pcbaCurrentTable->setHorizontalHeaderLabels({
            "通道",
            pcbaCurrent50mA ? "电流 mA(已矫正)" : "电流 uA(已矫正)",
            "ADC原始码(未矫正)",
            "内部基准矫正系数"
        });
    }

    for (int valve = 1; valve <= kValveCount; ++valve) {
        m_valveTable->setItem(valve - 1, 0, new QTableWidgetItem(QString("阀%1").arg(valve)));
        m_valveTable->setItem(valve - 1, 1, new QTableWidgetItem(m_snapshot.valvesOpen[valve] ? "打开" : "关闭"));
    }
    for (int sensor = 1; sensor <= kPressureSensorCount; ++sensor) {
        auto *nameItem = new QTableWidgetItem(QString("压力检测%1").arg(sensor));
        auto *valueItem = new QTableWidgetItem(sensorPressureText(m_snapshot, sensor - 1));
        if (pressureSensorFaultLatched(m_snapshot, sensor - 1)) {
            nameItem->setForeground(QBrush(QColor("#b91c1c")));
            valueItem->setForeground(QBrush(QColor("#b91c1c")));
            valueItem->setBackground(QBrush(QColor("#fee2e2")));
            valueItem->setToolTip(sensorFaultUiText(m_snapshot, sensor - 1));
        } else if (!pressureSensorValid(m_snapshot, sensor - 1)) {
            valueItem->setForeground(QBrush(QColor("#b45309")));
            valueItem->setToolTip(sensorFaultUiText(m_snapshot, sensor - 1));
        }
        m_pressureTable->setItem(sensor - 1, 0, nameItem);
        m_pressureTable->setItem(sensor - 1, 1, valueItem);
    }
    for (int i = 0; i < kChannelCount; ++i) {
        const auto &channel = m_snapshot.channels[i];
        const bool fixturePressureValid = pressureSensorValid(m_snapshot, 6 + i);
        const bool pcbaPressureValid = channel.online && channel.pressure001mmHg > 0;
        m_pcbaTable->setItem(i, 0, new QTableWidgetItem(QString("通道%1").arg(i + 1)));
        m_pcbaTable->setItem(i, 1, new QTableWidgetItem(channel.online ? "在线" : "离线"));
        auto *fixtureItem = new QTableWidgetItem(sensorPressureText(m_snapshot, 6 + i));
        if (pressureSensorFaultLatched(m_snapshot, 6 + i)) {
            fixtureItem->setForeground(QBrush(QColor("#b91c1c")));
            fixtureItem->setBackground(QBrush(QColor("#fee2e2")));
            fixtureItem->setToolTip(sensorFaultUiText(m_snapshot, 6 + i));
        } else if (!fixturePressureValid) {
            fixtureItem->setForeground(QBrush(QColor("#b45309")));
            fixtureItem->setToolTip(sensorFaultUiText(m_snapshot, 6 + i));
        }
        m_pcbaTable->setItem(i, 2, fixtureItem);
        m_pcbaTable->setItem(i, 3, new QTableWidgetItem(formatPressure001mmHg(channel.pressure001mmHg, pcbaPressureValid)));
        m_pcbaTable->setItem(i, 4, new QTableWidgetItem(pcbaPressureValid ? (channel.pass ? "合格" : "不合格") : "--"));
        if (m_pcbaCurrentTable) {
            const QString realtimeCurrent = pcbaCurrent50mA
                ? formatCurrentUaX100AsMa(channel.workCurrentUaX100, channel.workCurrentValid)
                : formatCurrentUaX100(channel.standbyCurrentUaX100, channel.standbyCurrentValid);
            m_pcbaCurrentTable->setItem(i, 0, new QTableWidgetItem(QString("通道%1").arg(i + 1)));
            m_pcbaCurrentTable->setItem(i, 1, new QTableWidgetItem(realtimeCurrent));
            m_pcbaCurrentTable->setItem(i, 2, new QTableWidgetItem(channel.currentRawAdcValid
                                                                   ? QString::number(channel.currentRawAdc)
                                                                   : "--"));
            m_pcbaCurrentTable->setItem(i, 3, new QTableWidgetItem(m_snapshot.adcReferenceValid &&
                                                                   !m_snapshot.adcReferenceRangeError
                                                                   ? QString("%1").arg(m_snapshot.adcScalePpm / 1000000.0,
                                                                                       0,
                                                                                       'f',
                                                                                       6)
                                                                   : "--"));
        }
    }

    if (m_pcbaCurrentStatusLabel) {
        int validRealtimeCount = 0;
        for (const auto &channel : m_snapshot.channels) {
            if (pcbaCurrent50mA ? channel.workCurrentValid : channel.standbyCurrentValid) {
                ++validRealtimeCount;
            }
        }
        m_pcbaCurrentStatusLabel->setText(QString("%1 | %2 | 电流已按内部基准矫正，ADC原始码未矫正 | 电流有效通道 %3/%4")
                                              .arg(m_snapshot.state == RuntimeState::PcbaCurrentTest
                                                       ? "已进入PCBA电流测试"
                                                       : "未进入PCBA电流测试")
                                              .arg(QString("%1 | %2")
                                                       .arg(pcbaSupplyVoltageText(m_snapshot))
                                                       .arg(pcbaCurrent50mA
                                                                ? "mA模式，PB1共享低阻支路已打开"
                                                                : "uA模式，PB1共享低阻支路已关闭"))
                                              .arg(validRealtimeCount)
                                              .arg(kChannelCount));
    }

    if (m_singlePcbaStatusLabel) {
        if (m_singlePcbaTimingReport.done) {
            m_singlePcbaStatusLabel->setText(
                QString("已完成 | %1 | 结果: %2")
                    .arg(pcbaDiagnosticStateText(m_snapshot.state,
                                                 RuntimeState::PcbaTimingDiagnostic,
                                                 QStringLiteral("单PCBA指令诊断"),
                                                 false))
                    .arg(m_singlePcbaTimingReport.finalPass ? "PASS" : "FAIL"));
        } else if (m_singlePcbaTimingRunning || m_singlePcbaStartPending) {
            m_singlePcbaStatusLabel->setText(
                QString("运行中 | %1 | %2")
                    .arg(pcbaDiagnosticStateText(m_snapshot.state,
                                                 RuntimeState::PcbaTimingDiagnostic,
                                                 QStringLiteral("单PCBA指令诊断"),
                                                 true))
                    .arg(singlePcbaProgressText(m_singlePcbaTimingReport)));
        } else {
            m_singlePcbaStatusLabel->setText("未启动 | 点击“开始单PCBA指令测试”后，MCU 将直接对 1号位 UART1 执行完整串口指令计时，不等待工装压合。");
        }
    }
    updateSinglePcbaTimingTable();

    if (m_singleTankPcbaStatusLabel) {
        if (m_singleTankPcbaReport.done) {
            m_singleTankPcbaStatusLabel->setText(
                QString("已完成 | %1 | 结果: %2")
                    .arg(pcbaDiagnosticStateText(m_snapshot.state,
                                                 RuntimeState::SingleTankPcbaDiagnostic,
                                                 QStringLiteral("单罐单PCBA测试"),
                                                 false))
                    .arg(m_singleTankPcbaReport.finalPass ? "PASS" : "FAIL"));
        } else if (m_singleTankPcbaRunning || m_singleTankPcbaStartPending) {
            m_singleTankPcbaStatusLabel->setText(
                QString("运行中 | %1 | %2")
                    .arg(pcbaDiagnosticStateText(m_snapshot.state,
                                                 RuntimeState::SingleTankPcbaDiagnostic,
                                                 QStringLiteral("单罐单PCBA测试"),
                                                 true))
                    .arg(singleTankPcbaProgressText(m_singleTankPcbaReport)));
        } else {
            m_singleTankPcbaStatusLabel->setText("未启动 | 点击“开始单罐单PCBA测试”后，MCU 将在 1号位执行完整供电、电流和串口计时流程。");
        }
    }
    updateSingleTankPcbaTable();

    if (m_adcReferenceStatusLabel && m_adcReferenceVddaLabel &&
        m_adcReferenceRawLabel && m_adcReferenceScaleLabel) {
        m_adcReferenceStatusLabel->setText(m_snapshot.adcReferenceRangeError
            ? "基准异常"
            : (m_snapshot.adcReferenceValid ? "实时基准有效" : "等待内部基准"));
        m_adcReferenceVddaLabel->setText(QString("%1 mV").arg(m_snapshot.adcVddaMv));
        m_adcReferenceRawLabel->setText(m_snapshot.adcVrefintRaw == 0
            ? "--"
            : QString::number(m_snapshot.adcVrefintRaw));
        m_adcReferenceScaleLabel->setText(QString("%1")
                                                .arg(m_snapshot.adcScalePpm / 1000000.0, 0, 'f', 6));
    }

    if (m_rtcTimeLabel && m_rtcBatteryLabel && m_rtcOscillatorLabel) {
        if (m_snapshot.rtcSnapshotValid) {
            const QDateTime rtcTime = QDateTime::fromSecsSinceEpoch(m_snapshot.rtcEpochSeconds);
            m_rtcTimeLabel->setText(rtcTime.toString("yyyy-MM-dd HH:mm:ss"));
            m_rtcBatteryLabel->setText(m_snapshot.rtcBackupValid ? "有电" : "无效或未检测到保持数据");
            m_rtcOscillatorLabel->setText(m_snapshot.rtcOscillatorReady ? "正常" : "未就绪");
        } else {
            m_rtcTimeLabel->setText("--");
            m_rtcBatteryLabel->setText("--");
            m_rtcOscillatorLabel->setText("--");
        }
    }
    refreshStatusTablesVisibility();
}

void MainWindow::updateSinglePcbaTimingTable()
{
    if (!m_singlePcbaCommandTable) {
        return;
    }

    int passCount = 0;
    int failCount = 0;
    const auto &report = m_singlePcbaTimingReport;
    const bool showCurrentRow =
        !report.done && (report.running || m_singlePcbaTimingRunning || m_singlePcbaStartPending);
    Q_UNUSED(showCurrentRow)
    m_singlePcbaCommandTable->setRowCount(kPcbaTimingStepCount);
    for (int row = 0; row < kPcbaTimingStepCount; ++row) {
        m_singlePcbaCommandTable->setRowHidden(row, false);
        const bool hasEntry = row < report.count;
        const PcbaTimingEntry entry = hasEntry ? report.entries[static_cast<size_t>(row)] : PcbaTimingEntry{};
        const bool pass = hasEntry && entry.ok && entry.elapsedUs <= kPcbaTimingLimitUs;
        const bool fail = hasEntry && (!entry.ok || entry.elapsedUs > kPcbaTimingLimitUs);
        if (pass) {
            ++passCount;
        } else if (fail) {
            ++failCount;
        }

        const QString elapsedText = hasEntry && entry.elapsedUs > 0
            ? QString("%1 ms").arg(entry.elapsedUs / 1000.0, 0, 'f', 3)
            : "--";
        const bool stoppedEarly = report.done && report.count < kPcbaTimingStepCount && row >= report.count;
        const QString verdict = !hasEntry ? (stoppedEarly ? "停止" : "等待") : (pass ? "√" : "不通过");
        const QString reason = hasEntry
            ? singlePcbaReasonText(entry)
            : (stoppedEarly ? "已在前序失败后停止，后续步骤未执行"
                            : "正在等待 PCBA 回包或 MCU 超时结果");
        const QString rxText = hasEntry ? singlePcbaRxText(entry) : "--";

        const QStringList values{
            singlePcbaStepText(row),
            singlePcbaTxText(row),
            rxText,
            elapsedText,
            verdict,
            reason,
        };
        for (int col = 0; col < values.size(); ++col) {
            auto *item = m_singlePcbaCommandTable->item(row, col);
            if (!item) {
                item = new QTableWidgetItem();
                m_singlePcbaCommandTable->setItem(row, col, item);
            }
            item->setText(values[col]);
            item->setToolTip(values[col]);
            if (pass) {
                item->setBackground(QColor("#dcfce7"));
                item->setForeground(QColor("#166534"));
            } else if (fail) {
                item->setBackground(QColor("#fee2e2"));
                item->setForeground(QColor("#991b1b"));
            } else {
                item->setBackground(QColor("#ffffff"));
                item->setForeground(QColor("#334155"));
            }
        }
    }

    if (m_singlePcbaSummaryLabel) {
        const QString stateText = report.done
            ? QString("完成，%1").arg(report.finalPass
                                         ? "全部通过"
                                         : (report.count < kPcbaTimingStepCount
                                                ? QString("遇到失败已停止于第%1步").arg(report.count)
                                                : "存在失败项"))
            : (report.running || m_singlePcbaTimingRunning ? "测试运行中" : "等待启动");
        m_singlePcbaSummaryLabel->setText(
            QString("%1 | 已返回 %2/%3 条 | 通过 %4 条 | 不通过 %5 条 | 10ms规范")
                .arg(stateText)
                .arg(report.count)
                .arg(kPcbaTimingStepCount)
                .arg(passCount)
                .arg(failCount));
    }
    if (m_singlePcbaSerialLog) {
        const QString detail = pcbaTimingSerialLogText(report);
        if (m_singlePcbaSerialLog->toPlainText() != detail) {
            m_singlePcbaSerialLog->setPlainText(detail);
        }
    }
    if (m_singlePcbaStopOnFailCheck) {
        const QSignalBlocker blocker(m_singlePcbaStopOnFailCheck);
        m_singlePcbaStopOnFailCheck->setEnabled(!report.running && !m_singlePcbaTimingRunning && !m_singlePcbaStartPending);
    }
}

void MainWindow::updateSingleTankPcbaTable()
{
    if (!m_singleTankPcbaTable) {
        return;
    }

    int passCount = 0;
    int failCount = 0;
    uint32_t maxElapsedUs = 0;
    const auto &report = m_singleTankPcbaReport;
    const bool showAllRows =
        report.done || report.count > 0 || report.running || m_singleTankPcbaRunning || m_singleTankPcbaStartPending;
    const int visibleRows = showAllRows ? kSingleTankPcbaStepCount : 0;
    m_singleTankPcbaTable->setRowCount(visibleRows);
    for (int row = 0; row < visibleRows; ++row) {
        const bool hasEntry = row < report.count;
        const SingleTankPcbaEntry entry = hasEntry ? report.entries[static_cast<size_t>(row)] : SingleTankPcbaEntry{};
        const bool pass = hasEntry && entry.ok;
        const bool fail = hasEntry && !entry.ok;
        if (pass) {
            ++passCount;
        } else if (fail) {
            ++failCount;
        }
        if (hasEntry && entry.kind != 2 && entry.elapsedUs > maxElapsedUs) {
            maxElapsedUs = entry.elapsedUs;
        }

        const QString elapsedText = hasEntry && entry.kind != 2 && entry.elapsedUs > 0
            ? QString("%1 ms").arg(entry.elapsedUs / 1000.0, 0, 'f', 3)
            : "--";
        const QString currentText = hasEntry && ((entry.flags & kSingleTankPcbaFlagCurrent) != 0)
            ? formatCurrentUaX100(entry.currentUaX100, entry.ok, 2, true)
            : "--";
        const QString verdict = !hasEntry ? "等待" : (pass ? "√" : "不通过");
        const uint8_t displayFlags = hasEntry ? entry.flags : expectedSingleTankPcbaFlags(row);
        const QStringList values{
            singleTankPcbaStepText(row),
            singleTankPcbaActionText(row, displayFlags),
            singleTankPcbaTxText(row),
            hasEntry ? singleTankPcbaRxText(entry) : "--",
            hasEntry ? singleTankPcbaParsedText(row, entry) : ((row == 0 || row == 2) ? "正在等待电流采样结果" : "正在等待 PCBA 回包或 MCU 超时结果"),
            elapsedText,
            currentText,
            hasEntry ? singleTankPcbaReasonText(entry) : "等待",
        };

        for (int col = 0; col < values.size(); ++col) {
            auto *item = m_singleTankPcbaTable->item(row, col);
            if (!item) {
                item = new QTableWidgetItem();
                m_singleTankPcbaTable->setItem(row, col, item);
            }
            item->setText(values[col]);
            item->setToolTip(values[col]);
            if (pass) {
                item->setBackground(QColor("#dcfce7"));
                item->setForeground(QColor("#166534"));
            } else if (fail) {
                item->setBackground(QColor("#fee2e2"));
                item->setForeground(QColor("#991b1b"));
            } else {
                item->setBackground(QColor("#ffffff"));
                item->setForeground(QColor("#334155"));
            }
        }
    }

    if (m_singleTankPcbaSummaryLabel) {
        const QString stateText = report.done
            ? QString("完成，%1").arg(report.finalPass ? "全部通过" : "存在失败项")
            : (report.running || m_singleTankPcbaRunning ? "测试运行中" : "等待启动");
        m_singleTankPcbaSummaryLabel->setText(
            QString("%1 | 已返回 %2/%3 条 | 通过 %4 条 | 不通过 %5 条 | 待机电流 %6 | 运行电流 %7 | 最大串口耗时 %8 ms")
                .arg(stateText)
                .arg(report.count)
                .arg(kSingleTankPcbaStepCount)
                .arg(passCount)
                .arg(failCount)
                .arg(formatCurrentUaX100(report.standbyCurrentUaX100, report.count > 0, 2, true))
                .arg(formatCurrentUaX100(report.workCurrentUaX100, report.count > 2, 2, true))
                .arg(maxElapsedUs / 1000.0, 0, 'f', 3));
    }
    if (m_singleTankPcbaSerialLog) {
        const QString detail = singleTankPcbaSerialLogText(report);
        if (m_singleTankPcbaSerialLog->toPlainText() != detail) {
            m_singleTankPcbaSerialLog->setPlainText(detail);
        }
    }
}

void MainWindow::refreshStatusTablesVisibility()
{
    bool showPressureStatus = !isDebugMode();
    if (isDebugMode() && m_flowList && m_flowList->currentItem()) {
        const auto kind = static_cast<LeftItemKind>(m_flowList->currentItem()->data(kLeftKindRole).toInt());
        if (kind == LeftItemKind::DebugTool) {
            const auto tool = selectedDebugTool();
            showPressureStatus = tool == DebugTool::SingleTank ||
                                 tool == DebugTool::ManualValve;
        }
    }

    if (m_valveTable) {
        m_valveTable->setVisible(showPressureStatus);
    }
    if (m_pressureTable) {
        m_pressureTable->setVisible(showPressureStatus);
    }
    if (m_pcbaTable) {
        m_pcbaTable->setVisible(showPressureStatus);
    }
}

void MainWindow::updateFlowList()
{
    if (!m_flowList) {
        return;
    }
    for (int i = 0; i < m_flowList->count(); ++i) {
        auto *item = m_flowList->item(i);
        const auto kind = static_cast<LeftItemKind>(item->data(kLeftKindRole).toInt());
        const int value = item->data(kLeftValueRole).toInt();
        RuntimeState state = RuntimeState::Ready;
        QString text;
        bool currentItem = false;
        bool runtimeActive = false;
        bool debugOnly = kind == LeftItemKind::DebugTool;
        if (kind == LeftItemKind::DebugTool) {
            const auto tool = static_cast<DebugTool>(value);
            text = debugToolDisplayName(value);
            currentItem = m_flowList->currentItem() == item;
            runtimeActive = (tool == DebugTool::PcbaCurrent && m_snapshot.state == RuntimeState::PcbaCurrentTest) ||
                            (tool == DebugTool::SinglePcbaFlow &&
                             (m_singlePcbaTimingRunning || m_singlePcbaStartPending ||
                              m_snapshot.state == RuntimeState::PcbaTimingDiagnostic ||
                              m_snapshot.singlePcbaFlowActive)) ||
                            (tool == DebugTool::SingleTankPcba &&
                             (m_singleTankPcbaRunning || m_singleTankPcbaStartPending ||
                              m_snapshot.state == RuntimeState::SingleTankPcbaDiagnostic)) ||
                            (tool == DebugTool::RtcDebug &&
                             m_snapshot.state == RuntimeState::RtcDebug &&
                             !m_singlePcbaTimingRunning &&
                             !m_singlePcbaStartPending &&
                             !m_singleTankPcbaRunning &&
                             !m_singleTankPcbaStartPending);
        } else {
            state = stateFromIndex(value);
            text = stateDisplayName(state);
            currentItem = state == m_snapshot.state;
            debugOnly = isDebugOnlyState(state);
        }
        QFont font = item->font();
        font.setBold(currentItem || runtimeActive);
        item->setFont(font);
        item->setText(QString("%1 %2%3")
                          .arg(currentItem ? ">" : " ")
                          .arg(text)
                          .arg(runtimeActive ? "（运行中）" : ""));
        item->setToolTip(runtimeActive
                             ? "MCU 当前运行在该调试状态；左侧高亮只表示当前打开的工具页。"
                             : QString());
        item->setBackground(currentItem
                                ? (debugOnly ? QColor("#fde68a") : QColor("#dbeafe"))
                                : QColor("#ffffff"));
        item->setForeground(currentItem
                                ? QColor("#0f172a")
                                : (runtimeActive ? QColor("#166534") : QColor("#334155")));
    }
}

void MainWindow::updateCalibrationDialog()
{
    if (m_calibrationDialog) {
        m_calibrationDialog->setSnapshot(m_snapshot);
    }
}
