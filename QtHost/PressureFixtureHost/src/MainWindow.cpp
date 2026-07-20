#include "MainWindow.h"

#include "ArchitectureView.h"
#include "IntelHexValidator.h"

#include <QtCharts/QChart>
#include <QtCharts/QLegend>

#include <QCoreApplication>
#include <QDesktopServices>
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
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <limits>

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
    case RuntimeState::SensorCalibration:
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
constexpr int kSingleTankPcbaStepCount = 23;
constexpr int kPcbaTimingLimitUs = 10000;
constexpr std::array<double, 4> kSensorCalibrationDefaultActualMmHg{0.0, 50.0, 150.0, 250.0};
constexpr char kSensorCalibrationCapturedRawProperty[] = "pressureFixtureCapturedRaw";
constexpr uint8_t kSingleTankPcbaFlag5V = 0x01;
constexpr uint8_t kSingleTankPcbaFlag45V = 0x02;
constexpr uint8_t kSingleTankPcbaFlag50mA = 0x04;
constexpr uint8_t kSingleTankPcbaFlagCurrent = 0x08;
constexpr uint8_t kTankLoopFailureSensorFault = 0x01;
constexpr uint8_t kTankLoopFailureSensorInvalid = 0x02;
constexpr uint8_t kTankLoopFailureOverpressure = 0x03;
constexpr uint8_t kTankLoopFailureNoPressureRise = 0x04;
constexpr uint8_t kTankLoopFailureSettleTimeout = 0x05;
constexpr uint8_t kTankLoopFailureTrendSamples = 0x06;
constexpr uint8_t kTankLoopFailureTrendResidual = 0x07;
constexpr uint8_t kTankLoopFailureTrendDropRate = 0x08;
constexpr uint8_t kTankLoopFailureTrendDirection = 0x09;
constexpr uint8_t kSingleTankProtectionSensorFault = 0x01;
constexpr uint8_t kSingleTankProtectionNoRise = 0x02;

QString sensorCalibrationDetailText(uint8_t detail)
{
    switch (detail) {
    case 1: return QStringLiteral("当前状态不允许此操作");
    case 2: return QStringLiteral("参数无效");
    case 3: return QStringLiteral("传感器数据无效");
    case 4: return QStringLiteral("点动租约已超时");
    case 5: return QStringLiteral("检测到超压");
    case 6: return QStringLiteral("VENT完成");
    case 7: return QStringLiteral("VENT超时");
    case 8: return QStringLiteral("标定存储错误");
    case 9: return QStringLiteral("标定点顺序或间距不合法");
    case 10: return QStringLiteral("零点实测值必须为0");
    case 11: return QStringLiteral("记录完成");
    case 12: return QStringLiteral("写入完成");
    case 13: return QStringLiteral("清除完成");
    case 14: return QStringLiteral("采样尚未稳定，请稍后重试");
    default: return QString();
    }
}

bool sensorCalibrationDetailIsError(uint8_t detail)
{
    switch (detail) {
    case 1:
    case 2:
    case 3:
    case 5:
    case 7:
    case 8:
    case 9:
    case 10:
    case 14:
        return true;
    default:
        return false;
    }
}

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
        {"读版本配置", 0x01, -1, false},
        {"低功耗查询", 0x03, -1, false},
        {"正常功耗查询", 0x04, -1, false},
        {"压力查询1", 0x11, -1, false},
        {"压力查询2", 0x11, -1, false},
        {"压力查询3", 0x11, -1, false},
        {"压力查询4", 0x11, -1, false},
        {"压力查询5", 0x11, -1, false},
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
        {"开机+进测试", 0x00, 0u, 0u, false, false, "切50mA档稳定1s后，同一条开机+进测试命令发送2次，第二次前间隔0.5s，收到任意回包都记录"},
        {"开机后电流", 0x00, 0u, 0u, true, false, "开机+进测试后记录50mA档运行电流"},
        {"读版本配置", 0x01, 0u, 0u, false, false, "期望版本配置回0A 0A"},
        {"查低电", 0x03, 0u, 0u, false, false, "供电切到4.5V后等待1s再查询低电，期望状态数据01"},
        {"记录零点", 0x05, 0u, 0u, false, false, "打开1号罐至PCBA1气路并持续VENT；每次开阀至少30秒，达到0.0±0.1mmHg后继续VENT 30秒，关阀均压并确认后才记录零点"},
        {"趋势采样50mmHg", 0x00, 50000u, 0u, false, true, "第1个标定点；单次加压达到50mmHg即关进气，按观察窗拟合自然压力趋势"},
        {"标定50mmHg", 0x10, 50000u, 4u, false, false, "第1个标定点；压力检测1实际值按0.001mmHg单位直接下发，10ms内成功ACK即通过"},
        {"标后查询50mmHg", 0x11, 0u, 0u, false, false, "标定ACK后立即查询PCBA压力，与查询瞬间的MPRLS1预测压力比较"},
        {"趋势采样150mmHg", 0x00, 150000u, 0u, false, true, "第2个标定点；单次加压达到150mmHg即关进气，按观察窗拟合自然压力趋势，趋势合格后采用预测压力"},
        {"标定150mmHg", 0x10, 150000u, 4u, false, false, "第2个标定点；压力检测1实际值按0.001mmHg单位直接下发，10ms内成功ACK即通过"},
        {"标后查询150mmHg", 0x11, 0u, 0u, false, false, "标定ACK后立即查询PCBA压力，与查询瞬间的MPRLS1预测压力比较"},
        {"趋势采样250mmHg", 0x00, 250000u, 0u, false, true, "第3个标定点；单次加压达到250mmHg即关进气，按观察窗拟合自然压力趋势，趋势合格后采用预测压力"},
        {"标定250mmHg", 0x10, 250000u, 4u, false, false, "第3个标定点；压力检测1实际值按0.001mmHg单位直接下发，10ms内成功ACK即通过"},
        {"标后查询250mmHg", 0x11, 0u, 0u, false, false, "标定ACK后立即查询PCBA压力，与查询瞬间的MPRLS1预测压力比较"},
        {"写入Flash", 0x20, 0u, 0u, false, false, "50/150/250mmHg三点标定完成后立即写Flash，期望ACK YES"},
        {"趋势采样100mmHg", 0x00, 100000u, 0u, false, true, "写Flash后先一次性VENT至零，再单次加压；达到100mmHg关进气，按自然下降趋势同步查询"},
        {"压力查询100mmHg", 0x11, 0u, 0u, false, false, "查询PCBA压力回传，与上一趋势采样采用的预测压力比较"},
        {"趋势采样200mmHg", 0x00, 200000u, 0u, false, true, "单次加压达到200mmHg即关进气，按观察窗拟合自然下降趋势，趋势合格后采用预测压力"},
        {"压力查询200mmHg", 0x11, 0u, 0u, false, false, "查询PCBA压力回传，与上一趋势采样采用的预测压力比较"},
        {"趋势采样285mmHg", 0x00, 285000u, 0u, false, true, "单次加压达到285mmHg即关进气，按观察窗拟合自然下降趋势，趋势合格后采用预测压力"},
        {"压力查询285mmHg", 0x11, 0u, 0u, false, false, "查询PCBA压力回传，与上一趋势采样采用的预测压力比较"},
        {"关机", 0x21, 0u, 0u, false, false, "发送关机后关闭PCBA供电；MCU持续VENT，每次开阀至少30秒，达到0.0±0.1mmHg后继续VENT 30秒，关阀均压确认后结束"},
    }};
    return specs;
}

QString bytesToHexText(const QByteArray &bytes)
{
    return bytes.toHex(' ').toUpper();
}

QString formatTrendPressure001mmHg(uint32_t value, int precision = 2)
{
    return QString("%1 mmHg").arg(value / 1000.0, 0, 'f', precision);
}

QString csvField(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString boolText(bool value)
{
    return value ? QStringLiteral("1") : QStringLiteral("0");
}

QString hexByteText(uint8_t value)
{
    return QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString valveMaskText(const FixtureSnapshot &snapshot)
{
    quint32 mask = 0u;
    for (int valve = 1; valve <= kValveCount; ++valve) {
        if (snapshot.valvesOpen[valve]) {
            mask |= (1u << (valve - 1));
        }
    }
    return QStringLiteral("0x%1").arg(mask, 8, 16, QLatin1Char('0')).toUpper();
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
        return "无串口发送；MCU执行单次加压趋势采样";
    }
    const QString tx = bytesToHexText(buildPcbaFrame(spec.command, 0, spec.payloadValue, spec.payloadLength));
    if (index == 1) {
        return QString("%1；重复发送2次，间隔0.5s").arg(tx);
    }
    return tx;
}

QString singleTankPcbaTxTextForEntry(int index, const SingleTankPcbaEntry &entry)
{
    const auto &specs = singleTankPcbaStepSpecs();
    if (index < 0 || index >= static_cast<int>(specs.size())) {
        return "--";
    }
    const auto &spec = specs[static_cast<size_t>(index)];
    if (entry.kind == 4) {
        return "无串口发送；MCU单次加压到目标后关进气并拟合自然趋势";
    }
    if (entry.kind == 3) {
        return "未发送（前置趋势采样失败）";
    }
    if (entry.kind == 5) {
        return "未发送（VENT未达到零点门槛）";
    }
    if (spec.command == 0x10 && entry.parsedValue > 0u) {
        return bytesToHexText(buildPcbaFrame(spec.command,
                                             0,
                                             entry.parsedValue,
                                             spec.payloadLength));
    }
    return singleTankPcbaTxText(index);
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
    if (entry.kind == 2 || entry.kind == 3 || entry.kind == 4 || entry.kind == 5) {
        return "--";
    }
    if (entry.rawResponseLength > 0) {
        return rawBytesToHexText(entry.rawResponse, entry.rawResponseLength);
    }
    if (!entry.ok) {
        return "超时（10ms未收到PCBA回包）";
    }
    if (entry.kind == 0) {
        return QString("%1").arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return bytesToHexText(buildPcbaFrameFromData(entry.responseCommandOrByte,
                                                 entry.responseChannel,
                                                 entry.responseData,
                                                 entry.responseLength));
}

bool singleTankPcbaComparisonPressure(const SingleTankPcbaEntry &entry,
                                      const SingleTankPcbaEntry *trendEntry,
                                      uint32_t *pressure001mmHg)
{
    if (pressure001mmHg == nullptr) {
        return false;
    }
    if (entry.comparisonPressure001mmHg != 0u) {
        *pressure001mmHg = entry.comparisonPressure001mmHg;
        return true;
    }
    if (trendEntry != nullptr && trendEntry->kind == 4u) {
        *pressure001mmHg = trendEntry->trendPredictedPressure001mmHg;
        return true;
    }
    return false;
}

bool singleTankPcbaReturnedInvalidPressureSentinel(const SingleTankPcbaEntry &entry)
{
    return entry.responseCommandOrByte == 0x11u &&
           entry.responseLength == 4u &&
           entry.responseData[0] == 0xFFu &&
           entry.responseData[1] == 0xFFu &&
           entry.responseData[2] == 0xFFu &&
           entry.responseData[3] == 0xFFu;
}

QString singleTankPcbaParsedText(int index,
                                 const SingleTankPcbaEntry &entry,
                                 const SingleTankPcbaEntry *trendEntry = nullptr)
{
    if (entry.kind == 2) {
        return QString("电流=%1").arg(formatCurrentUaX100(entry.currentUaX100, entry.ok, 2, true));
    }
    if (entry.kind == 3) {
        return "前置趋势采样失败，本步骤未发送";
    }
    if (entry.kind == 4) {
        const auto &specs = singleTankPcbaStepSpecs();
        const QString targetText = index >= 0 && index < static_cast<int>(specs.size())
            ? formatPressure001mmHg(static_cast<int>(specs[static_cast<size_t>(index)].payloadValue), true, 1, true)
            : QStringLiteral("--");
        return QString("预测=%1 | 目标=%2 | 斜率=%3mmHg/s | 最大残差=%4 | 新样本=%5 | 观察=%6ms")
            .arg(formatTrendPressure001mmHg(entry.trendPredictedPressure001mmHg, 2))
            .arg(targetText)
            .arg(entry.trendSlope001mmHgPerSecond / 1000.0, 0, 'f', 3)
            .arg(formatTrendPressure001mmHg(entry.trendMaxResidual001mmHg, 2))
            .arg(entry.trendSampleCount)
            .arg(entry.trendObservationUs / 1000.0, 0, 'f', 0);
    }
    if (entry.kind == 5) {
        return QString("VENT最后压力检测1=%1 | 零点要求=0.0±0.1mmHg")
            .arg(formatPressure001mmHg(static_cast<int>(entry.parsedValue), true, 1, true));
    }
    if (entry.kind == 0) {
        return entry.ok ? "唤醒回00" : "未收到00唤醒回包";
    }
    const auto &specs = singleTankPcbaStepSpecs();
    const uint8_t command = (index >= 0 && index < static_cast<int>(specs.size()))
        ? specs[static_cast<size_t>(index)].command
        : entry.command;
    if (command == 0x10) {
        return QString("标定基准=%1 | 协议下发=%2（0.001mmHg）")
            .arg(formatPressure001mmHg(static_cast<int>(entry.parsedValue), true, 1, true))
            .arg(entry.parsedValue);
    }
    if (command == 0x05) {
        return QString("VENT后压力检测1=%1 | ACK数据=%2")
            .arg(formatPressure001mmHg(static_cast<int>(entry.parsedValue), true, 1, true))
            .arg(entry.responseLength > 0 ? entry.responseData[0] : 0);
    }
    if (command == 0x11) {
        uint32_t mprlsPressure001mmHg = 0u;
        const bool hasMprlsPressure =
            singleTankPcbaComparisonPressure(entry, trendEntry, &mprlsPressure001mmHg);
        const QString mprlsPressureText = hasMprlsPressure
            ? formatTrendPressure001mmHg(mprlsPressure001mmHg, 3)
            : QStringLiteral("--");
        if (entry.parsedValue == std::numeric_limits<uint32_t>::max()) {
            const QString pcbaText = singleTankPcbaReturnedInvalidPressureSentinel(entry)
                ? QStringLiteral("PCBA实测=无效值0xFFFFFFFF")
                : QStringLiteral("PCBA实测=未收到完整有效压力帧");
            return QString("%1 | MPRLS1同步气压=%2 | 误差=无法计算")
                .arg(pcbaText)
                .arg(mprlsPressureText);
        }
        const QString pcbaPressureText =
            formatPressure001mmHg(static_cast<int>(entry.parsedValue), true, 1, true);
        if (!hasMprlsPressure) {
            return QString("PCBA实测=%1 | MPRLS1同步气压=-- | 误差=无法计算")
                .arg(pcbaPressureText);
        }
        const int64_t error001mmHg =
            static_cast<int64_t>(entry.parsedValue) -
            static_cast<int64_t>(mprlsPressure001mmHg);
        const double errorMmHg = error001mmHg / 1000.0;
        return QString("PCBA实测=%1 | MPRLS1同步气压=%2 | 误差=%3%4mmHg")
            .arg(pcbaPressureText)
            .arg(mprlsPressureText)
            .arg(errorMmHg >= 0.0 ? "+" : "")
            .arg(errorMmHg, 0, 'f', 3);
    }
    if (command == 0x01) {
        return QString("版本配置=0x%1，期望0A 0A")
            .arg(entry.parsedValue, 4, 16, QLatin1Char('0'))
            .toUpper();
    }
    if (index == 1) {
        if (!entry.ok && entry.rawResponseLength == 0) {
            return "10ms内未收到PCBA回包";
        }
        return QString("开机回包长度=%1字节，允许00/00 00/ACK/其他").arg(entry.parsedValue);
    }
    if (command == 0x06 || command == 0x07 || command == 0x08) {
        return QString("控制值=%1，ACK数据=%2").arg(entry.parsedValue).arg(entry.responseLength > 0 ? entry.responseData[0] : 0);
    }
    return QString("ACK数据=%1").arg(entry.parsedValue);
}

QString singleTankPcbaReasonText(const SingleTankPcbaEntry &entry,
                                 const SingleTankPcbaEntry *trendEntry = nullptr)
{
    if (entry.kind == 3) {
        return "前置趋势采样失败，已按“继续”跳过";
    }
    if (entry.kind == 2) {
        return entry.ok ? QStringLiteral("通过") : QStringLiteral("电流采样无效");
    }
    if (entry.kind == 4) {
        const QString metrics = QString("预测%1，斜率%2mmHg/s，最大残差%3，%4个新样本/%5ms")
            .arg(formatTrendPressure001mmHg(entry.trendPredictedPressure001mmHg, 2))
            .arg(entry.trendSlope001mmHgPerSecond / 1000.0, 0, 'f', 3)
            .arg(formatTrendPressure001mmHg(entry.trendMaxResidual001mmHg, 2))
            .arg(entry.trendSampleCount)
            .arg(entry.trendObservationUs / 1000.0, 0, 'f', 0);
        if (entry.ok) {
            return QString("趋势采样通过：%1").arg(metrics);
        }
        switch (entry.command) {
        case kTankLoopFailureSensorFault:
            return QString("趋势采样失败：压力检测1故障锁存 | %1").arg(metrics);
        case kTankLoopFailureSensorInvalid:
            return QString("趋势采样失败：压力检测1持续无有效采样 | %1").arg(metrics);
        case kTankLoopFailureOverpressure:
            return QString("趋势采样失败：压力达到295mmHg绝对安全上限，MCU已关阀 | %1").arg(metrics);
        case kTankLoopFailureNoPressureRise:
            return QString("趋势采样失败：进气后3秒内压力未达到最小上升量 | %1").arg(metrics);
        case kTankLoopFailureSettleTimeout:
            return QString("趋势采样失败：总超时内拟合偏差或下降率未满足要求 | %1").arg(metrics);
        case kTankLoopFailureTrendSamples:
            return QString("趋势采样失败：新样本数量或采样跨度不足 | %1").arg(metrics);
        case kTankLoopFailureTrendResidual:
            return QString("趋势采样失败：最大拟合残差超限 | %1").arg(metrics);
        case kTankLoopFailureTrendDropRate:
            return QString("趋势采样失败：自然下降率超限 | %1").arg(metrics);
        case kTankLoopFailureTrendDirection:
            return QString("趋势采样失败：观察结束时趋势仍在上升，禁止标定或查询 | %1").arg(metrics);
        default:
            return QString("趋势采样未通过（固件未上报详细原因） | %1").arg(metrics);
        }
    }
    if (entry.kind == 5) {
        switch (entry.command) {
        case kTankLoopFailureSensorFault:
            return "零点VENT失败：压力检测1故障锁存，未发送记录零点";
        case kTankLoopFailureSensorInvalid:
            return "零点VENT失败：压力检测1持续无有效采样，未发送记录零点";
        case kTankLoopFailureOverpressure:
            return "零点VENT失败：压力达到295mmHg绝对安全上限，MCU已关阀";
        default:
            return "零点VENT未完成，未发送记录零点";
        }
    }
    if (entry.ok) {
        return entry.elapsedUs > 0u
            ? QString("通过，回包耗时%1ms（10ms上限内仅统计）")
                  .arg(entry.elapsedUs / 1000.0, 0, 'f', 3)
            : QStringLiteral("通过");
    }
    if (entry.rawResponseLength == 0u && entry.elapsedUs == 0u) {
        return "未执行或固件未返回该步骤结果";
    }
    if (entry.rawResponseLength == 0u) {
        return QString("等待%1ms未收到任何PCBA回包")
            .arg(entry.elapsedUs / 1000.0, 0, 'f', 3);
    }

    const double elapsedMs = entry.elapsedUs / 1000.0;
    if (entry.responseCommandOrByte == 0x7Fu) {
        if (entry.responseLength == 1u && entry.responseData[0] == 1u) {
            return QString("%1ms收到ACK NO，步骤被PCBA拒绝").arg(elapsedMs, 0, 'f', 3);
        }
        return QString("%1ms已收到ACK帧，但长度、数据或CRC不符合成功ACK")
            .arg(elapsedMs, 0, 'f', 3);
    }
    if (entry.command == 0x11u && entry.responseCommandOrByte == 0x11u) {
        if (entry.parsedValue == std::numeric_limits<uint32_t>::max()) {
            if (singleTankPcbaReturnedInvalidPressureSentinel(entry)) {
                return QString("%1ms收到PCBA无效压力0xFFFFFFFF")
                    .arg(elapsedMs, 0, 'f', 3);
            }
            return QString("%1ms只收到PCBA残帧，未取得完整0x11四字节压力回包")
                .arg(elapsedMs, 0, 'f', 3);
        }
        uint32_t mprlsPressure001mmHg = 0u;
        if (singleTankPcbaComparisonPressure(entry,
                                             trendEntry,
                                             &mprlsPressure001mmHg)) {
            const int64_t error001mmHg =
                static_cast<int64_t>(entry.parsedValue) -
                static_cast<int64_t>(mprlsPressure001mmHg);
            const double errorMmHg = error001mmHg / 1000.0;
            const double absoluteErrorMmHg =
                std::abs(static_cast<double>(error001mmHg)) / 1000.0;
            return QString("%1ms收到PCBA实测%2；MPRLS1同步气压%3；误差%4%5mmHg，绝对误差%6mmHg，超过允许误差")
                .arg(elapsedMs, 0, 'f', 3)
                .arg(formatPressure001mmHg(static_cast<int>(entry.parsedValue),
                                           true,
                                           1,
                                           true))
                .arg(formatTrendPressure001mmHg(
                    mprlsPressure001mmHg,
                    3))
                .arg(errorMmHg >= 0.0 ? "+" : "")
                .arg(errorMmHg, 0, 'f', 3)
                .arg(absoluteErrorMmHg, 0, 'f', 3);
        }
        return QString("%1ms收到PCBA压力%2，但与压力检测1基准差超过允许误差")
            .arg(elapsedMs, 0, 'f', 3)
            .arg(formatPressure001mmHg(static_cast<int>(entry.parsedValue),
                                       true,
                                       1,
                                       true));
    }
    if (entry.command == 0x01u) {
        return QString("%1ms已收到版本回包，但内容不是0A 0A")
            .arg(elapsedMs, 0, 'f', 3);
    }
    return QString("%1ms已收到回包，但命令、长度或内容不符合本步骤要求")
        .arg(elapsedMs, 0, 'f', 3);
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

QString singleTankPcbaParsedDetail(int index,
                                   const SingleTankPcbaEntry &entry,
                                   const SingleTankPcbaEntry *trendEntry = nullptr)
{
    if (entry.kind == 2) {
        return QString("电流采样=%1").arg(formatCurrentUaX100(entry.currentUaX100, entry.ok, 2, true));
    }
    if (entry.kind == 4) {
        return QString("%1；后续标定或查询使用该预测压力")
            .arg(singleTankPcbaParsedText(index, entry, trendEntry));
    }
    if (entry.kind == 0) {
        return QString("单字节唤醒回包=0x%1").arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0')).toUpper();
    }
    return QString("%1 | 字段解析 cmd=0x%2 ch=%3 len=%4 data=%5")
        .arg(singleTankPcbaParsedText(index, entry, trendEntry))
        .arg(entry.responseCommandOrByte, 2, 16, QLatin1Char('0'))
        .arg(entry.responseChannel)
        .arg(entry.responseLength)
        .arg(responseDataText(entry.responseData, entry.responseLength))
        .toUpper();
}

const SingleTankPcbaEntry *singleTankPcbaTrendSource(const SingleTankPcbaReport &report,
                                                     int row)
{
    for (int offset = 1; offset <= 2; ++offset) {
        const int candidate = row - offset;
        if (candidate >= 0 &&
            report.entries[static_cast<size_t>(candidate)].kind == 4u) {
            return &report.entries[static_cast<size_t>(candidate)];
        }
    }
    return nullptr;
}

QString singleTankPcbaSerialLogText(const SingleTankPcbaReport &report)
{
    QStringList lines;
    lines << "说明：这里显示MCU趋势采样动作及软UART1 <-> PCBA串口收发。趋势观察窗不是UART超时；每路PCBA回包接受上限为10ms。";
    const int visibleRows = kSingleTankPcbaStepCount;
    for (int row = 0; row < visibleRows; ++row) {
        const bool hasEntry = row < report.count;
        const SingleTankPcbaEntry entry = hasEntry ? report.entries[static_cast<size_t>(row)] : SingleTankPcbaEntry{};
        lines << QString("[%1] %2").arg(row + 1, 2, 10, QLatin1Char('0')).arg(singleTankPcbaStepText(row));
        if (hasEntry) {
            const SingleTankPcbaEntry *trendEntry =
                singleTankPcbaTrendSource(report, row);
            lines << QString("  已执行动作/档位: %1").arg(singleTankPcbaActionText(row, entry.flags));
            lines << QString("  MCU -> PCBA: %1").arg(singleTankPcbaTxTextForEntry(row, entry));
            lines << QString("  PCBA -> MCU: %1").arg(singleTankPcbaRxText(entry));
            lines << QString("  解析: %1").arg(singleTankPcbaParsedDetail(row, entry, trendEntry));
            const QString timing = entry.kind == 2
                ? QStringLiteral("--")
                : (entry.kind == 4
                    ? QString("趋势观察 %1 ms").arg(entry.trendObservationUs / 1000.0, 0, 'f', 0)
                    : QString("UART回包 %1 ms").arg(entry.elapsedUs / 1000.0, 0, 'f', 3));
            lines << QString("  耗时: %1 | 判定: %2")
                         .arg(timing, singleTankPcbaReasonText(entry, trendEntry));
        } else {
            lines << QString("  当前动作/档位: %1").arg(singleTankPcbaActionText(row, expectedSingleTankPcbaFlags(row)));
            lines << QString("  MCU -> PCBA: %1").arg(singleTankPcbaTxText(row));
            lines << ((row == 0 || row == 2)
                          ? "  正在等待: MCU 完成供电切换/电流采样并返回该步骤结果"
                          : (singleTankPcbaStepSpecs()[static_cast<size_t>(row)].skipOnly
                              ? "  正在等待: MCU完成单次加压和趋势采样"
                              : "  正在等待: PCBA回包或MCU返回10ms超时结果"));
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
    case 5: return "传感器校准";
    case 6: return "阈值与手动阀";
    case 7: return "ADC实时基准";
    case 8: return "RTC时钟调试模式";
    case 9: return "固件烧录";
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

QString singleTankProtectionReasonText(const FixtureSnapshot &snapshot)
{
    switch (snapshot.singleTankProtectionReason) {
    case kSingleTankProtectionSensorFault:
        return QStringLiteral("压力传感器异常");
    case kSingleTankProtectionNoRise:
        return QStringLiteral("持续打气但压力没有正常上升");
    default:
        return QStringLiteral("未知保护原因");
    }
}

QString singleTankProtectionUiText(const FixtureSnapshot &snapshot)
{
    if (!snapshot.singleTankProtectionActive) {
        return {};
    }

    const QString sensorText = snapshot.singleTankProtectionSensorIndex >= 0
        ? QStringLiteral("压力检测%1").arg(snapshot.singleTankProtectionSensorIndex + 1)
        : QStringLiteral("对应压力检测");
    const QString valveText = snapshot.singleTankProtectionInletValve > 0
        ? QStringLiteral("进气阀V%1").arg(snapshot.singleTankProtectionInletValve)
        : QStringLiteral("对应进气阀");
    return QStringLiteral("MCU保护触发 | 已强制关闭%1 | %2 | %3")
        .arg(valveText)
        .arg(sensorText)
        .arg(singleTankProtectionReasonText(snapshot));
}

} // namespace

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
    installEventFilter(this);
    QCoreApplication::instance()->installEventFilter(this);

    connect(&m_transport, &WindowsSerialTransport::bytesReceived, this, &MainWindow::handleSerialBytes);
    connect(&m_transport, &WindowsSerialTransport::errorOccurred, this, &MainWindow::handleSerialError);
    connect(&m_transport, &WindowsSerialTransport::openChanged, this, [this](bool open) {
        m_connectButton->setText(open ? "断开" : "连接");
        m_snapshot.linkMode = open ? LinkMode::UsbCdc : LinkMode::Disconnected;
        if (!open) {
            m_sensorCalibrationJogTimer.stop();
            m_sensorCalibrationPollTimer.stop();
            m_sensorCalibrationJogActuator = usb::SensorCalibrationStop;
            m_sensorCalibrationPendingRecordPoint = -1;
            m_sensorCalibrationPendingRecordActual001mmHg = 0;
            m_sensorCalibrationPendingRecordAcknowledged = false;
            m_sensorCalibrationStatus = SensorCalibrationStatus{};
            setSensorCalibrationCapability(false);
            if (m_singleTankRunning) {
                m_singleTankTimer.stop();
                m_singleTankRunning = false;
                resetSingleTankLoopControl();
                updateSingleTankPanel();
            }
            m_singleTankPcbaStartPending = false;
            m_singleTankPcbaRunning = false;
            m_singleTankPcbaPollTimer.stop();
            setSingleTankPcbaProfileControlsEnabled(true);
            if (m_singleTankPcbaLogActive) {
                stopSingleTankPcbaLogSession(QStringLiteral("transport_closed"));
            }
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
    m_sensorCalibrationJogTimer.setInterval(150);
    connect(&m_sensorCalibrationJogTimer, &QTimer::timeout,
            this, &MainWindow::serviceSensorCalibrationJog);
    m_sensorCalibrationPollTimer.setInterval(400);
    connect(&m_sensorCalibrationPollTimer, &QTimer::timeout,
            this, &MainWindow::requestSensorCalibrationStatus);

    refreshPorts();
    QTimer::singleShot(350, this, [this]() {
        autoConnectFixtureUsbCdc();
    });
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
    m_pcbaCurrentTable = new QTableWidget(kChannelCount, 5, m_debugCurrentBox);
    m_pcbaCurrentTable->setHorizontalHeaderLabels({"通道", "10次平均电流 uA(已矫正)", "均方差", "ADC原始码(未矫正)", "内部基准矫正系数"});
    m_pcbaCurrentTable->verticalHeader()->hide();
    m_pcbaCurrentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pcbaCurrentTable->setMaximumHeight(240);
    currentLayout->addWidget(m_pcbaCurrentTable);
    auto *currentChartControlLayout = new QHBoxLayout();
    currentChartControlLayout->addWidget(new QLabel("曲线通道", m_debugCurrentBox));
    m_pcbaCurrentChartChannelCombo = new QComboBox(m_debugCurrentBox);
    for (int i = 0; i < kChannelCount; ++i) {
        m_pcbaCurrentChartChannelCombo->addItem(QString("通道%1").arg(i + 1), i);
    }
    currentChartControlLayout->addWidget(m_pcbaCurrentChartChannelCombo);
    currentChartControlLayout->addStretch(1);
    currentLayout->addLayout(currentChartControlLayout);
    m_pcbaCurrentChartSummaryLabel = new QLabel("等待采样结果", m_debugCurrentBox);
    m_pcbaCurrentChartSummaryLabel->setWordWrap(true);
    m_pcbaCurrentChartSummaryLabel->setStyleSheet("color: #475569;");
    currentLayout->addWidget(m_pcbaCurrentChartSummaryLabel);
    auto *currentChart = new QChart();
    currentChart->legend()->setVisible(true);
    currentChart->setTitle("PCBA 电流 10次采样曲线");
    m_pcbaCurrentSeries = new QLineSeries(currentChart);
    m_pcbaCurrentSeries->setName("10次采样");
    m_pcbaCurrentAverageSeries = new QLineSeries(currentChart);
    m_pcbaCurrentAverageSeries->setName("平均值");
    {
        QPen averagePen(QColor("#dc2626"));
        averagePen.setWidth(2);
        averagePen.setStyle(Qt::DashLine);
        m_pcbaCurrentAverageSeries->setPen(averagePen);
    }
    currentChart->addSeries(m_pcbaCurrentSeries);
    currentChart->addSeries(m_pcbaCurrentAverageSeries);
    m_pcbaCurrentAxisX = new QValueAxis(currentChart);
    m_pcbaCurrentAxisX->setTitleText("采样序号");
    m_pcbaCurrentAxisX->setLabelFormat("%d");
    m_pcbaCurrentAxisX->setTickCount(kCurrentSampleCount);
    m_pcbaCurrentAxisX->setRange(1, kCurrentSampleCount);
    currentChart->addAxis(m_pcbaCurrentAxisX, Qt::AlignBottom);
    m_pcbaCurrentSeries->attachAxis(m_pcbaCurrentAxisX);
    m_pcbaCurrentAverageSeries->attachAxis(m_pcbaCurrentAxisX);
    m_pcbaCurrentAxisY = new QValueAxis(currentChart);
    m_pcbaCurrentAxisY->setTitleText("电流 (uA)");
    m_pcbaCurrentAxisY->setLabelFormat("%.2f");
    currentChart->addAxis(m_pcbaCurrentAxisY, Qt::AlignLeft);
    m_pcbaCurrentSeries->attachAxis(m_pcbaCurrentAxisY);
    m_pcbaCurrentAverageSeries->attachAxis(m_pcbaCurrentAxisY);
    m_pcbaCurrentChartView = new QChartView(currentChart, m_debugCurrentBox);
    m_pcbaCurrentChartView->setRenderHint(QPainter::Antialiasing);
    m_pcbaCurrentChartView->setMinimumHeight(260);
    currentLayout->addWidget(m_pcbaCurrentChartView);
    connect(currentStartButton, &QPushButton::clicked, this, &MainWindow::enterPcbaCurrentTest);
    connect(currentStopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    connect(m_pcbaSupplyVoltageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::handlePcbaSupplyVoltageChanged);
    connect(m_pcbaCurrent50mACheck, &QCheckBox::toggled, this, &MainWindow::setPcbaCurrent50mAEnabled);
    connect(m_pcbaCurrentChartChannelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updatePcbaCurrentChart(); });
    debugPageLayout->addWidget(m_debugCurrentBox);

    m_debugSinglePcbaBox = new QGroupBox("单PCBA全流程测试", debugPage);
    auto *singlePcbaLayout = new QVBoxLayout(m_debugSinglePcbaBox);
    auto *singlePcbaHint = new QLabel("固定使用 1号位 UART1；该调试模式不等待工装压合/压合开关。页面会同时展示 MCU 与 PCBA 的串口测试链路，以及本次流程里待机 uA / 工作 mA 两段电流各 10 次采样曲线。", m_debugSinglePcbaBox);
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
    singlePcbaLayout->addWidget(new QLabel("本次电流采样图（固定 1号位）", m_debugSinglePcbaBox));
    initializeCurrentChartWidgets(m_singlePcbaStandbyChart,
                                  m_debugSinglePcbaBox,
                                  singlePcbaLayout,
                                  QStringLiteral("待机电流 10次采样曲线"));
    initializeCurrentChartWidgets(m_singlePcbaWorkChart,
                                  m_debugSinglePcbaBox,
                                  singlePcbaLayout,
                                  QStringLiteral("工作电流 10次采样曲线"));
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
    auto *singleTankPcbaHint = new QLabel("按固件内置单罐单PCBA流程执行。每个压力点单次加压到目标后关闭进气，在设定窗口内拟合自然压力趋势；最大偏差和下降率合格后，使用预测压力做标定或查询。记录零点前及测试结束后MCU都会先VENT；每次开阀至少30秒，达到0.0±0.1mmHg后继续VENT 30秒，再关阀均压并确认静态零点。", m_debugSingleTankPcbaBox);
    singleTankPcbaHint->setWordWrap(true);
    singleTankPcbaHint->setStyleSheet("color: #475569;");
    singleTankPcbaLayout->addWidget(singleTankPcbaHint);
    auto *singleTankPcbaButtonLayout = new QHBoxLayout();
    auto *singleTankPcbaStartButton = new QPushButton("开始单罐单PCBA测试", m_debugSingleTankPcbaBox);
    auto *singleTankPcbaStopButton = new QPushButton("停止", m_debugSingleTankPcbaBox);
    singleTankPcbaButtonLayout->addWidget(singleTankPcbaStartButton);
    singleTankPcbaButtonLayout->addWidget(singleTankPcbaStopButton);
    singleTankPcbaLayout->addLayout(singleTankPcbaButtonLayout);
    auto *singleTankPcbaProfileLayout = new QGridLayout();
    m_singleTankPcbaMaxDeviationSpin = new QDoubleSpinBox(m_debugSingleTankPcbaBox);
    m_singleTankPcbaMaxDeviationSpin->setRange(0.05, 5.0);
    m_singleTankPcbaMaxDeviationSpin->setDecimals(2);
    m_singleTankPcbaMaxDeviationSpin->setSingleStep(0.01);
    m_singleTankPcbaMaxDeviationSpin->setValue(0.50);
    m_singleTankPcbaMaxDeviationSpin->setSuffix(" mmHg");
    m_singleTankPcbaMaxDeviationSpin->setToolTip("趋势拟合允许的最大残差；达到目标即关进气，不会主动加压到目标值以上");
    m_singleTankPcbaTrendWindowSpin = new QSpinBox(m_debugSingleTankPcbaBox);
    m_singleTankPcbaTrendWindowSpin->setRange(1000, 20000);
    m_singleTankPcbaTrendWindowSpin->setSingleStep(100);
    m_singleTankPcbaTrendWindowSpin->setValue(3000);
    m_singleTankPcbaTrendWindowSpin->setSuffix(" ms");
    m_singleTankPcbaTrendWindowSpin->setToolTip("关闭进气后采集新鲜压力样本并拟合自然变化趋势的时间窗；最少1000ms");
    m_singleTankPcbaMaxDropRateSpin = new QDoubleSpinBox(m_debugSingleTankPcbaBox);
    m_singleTankPcbaMaxDropRateSpin->setRange(0.05, 10.0);
    m_singleTankPcbaMaxDropRateSpin->setDecimals(2);
    m_singleTankPcbaMaxDropRateSpin->setSingleStep(0.01);
    m_singleTankPcbaMaxDropRateSpin->setValue(3.0);
    m_singleTankPcbaMaxDropRateSpin->setSuffix(" mmHg/s");
    m_singleTankPcbaMaxDropRateSpin->setToolTip("拟合趋势允许的最大自然下降率；单位为mmHg/s");
    singleTankPcbaProfileLayout->addWidget(new QLabel("趋势最大偏差", m_debugSingleTankPcbaBox), 0, 0);
    singleTankPcbaProfileLayout->addWidget(new QLabel("趋势观察时间", m_debugSingleTankPcbaBox), 0, 1);
    singleTankPcbaProfileLayout->addWidget(new QLabel("最大允许下降率", m_debugSingleTankPcbaBox), 0, 2);
    singleTankPcbaProfileLayout->addWidget(m_singleTankPcbaMaxDeviationSpin, 1, 0);
    singleTankPcbaProfileLayout->addWidget(m_singleTankPcbaTrendWindowSpin, 1, 1);
    singleTankPcbaProfileLayout->addWidget(m_singleTankPcbaMaxDropRateSpin, 1, 2);
    singleTankPcbaProfileLayout->setColumnStretch(0, 1);
    singleTankPcbaProfileLayout->setColumnStretch(1, 1);
    singleTankPcbaProfileLayout->setColumnStretch(2, 1);
    singleTankPcbaLayout->addLayout(singleTankPcbaProfileLayout);
    m_singleTankPcbaContinueOnFailCheck = new QCheckBox("检测项不合格或回包超时后继续", m_debugSingleTankPcbaBox);
    m_singleTankPcbaContinueOnFailCheck->setChecked(true);
    m_singleTankPcbaContinueOnFailCheck->setToolTip("失败项仍记为不通过；压力传感器故障、硬超压等安全故障仍会停机");
    singleTankPcbaLayout->addWidget(m_singleTankPcbaContinueOnFailCheck);
    m_singleTankPcbaStatusLabel = new QLabel(m_debugSingleTankPcbaBox);
    m_singleTankPcbaStatusLabel->setWordWrap(true);
    m_singleTankPcbaStatusLabel->setStyleSheet("color: #475569;");
    singleTankPcbaLayout->addWidget(m_singleTankPcbaStatusLabel);
    auto *singleTankPcbaLogLayout = new QHBoxLayout();
    m_singleTankPcbaLogStatusLabel = new QLabel(m_debugSingleTankPcbaBox);
    m_singleTankPcbaLogStatusLabel->setWordWrap(true);
    m_singleTankPcbaLogStatusLabel->setStyleSheet("color: #475569;");
    auto *singleTankPcbaOpenLogButton = new QPushButton("打开LOG目录", m_debugSingleTankPcbaBox);
    singleTankPcbaLogLayout->addWidget(m_singleTankPcbaLogStatusLabel, 1);
    singleTankPcbaLogLayout->addWidget(singleTankPcbaOpenLogButton);
    singleTankPcbaLayout->addLayout(singleTankPcbaLogLayout);
    m_singleTankPcbaSummaryLabel = new QLabel(m_debugSingleTankPcbaBox);
    m_singleTankPcbaSummaryLabel->setWordWrap(true);
    m_singleTankPcbaSummaryLabel->setStyleSheet("font-weight: 700; color: #0f172a;");
    singleTankPcbaLayout->addWidget(m_singleTankPcbaSummaryLabel);
    singleTankPcbaLayout->addWidget(new QLabel("本次电流采样图（固定 1号位）", m_debugSingleTankPcbaBox));
    initializeCurrentChartWidgets(m_singleTankPcbaStandbyChart,
                                  m_debugSingleTankPcbaBox,
                                  singleTankPcbaLayout,
                                  QStringLiteral("待机电流 10次采样曲线"));
    initializeCurrentChartWidgets(m_singleTankPcbaWorkChart,
                                  m_debugSingleTankPcbaBox,
                                  singleTankPcbaLayout,
                                  QStringLiteral("工作电流 10次采样曲线"));
    m_singleTankPcbaTable = new QTableWidget(0, 8, m_debugSingleTankPcbaBox);
    m_singleTankPcbaTable->setHorizontalHeaderLabels({"测试项目", "动作/档位", "发送给PCBA", "PCBA回包", "解析信息", "耗时", "电流", "判定/原因"});
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
    singleTankPcbaLayout->addWidget(new QLabel("趋势采样与MCU <-> PCBA串口明细", m_debugSingleTankPcbaBox));
    m_singleTankPcbaSerialLog = new QPlainTextEdit(m_debugSingleTankPcbaBox);
    m_singleTankPcbaSerialLog->setReadOnly(true);
    m_singleTankPcbaSerialLog->setMaximumBlockCount(700);
    m_singleTankPcbaSerialLog->setMinimumHeight(220);
    m_singleTankPcbaSerialLog->setFont(QFont("Cascadia Mono", 9));
    singleTankPcbaLayout->addWidget(m_singleTankPcbaSerialLog);
    connect(singleTankPcbaStartButton, &QPushButton::clicked, this, &MainWindow::startSingleTankPcbaFlow);
    connect(singleTankPcbaStopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    connect(singleTankPcbaOpenLogButton, &QPushButton::clicked,
            this, &MainWindow::openSingleTankPcbaLogFolder);
    updateSingleTankPcbaLogUi();
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
    m_singleTankLogCheck = new QCheckBox("长时间跑时自动记录日志（建议开启）", m_debugSingleTankBox);
    m_singleTankLogCheck->setChecked(true);
    m_singleTankStatusLabel = new QLabel(m_debugSingleTankBox);
    m_singleTankStatusLabel->setWordWrap(true);
    m_singleTankStatusLabel->setStyleSheet("color: #475569;");
    m_singleTankLogStatusLabel = new QLabel(m_debugSingleTankBox);
    m_singleTankLogStatusLabel->setWordWrap(true);
    m_singleTankLogStatusLabel->setStyleSheet("color: #475569;");
    auto *singleTankOpenLogButton = new QPushButton("打开日志目录", m_debugSingleTankBox);
    singleTankLayout->addWidget(new QLabel("罐体"), 0, 0);
    singleTankLayout->addWidget(m_singleTankCombo, 0, 1, 1, 2);
    singleTankLayout->addWidget(new QLabel("目标"), 1, 0);
    singleTankLayout->addWidget(m_singleTankTargetSpin, 1, 1);
    singleTankLayout->addWidget(new QLabel("容差"), 1, 2);
    singleTankLayout->addWidget(m_singleTankToleranceSpin, 1, 3);
    singleTankLayout->addWidget(m_singleTankStartButton, 2, 0, 1, 2);
    singleTankLayout->addWidget(m_singleTankStopButton, 2, 2, 1, 2);
    singleTankLayout->addWidget(m_singleTankLogCheck, 3, 0, 1, 3);
    singleTankLayout->addWidget(singleTankOpenLogButton, 3, 3);
    singleTankLayout->addWidget(m_singleTankStatusLabel, 4, 0, 1, 4);
    singleTankLayout->addWidget(m_singleTankLogStatusLabel, 5, 0, 1, 4);
    connect(m_singleTankCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::handleSingleTankSelectionChanged);
    connect(m_singleTankStartButton, &QPushButton::clicked, this, &MainWindow::startSingleTankLoop);
    connect(m_singleTankStopButton, &QPushButton::clicked, this, &MainWindow::stopSingleTankLoop);
    connect(singleTankOpenLogButton, &QPushButton::clicked, this, &MainWindow::openSingleTankLogFolder);
    handleSingleTankSelectionChanged(m_singleTankCombo->currentIndex());
    updateSingleTankLogUi();
    debugPageLayout->addWidget(m_debugSingleTankBox);

    m_debugSensorCalibrationBox = new QGroupBox("传感器校准", debugPage);
    auto *calibrationLayout = new QVBoxLayout(m_debugSensorCalibrationBox);
    auto *calibrationHint = new QLabel(
        "支持借用 IIC1 工位和原位校准。MCU 按所选模式自动限制传感器、罐体和阀门资源；正压实测输入安全上限为 294.999 mmHg。",
        m_debugSensorCalibrationBox);
    calibrationHint->setWordWrap(true);
    calibrationHint->setStyleSheet("font-weight: 700; color: #0f172a;");
    calibrationLayout->addWidget(calibrationHint);

    m_sensorCalibrationCapabilityLabel = new QLabel(m_debugSensorCalibrationBox);
    m_sensorCalibrationCapabilityLabel->setWordWrap(true);
    calibrationLayout->addWidget(m_sensorCalibrationCapabilityLabel);

    auto *calibrationModeLayout = new QHBoxLayout();
    calibrationModeLayout->addWidget(new QLabel("校准方式", m_debugSensorCalibrationBox));
    m_sensorCalibrationModeCombo = new QComboBox(m_debugSensorCalibrationBox);
    m_sensorCalibrationModeCombo->addItem("借用IIC1工位", 0);
    m_sensorCalibrationModeCombo->addItem("原位校准", 1);
    calibrationModeLayout->addWidget(m_sensorCalibrationModeCombo, 1);
    calibrationLayout->addLayout(calibrationModeLayout);

    auto *calibrationSlotLayout = new QHBoxLayout();
    m_sensorCalibrationSlotTitleLabel = new QLabel("保存到传感器位置", m_debugSensorCalibrationBox);
    calibrationSlotLayout->addWidget(m_sensorCalibrationSlotTitleLabel);
    m_sensorCalibrationSlotCombo = new QComboBox(m_debugSensorCalibrationBox);
    for (int slot = 1; slot <= kPressureSensorCount; ++slot) {
        m_sensorCalibrationSlotCombo->addItem(QString("压力检测%1").arg(slot), slot);
    }
    calibrationSlotLayout->addWidget(m_sensorCalibrationSlotCombo, 1);
    m_sensorCalibrationSlotLabel = new QLabel(m_debugSensorCalibrationBox);
    m_sensorCalibrationSlotLabel->setMinimumWidth(92);
    calibrationSlotLayout->addWidget(m_sensorCalibrationSlotLabel);
    calibrationLayout->addLayout(calibrationSlotLayout);

    m_sensorCalibrationLiveLabel = new QLabel(m_debugSensorCalibrationBox);
    m_sensorCalibrationLiveLabel->setWordWrap(true);
    m_sensorCalibrationLiveLabel->setStyleSheet("color: #334155;");
    calibrationLayout->addWidget(m_sensorCalibrationLiveLabel);

    m_sensorCalibrationStateLabel = new QLabel(m_debugSensorCalibrationBox);
    m_sensorCalibrationStateLabel->setWordWrap(true);
    m_sensorCalibrationStateLabel->setStyleSheet("color: #475569;");
    calibrationLayout->addWidget(m_sensorCalibrationStateLabel);

    auto *calibrationModeButtons = new QHBoxLayout();
    m_sensorCalibrationEnterButton = new QPushButton("进入校准模式", m_debugSensorCalibrationBox);
    m_sensorCalibrationExitButton = new QPushButton("退出并关阀", m_debugSensorCalibrationBox);
    calibrationModeButtons->addWidget(m_sensorCalibrationEnterButton);
    calibrationModeButtons->addWidget(m_sensorCalibrationExitButton);
    calibrationLayout->addLayout(calibrationModeButtons);

    auto *calibrationJogButtons = new QHBoxLayout();
    m_sensorCalibrationFillButton = new QPushButton("按住充气（V1）", m_debugSensorCalibrationBox);
    m_sensorCalibrationReleaseButton = new QPushButton("按住微量放气（V21）", m_debugSensorCalibrationBox);
    m_sensorCalibrationFillButton->setMinimumHeight(42);
    m_sensorCalibrationReleaseButton->setMinimumHeight(42);
    m_sensorCalibrationFillButton->setToolTip("按住期间每150ms向MCU续租500ms，松开立即发送停止");
    m_sensorCalibrationReleaseButton->setToolTip("按住期间每150ms向MCU续租500ms，松开立即发送停止");
    m_sensorCalibrationFillButton->installEventFilter(this);
    m_sensorCalibrationReleaseButton->installEventFilter(this);
    calibrationJogButtons->addWidget(m_sensorCalibrationFillButton);
    calibrationJogButtons->addWidget(m_sensorCalibrationReleaseButton);
    calibrationLayout->addLayout(calibrationJogButtons);

    auto *calibrationVentButtons = new QHBoxLayout();
    m_sensorCalibrationVentButton = new QPushButton("VENT至零压", m_debugSensorCalibrationBox);
    m_sensorCalibrationCancelVentButton = new QPushButton("取消VENT", m_debugSensorCalibrationBox);
    calibrationVentButtons->addWidget(m_sensorCalibrationVentButton);
    calibrationVentButtons->addWidget(m_sensorCalibrationCancelVentButton);
    calibrationLayout->addLayout(calibrationVentButtons);

    m_sensorCalibrationPointTable = new QTableWidget(4, 5, m_debugSensorCalibrationBox);
    m_sensorCalibrationPointTable->setHorizontalHeaderLabels(
        {"点位", "实测气压", "原始计数", "状态", "操作"});
    m_sensorCalibrationPointTable->verticalHeader()->hide();
    m_sensorCalibrationPointTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_sensorCalibrationPointTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sensorCalibrationPointTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sensorCalibrationPointTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_sensorCalibrationPointTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_sensorCalibrationPointTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_sensorCalibrationPointTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_sensorCalibrationPointTable->setMinimumHeight(160);
    m_sensorCalibrationPointTable->setMaximumHeight(180);
    const QStringList calibrationPointNames{"零点", "标定点1", "标定点2", "标定点3"};
    for (int point = 0; point < 4; ++point) {
        auto *nameItem = new QTableWidgetItem(calibrationPointNames[point]);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_sensorCalibrationPointTable->setItem(point, 0, nameItem);
        auto *actualSpin = new QDoubleSpinBox(m_sensorCalibrationPointTable);
        actualSpin->setRange(0.0, 294.999);
        actualSpin->setDecimals(3);
        actualSpin->setSingleStep(0.1);
        actualSpin->setSuffix(" mmHg");
        actualSpin->setReadOnly(false);
        actualSpin->setKeyboardTracking(false);
        actualSpin->setFocusPolicy(Qt::StrongFocus);
        actualSpin->setValue(kSensorCalibrationDefaultActualMmHg[static_cast<size_t>(point)]);
        actualSpin->setEnabled(point != 0);
        m_sensorCalibrationActualSpins[static_cast<size_t>(point)] = actualSpin;
        m_sensorCalibrationPointTable->setCellWidget(point, 1, actualSpin);
        m_sensorCalibrationPointTable->setItem(point, 2, new QTableWidgetItem("--"));
        m_sensorCalibrationPointTable->setItem(point, 3, new QTableWidgetItem("待记录"));
        auto *recordButton = new QPushButton(point == 0 ? "记录零点" : "记录当前点",
                                             m_sensorCalibrationPointTable);
        m_sensorCalibrationRecordButtons[static_cast<size_t>(point)] = recordButton;
        m_sensorCalibrationPointTable->setCellWidget(point, 4, recordButton);
        connect(recordButton, &QPushButton::clicked, this, [this, point]() {
            recordSensorCalibrationPoint(point);
        });
        connect(actualSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { updateSensorCalibrationPanel(); });
    }
    calibrationLayout->addWidget(m_sensorCalibrationPointTable);

    auto *calibrationStorageButtons = new QGridLayout();
    m_sensorCalibrationSaveButton = new QPushButton("写入所选传感器位置", m_debugSensorCalibrationBox);
    m_sensorCalibrationResetButton = new QPushButton("清空本次采样", m_debugSensorCalibrationBox);
    m_sensorCalibrationClearSlotButton = new QPushButton("清除所选位置标定", m_debugSensorCalibrationBox);
    calibrationStorageButtons->addWidget(m_sensorCalibrationSaveButton, 0, 0, 1, 2);
    calibrationStorageButtons->addWidget(m_sensorCalibrationResetButton, 1, 0);
    calibrationStorageButtons->addWidget(m_sensorCalibrationClearSlotButton, 1, 1);
    calibrationLayout->addLayout(calibrationStorageButtons);

    connect(m_sensorCalibrationSlotCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::handleSensorCalibrationSlotChanged);
    connect(m_sensorCalibrationModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::handleSensorCalibrationSlotChanged);
    connect(m_sensorCalibrationEnterButton, &QPushButton::clicked,
            this, &MainWindow::enterSensorCalibration);
    connect(m_sensorCalibrationExitButton, &QPushButton::clicked,
            this, &MainWindow::exitSensorCalibration);
    connect(m_sensorCalibrationFillButton, &QPushButton::pressed,
            this, &MainWindow::startSensorCalibrationFill);
    connect(m_sensorCalibrationFillButton, &QPushButton::released,
            this, &MainWindow::stopSensorCalibrationJog);
    connect(m_sensorCalibrationReleaseButton, &QPushButton::pressed,
            this, &MainWindow::startSensorCalibrationRelease);
    connect(m_sensorCalibrationReleaseButton, &QPushButton::released,
            this, &MainWindow::stopSensorCalibrationJog);
    connect(m_sensorCalibrationVentButton, &QPushButton::clicked,
            this, &MainWindow::startSensorCalibrationVent);
    connect(m_sensorCalibrationCancelVentButton, &QPushButton::clicked,
            this, &MainWindow::cancelSensorCalibrationVent);
    connect(m_sensorCalibrationSaveButton, &QPushButton::clicked,
            this, &MainWindow::saveSensorCalibration);
    connect(m_sensorCalibrationResetButton, &QPushButton::clicked,
            this, &MainWindow::resetSensorCalibrationSession);
    connect(m_sensorCalibrationClearSlotButton, &QPushButton::clicked,
            this, &MainWindow::clearSensorCalibrationSlot);
    debugPageLayout->addWidget(m_debugSensorCalibrationBox);
    setSensorCalibrationCapability(false);

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

    m_pressureTable = new QTableWidget(kPressureSensorCount, 4, panel);
    m_pressureTable->setHorizontalHeaderLabels({"压力检测", "mmHg", "标定状态", "数学饱和修复"});
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const bool calibrationJogButton = watched == m_sensorCalibrationFillButton ||
                                      watched == m_sensorCalibrationReleaseButton;
    const bool applicationDeactivated = watched == QCoreApplication::instance() &&
                                        event->type() == QEvent::ApplicationDeactivate;
    const bool jogButtonDisabled = calibrationJogButton && event->type() == QEvent::EnabledChange &&
                                   !static_cast<QWidget *>(watched)->isEnabled();
    if (applicationDeactivated || jogButtonDisabled ||
        (watched == this && event->type() == QEvent::WindowDeactivate) ||
        (calibrationJogButton &&
         (event->type() == QEvent::FocusOut || event->type() == QEvent::Leave ||
          event->type() == QEvent::Hide))) {
        stopSensorCalibrationJog();
    }
    return QMainWindow::eventFilter(watched, event);
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
    if (!isDebugMode()) {
        stopSensorCalibrationJog();
        m_sensorCalibrationPollTimer.stop();
        if (m_sensorCalibrationSupported && m_sensorCalibrationStatus.active && m_transport.isOpen()) {
            sendSensorCalibrationAction(
                usb::buildSensorCalibrationSimpleAction(nextSequence(), usb::SensorCalibrationExit),
                "SENSOR_CALIBRATION EXIT 切换生产模式");
        }
    }
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
            DebugTool::SensorCalibration,
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
    case DebugTool::SensorCalibration: return RuntimeState::SensorCalibration;
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
    const bool wasSensorCalibrationVisible =
        m_debugSensorCalibrationBox && m_debugSensorCalibrationBox->isVisible();
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
    const bool showSensorCalibration = debugMode && tool == DebugTool::SensorCalibration;
    if (m_debugSensorCalibrationBox) {
        m_debugSensorCalibrationBox->setVisible(showSensorCalibration);
    }
    if (showSensorCalibration && m_sensorCalibrationSupported && m_transport.isOpen()) {
        requestSensorCalibrationStatus();
        m_sensorCalibrationPollTimer.start();
    } else {
        m_sensorCalibrationPollTimer.stop();
        stopSensorCalibrationJog();
        if (wasSensorCalibrationVisible && m_sensorCalibrationSupported &&
            m_sensorCalibrationStatus.active && m_transport.isOpen()) {
            sendSensorCalibrationAction(
                usb::buildSensorCalibrationSimpleAction(nextSequence(), usb::SensorCalibrationExit),
                "SENSOR_CALIBRATION EXIT 离开校准工具");
        }
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
    case DebugTool::SensorCalibration:
        requestSensorCalibrationStatus();
        statusBar()->showMessage("已打开传感器校准，固定使用压力检测1和1号罐", 3000);
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
    updateSingleTankLogUi();
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
        startSingleTankLogSession();
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
    stopSingleTankLogSession(QStringLiteral("host_stop"));
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
        stopSingleTankLogSession(QStringLiteral("transport_disconnected"));
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

    if (m_snapshot.singleTankProtectionActive &&
        (m_snapshot.singleTankProtectionTankIndex < 0 ||
         m_snapshot.singleTankProtectionTankIndex == index)) {
        m_singleTankAwaitingReady = false;
        m_singleTankStatusLabel->setText(QString("%1 | %2 | MCU状态=%3")
                                             .arg(tank.name)
                                             .arg(singleTankProtectionUiText(m_snapshot))
                                             .arg(stateDisplayName(m_snapshot.state)));
        return;
    }

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
    int fixtureUsbCdcIndex = -1;
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
        if (fixtureUsbCdcIndex < 0 && port.isFixtureUsbCdc) {
            fixtureUsbCdcIndex = m_portCombo->count() - 1;
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
    if (m_transport.isOpen() && preferredIndex >= 0) {
        m_portCombo->setCurrentIndex(preferredIndex);
    } else if (fixtureUsbCdcIndex >= 0) {
        m_portCombo->setCurrentIndex(fixtureUsbCdcIndex);
    } else if (preferredIndex >= 0 &&
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

void MainWindow::autoConnectFixtureUsbCdc()
{
    if (m_transport.isOpen() || m_waitingForHello) {
        return;
    }

    refreshPorts();
    const auto ports = WindowsSerialTransport::availablePortInfos();
    for (const auto &port : ports) {
        if (!port.isFixtureUsbCdc) {
            continue;
        }

        const int comboIndex = m_portCombo->findData(port.portName);
        if (comboIndex >= 0) {
            m_portCombo->setCurrentIndex(comboIndex);
        }
        resetConnectAttempts();
        m_connectCandidatePorts.push_back(port.portName);
        m_connectCandidateDisplays.push_back(port.displayName);
        appendLog(QStringLiteral("识别到本机工装 USB CDC，自动连接 %1").arg(port.displayName));
        (void)tryOpenNextConnectCandidate();
        return;
    }
    appendLog(QStringLiteral("未识别到气压检测工装 USB CDC，保留手动连接"));
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
        if (m_singleTankLogActive) {
            stopSingleTankLogSession(QStringLiteral("manual_disconnect"));
        }
        if (m_singleTankPcbaLogActive) {
            stopSingleTankPcbaLogSession(QStringLiteral("manual_disconnect"));
        }
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
            const bool calibrationSupported = parsed.frame.payload.size() >= 2 &&
                (static_cast<uint8_t>(parsed.frame.payload[1]) & 0x04u) != 0u;
            setSensorCalibrationCapability(calibrationSupported);
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
                m_singleTankPcbaStatusLabel->setText(
                    QString("命令已确认 | MCU 正在测试 1号位 | 不合格后继续：%1")
                        .arg(m_singleTankPcbaContinueOnFail ? "开启" : "关闭"));
                m_singleTankPcbaRunning = true;
                m_singleTankPcbaPollTimer.start();
            }
            if (acceptedCommand == usb::SingleTankLoop && m_singleTankRunning && m_singleTankStatusLabel) {
                const int index = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
                const QString tankName = (index >= 0 && index < kTankCount) ? tankSpecs()[index].name : QString("单罐闭环");
                m_singleTankStatusLabel->setText(QString("%1 | MCU已确认启动命令，等待进入单罐闭环状态").arg(tankName));
            }
            if (acceptedCommand == usb::SensorCalibrationAction) {
                if (m_sensorCalibrationPendingRecordPoint >= 0) {
                    m_sensorCalibrationPendingRecordAcknowledged = true;
                    updateSensorCalibrationPanel();
                }
                requestSensorCalibrationStatus();
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
                setSingleTankPcbaProfileControlsEnabled(true);
                if (m_singleTankPcbaStatusLabel) {
                    m_singleTankPcbaStatusLabel->setText(
                        QString("启动失败 | MCU拒绝“单罐单PCBA测试”：%1。请确认 MCU 固件已包含 RUN_SINGLE_TANK_PCBA。")
                            .arg(errorText));
                }
                stopSingleTankPcbaLogSession(QStringLiteral("mcu_rejected_start:%1").arg(errorText));
                statusBar()->showMessage(QString("单罐单PCBA测试启动失败：%1").arg(errorText), 6000);
            }
            if (rejectedCommand == usb::SingleTankLoop) {
                m_singleTankTimer.stop();
                m_singleTankRunning = false;
                stopSingleTankLogSession(QStringLiteral("mcu_rejected_start"));
                resetSingleTankLoopControl();
                updateSingleTankPanel();
                if (m_singleTankStatusLabel) {
                    m_singleTankStatusLabel->setText(QString("单罐闭环启动失败 | MCU拒绝命令：%1").arg(errorText));
                }
                statusBar()->showMessage(QString("单罐闭环命令被 MCU 拒绝：%1").arg(errorText), 5000);
            }
            if (rejectedCommand == usb::SensorCalibrationAction) {
                m_sensorCalibrationJogTimer.stop();
                m_sensorCalibrationJogActuator = usb::SensorCalibrationStop;
                m_sensorCalibrationPendingRecordPoint = -1;
                m_sensorCalibrationPendingRecordActual001mmHg = 0;
                m_sensorCalibrationPendingRecordAcknowledged = false;
                updateSensorCalibrationPanel();
                if (m_sensorCalibrationStateLabel) {
                    m_sensorCalibrationStateLabel->setText(
                        QString("校准命令被 MCU 拒绝：%1").arg(errorText));
                    m_sensorCalibrationStateLabel->setStyleSheet("font-weight: 700; color: #b91c1c;");
                }
                requestSensorCalibrationStatus();
                statusBar()->showMessage(QString("传感器校准命令被拒绝：%1").arg(errorText), 6000);
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
                const bool waitingForMcuStart =
                    m_singleTankPcbaStartPending &&
                    !report.running &&
                    !report.done &&
                    report.count == 0u;
                if (waitingForMcuStart) {
                    appendLog("SINGLE_TANK_PCBA_REPORT 尚未进入运行态，保留启动等待和本次LOG");
                    continue;
                }
                recordSingleTankPcbaReport(report);
                m_singleTankPcbaReport = report;
                m_singleTankPcbaStartPending = report.running;
                m_singleTankPcbaRunning = report.running && !report.done;
                if (report.done || !report.running) {
                    m_singleTankPcbaPollTimer.stop();
                    setSingleTankPcbaProfileControlsEnabled(true);
                }
                updateSingleTankPcbaTable();
                if (report.done || !report.running) {
                    stopSingleTankPcbaLogSession(report.done ?
                        QStringLiteral("mcu_report_done") : QStringLiteral("mcu_report_stopped"));
                }
            } else {
                appendLog("SINGLE_TANK_PCBA_REPORT 长度或格式不对");
            }
        }
        if (parsed.frame.command == usb::GetSensorCalibrationStatus) {
            SensorCalibrationStatus status;
            if (usb::parseSensorCalibrationStatus(parsed.frame.payload, status)) {
                bool recordCompleted = false;
                if (m_sensorCalibrationPendingRecordPoint >= 0 &&
                    m_sensorCalibrationPendingRecordAcknowledged) {
                    const size_t point = static_cast<size_t>(m_sensorCalibrationPendingRecordPoint);
                    recordCompleted = (status.capturedMask & (1u << point)) != 0u &&
                        status.actual001mmHg[point] == m_sensorCalibrationPendingRecordActual001mmHg;
                    if (recordCompleted) {
                        m_sensorCalibrationPendingRecordPoint = -1;
                        m_sensorCalibrationPendingRecordActual001mmHg = 0;
                        m_sensorCalibrationPendingRecordAcknowledged = false;
                    }
                }
                m_sensorCalibrationStatus = status;
                m_snapshot.pressureCalibrationStatusAvailable = true;
                m_snapshot.pressureCalibrationValidMask = status.calibratedMask;
                m_snapshot.pressureCalibrationModeActive = status.active;
                m_snapshot.pressureCalibrationActuator = status.actuator;
                m_snapshot.pressureCalibrationCapturedMask = status.capturedMask;
                m_snapshot.pressureCalibrationStorageFault = status.storageFault;
                m_architectureView->setSnapshot(m_snapshot);
                updateTables();
                if (status.actuator == usb::SensorCalibrationStop &&
                    m_sensorCalibrationJogActuator == usb::SensorCalibrationStop) {
                    m_sensorCalibrationJogTimer.stop();
                }
                updateSensorCalibrationPanel();
                if (recordCompleted) {
                    statusBar()->showMessage("传感器标定点记录完成", 4000);
                }
            } else {
                appendLog("SENSOR_CALIBRATION_STATUS 长度或格式不对");
            }
        }
    }
}

void MainWindow::handleSerialError(const QString &message)
{
    if (!m_transport.isOpen()) {
        if (m_singleTankLogActive) {
            stopSingleTankLogSession(QStringLiteral("serial_error:%1").arg(message));
        }
        if (m_singleTankPcbaLogActive) {
            stopSingleTankPcbaLogSession(QStringLiteral("serial_error:%1").arg(message));
        }
    }
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
    if (m_singleTankLogActive) {
        recordSingleTankSnapshot(previous);
    }
    if (m_singleTankPcbaLogActive) {
        recordSingleTankPcbaSnapshot(previous);
    }
    if (m_snapshot.pressureCalibrationStatusAvailable) {
        m_sensorCalibrationStatus.calibratedMask = m_snapshot.pressureCalibrationValidMask;
        m_sensorCalibrationStatus.active = m_snapshot.pressureCalibrationModeActive;
        m_sensorCalibrationStatus.actuator = m_snapshot.pressureCalibrationActuator;
        m_sensorCalibrationStatus.capturedMask = m_snapshot.pressureCalibrationCapturedMask;
        m_sensorCalibrationStatus.storageFault = m_snapshot.pressureCalibrationStorageFault;
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
    updateSensorCalibrationPanel();
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
    setSingleTankPcbaProfileControlsEnabled(true);
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Stop), "STOP");
    if (m_singleTankPcbaLogActive) {
        stopSingleTankPcbaLogSession(QStringLiteral("operator_stop"));
    }
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

void MainWindow::setSingleTankPcbaProfileControlsEnabled(bool enabled)
{
    if (m_singleTankPcbaContinueOnFailCheck) {
        m_singleTankPcbaContinueOnFailCheck->setEnabled(enabled);
    }
    if (m_singleTankPcbaMaxDeviationSpin) {
        m_singleTankPcbaMaxDeviationSpin->setEnabled(enabled);
    }
    if (m_singleTankPcbaTrendWindowSpin) {
        m_singleTankPcbaTrendWindowSpin->setEnabled(enabled);
    }
    if (m_singleTankPcbaMaxDropRateSpin) {
        m_singleTankPcbaMaxDropRateSpin->setEnabled(enabled);
    }
}

void MainWindow::startSingleTankPcbaFlow()
{
    if (m_singleTankPcbaRunning || m_singleTankPcbaStartPending) {
        appendLog(QStringLiteral("单罐单PCBA测试已在运行，忽略重复启动"));
        statusBar()->showMessage(QStringLiteral("单罐单PCBA测试已在运行"), 3000);
        return;
    }
    const double maxDeviationMmHg = m_singleTankPcbaMaxDeviationSpin ?
        m_singleTankPcbaMaxDeviationSpin->value() : 0.50;
    const uint32_t trendWindowMs = m_singleTankPcbaTrendWindowSpin ?
        static_cast<uint32_t>(m_singleTankPcbaTrendWindowSpin->value()) : 3000u;
    const double maxDropRateMmHgPerSecond = m_singleTankPcbaMaxDropRateSpin ?
        m_singleTankPcbaMaxDropRateSpin->value() : 1.0;
    const uint32_t maxDeviation001mmHg =
        static_cast<uint32_t>(std::llround(maxDeviationMmHg * 1000.0));
    const uint32_t maxDropRate001mmHgPerSecond =
        static_cast<uint32_t>(std::llround(maxDropRateMmHgPerSecond * 1000.0));

    if (!isDebugMode()) {
        selectDebugMode();
    }
    if (m_singleTankRunning) {
        stopSingleTankLoop();
    }
    m_singleTankPcbaReport = SingleTankPcbaReport{};
    (void)startSingleTankPcbaLogSession();
    m_singleTankPcbaStartPending = true;
    m_singleTankPcbaRunning = true;
    m_singleTankPcbaContinueOnFail = m_singleTankPcbaContinueOnFailCheck &&
                                     m_singleTankPcbaContinueOnFailCheck->isChecked();
    setSingleTankPcbaProfileControlsEnabled(false);
    appendLog(QString("单罐单PCBA趋势参数 | 最大偏差=%1mmHg | 观察时间=%2ms | 最大允许下降率=%3mmHg/s")
                  .arg(maxDeviationMmHg, 0, 'f', 2)
                  .arg(trendWindowMs)
                  .arg(maxDropRateMmHgPerSecond, 0, 'f', 2));
    if (m_singleTankPcbaStatusLabel) {
        m_singleTankPcbaStatusLabel->setText(
            QString("启动中 | 趋势最大偏差 %1mmHg | 观察 %2ms | 最大允许下降率 %3mmHg/s | 不合格后继续：%4")
                .arg(maxDeviationMmHg, 0, 'f', 2)
                .arg(trendWindowMs)
                .arg(maxDropRateMmHgPerSecond, 0, 'f', 2)
                .arg(m_singleTankPcbaContinueOnFail ? "开启" : "关闭"));
    }
    updateSingleTankPcbaTable();
    if (sendFrame(usb::buildRunSingleTankPcba(nextSequence(),
                                               m_singleTankPcbaContinueOnFail,
                                               maxDeviation001mmHg,
                                               trendWindowMs,
                                               maxDropRate001mmHgPerSecond),
                  QString("RUN_SINGLE_TANK_PCBA | 趋势最大偏差=%1mmHg | 观察=%2ms | 最大允许下降率=%3mmHg/s | 继续=%4")
                      .arg(maxDeviationMmHg, 0, 'f', 2)
                      .arg(trendWindowMs)
                      .arg(maxDropRateMmHgPerSecond, 0, 'f', 2)
                      .arg(m_singleTankPcbaContinueOnFail ? "1" : "0"))) {
        m_singleTankPcbaPollTimer.start();
    } else {
        m_singleTankPcbaStartPending = false;
        m_singleTankPcbaRunning = false;
        setSingleTankPcbaProfileControlsEnabled(true);
        stopSingleTankPcbaLogSession(QStringLiteral("host_send_failed"));
    }
}

void MainWindow::requestSingleTankPcbaReport()
{
    if (!m_transport.isOpen()) {
        m_singleTankPcbaPollTimer.stop();
        m_singleTankPcbaRunning = false;
        setSingleTankPcbaProfileControlsEnabled(true);
        stopSingleTankPcbaLogSession(QStringLiteral("transport_closed"));
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

void MainWindow::openSingleTankLogFolder()
{
    const QString dirPath = defaultSingleTankLogDir();
    QDir().mkpath(dirPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

void MainWindow::openSingleTankPcbaLogFolder()
{
    const QString dirPath = defaultSingleTankPcbaLogDir();
    QDir().mkpath(dirPath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
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

    const IntelHexValidationResult hexValidation = validateApplicationIntelHexFile(hexPath);
    if (!hexValidation.ok) {
        const QString error = QString("HEX安全检查失败，未启动J-Link。\n\n%1").arg(hexValidation.error);
        if (m_firmwareStatusLabel) {
            m_firmwareStatusLabel->setText("拒绝烧录: " + hexValidation.error);
            m_firmwareStatusLabel->setStyleSheet("font-weight: 700; color: #b91c1c;");
        }
        appendFirmwareLog("拒绝烧录: " + hexValidation.error);
        appendLog("固件烧录被HEX安全检查阻止: " + hexValidation.error);
        QMessageBox::critical(this, "HEX安全检查失败", error);
        return;
    }

    const QString message = QString(
        "将使用 J-Link 以 SWD 100kHz 下载到 STM32F103ZE。\n\n"
        "文件:\n%1\n\n"
        "HEX安全检查通过：%2字节，地址%3..%4。\n"
        "文件未包含0x0807F000..0x0807FFFF标定保留区；仅更新HEX涉及的程序扇区，不执行整片擦除。")
                                .arg(QDir::toNativeSeparators(hexPath))
                                .arg(hexValidation.dataBytes)
                                .arg(QString("0x%1").arg(hexValidation.minimumAddress, 8, 16, QLatin1Char('0')).toUpper())
                                .arg(QString("0x%1").arg(hexValidation.maximumAddress, 8, 16, QLatin1Char('0')).toUpper());
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
        m_firmwareStatusLabel->setStyleSheet("color: #0f766e;");
    }
    m_firmwareDownloadSawError = false;
    if (m_firmwareLog) {
        m_firmwareLog->clear();
    }
    appendFirmwareLog(QString("HEX安全检查通过: %1 bytes | 0x%2..0x%3")
                          .arg(hexValidation.dataBytes)
                          .arg(hexValidation.minimumAddress, 8, 16, QLatin1Char('0'))
                          .arg(hexValidation.maximumAddress, 8, 16, QLatin1Char('0'))
                          .toUpper());
    appendFirmwareLog("开始烧录（无mass erase，HEX未覆盖标定A/B页）: " + QDir::toNativeSeparators(hexPath));
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
    selectDebugMode();
    selectSensorCalibrationTool();
    if (m_sensorCalibrationSlotCombo) {
        const int index = m_sensorCalibrationSlotCombo->findData(sensorNumber);
        if (index >= 0) {
            m_sensorCalibrationSlotCombo->setCurrentIndex(index);
        }
    }
    appendLog(QString("打开传感器校准工具 | 固定采样源=压力检测1 | 目标位置=%1").arg(sensorNumber));
    statusBar()->showMessage(
        QString("校准源固定为压力检测1；完成后将写入压力检测%1位置").arg(sensorNumber), 5000);
}

void MainWindow::selectSensorCalibrationTool()
{
    if (!m_flowList) {
        return;
    }
    for (int row = 0; row < m_flowList->count(); ++row) {
        auto *item = m_flowList->item(row);
        if (static_cast<LeftItemKind>(item->data(kLeftKindRole).toInt()) == LeftItemKind::DebugTool &&
            static_cast<DebugTool>(item->data(kLeftValueRole).toInt()) == DebugTool::SensorCalibration) {
            m_flowList->setCurrentRow(row);
            return;
        }
    }
}

void MainWindow::setSensorCalibrationCapability(bool supported)
{
    bool connected = m_transport.isOpen();
#ifdef PRESSURE_FIXTURE_HOST_TEST_ACCESS
    connected = connected || m_sensorCalibrationTestConnected;
#endif
    m_sensorCalibrationCapabilityKnown = connected;
    m_sensorCalibrationSupported = supported;
    if (m_sensorCalibrationCapabilityLabel) {
        if (!connected) {
            m_sensorCalibrationCapabilityLabel->setText("未连接 MCU，校准控制已禁用");
            m_sensorCalibrationCapabilityLabel->setStyleSheet("color: #b45309;");
        } else if (!supported) {
            m_sensorCalibrationCapabilityLabel->setText(
                "当前 MCU HELLO 未声明传感器校准能力（capability bit2），请先更新固件");
            m_sensorCalibrationCapabilityLabel->setStyleSheet("font-weight: 700; color: #b91c1c;");
        } else {
            m_sensorCalibrationCapabilityLabel->setText("MCU 已声明传感器校准能力");
            m_sensorCalibrationCapabilityLabel->setStyleSheet("color: #0f766e;");
        }
    }
    updateSensorCalibrationPanel();
}

bool MainWindow::sendSensorCalibrationAction(const QByteArray &frame, const QString &description)
{
    if (!m_sensorCalibrationSupported) {
        statusBar()->showMessage("当前 MCU 固件不支持传感器校准", 4000);
        updateSensorCalibrationPanel();
        return false;
    }
    return sendFrame(frame, description);
}

void MainWindow::enterSensorCalibration()
{
    stopSensorCalibrationJog();
    const int slot = m_sensorCalibrationSlotCombo
        ? m_sensorCalibrationSlotCombo->currentData().toInt()
        : 0;
    const bool inPlaceMode = m_sensorCalibrationModeCombo &&
        m_sensorCalibrationModeCombo->currentData().toInt() == 1;
    if (slot < 1 || slot > kPressureSensorCount) {
        return;
    }
    if (sendSensorCalibrationAction(
            usb::buildSensorCalibrationEnter(nextSequence(), inPlaceMode, static_cast<uint8_t>(slot)),
            QString("SENSOR_CALIBRATION ENTER mode=%1 slot=%2")
                .arg(inPlaceMode ? "in-place" : "fixed-IIC1")
                .arg(slot))) {
        for (int point = 1; point < 4; ++point) {
            if (auto *spin = m_sensorCalibrationActualSpins[static_cast<size_t>(point)]) {
                const QSignalBlocker blocker(spin);
                spin->setValue(kSensorCalibrationDefaultActualMmHg[static_cast<size_t>(point)]);
                spin->setProperty(kSensorCalibrationCapturedRawProperty, QVariant());
            }
        }
        m_sensorCalibrationStateLabel->setText("进入命令已发送，等待 MCU 确认并全关其他阀门");
        m_sensorCalibrationPollTimer.start();
    }
}

void MainWindow::exitSensorCalibration()
{
    stopSensorCalibrationJog();
    if (sendSensorCalibrationAction(
            usb::buildSensorCalibrationSimpleAction(nextSequence(), usb::SensorCalibrationExit),
            "SENSOR_CALIBRATION EXIT 并关阀")) {
        m_sensorCalibrationStateLabel->setText("退出命令已发送，等待 MCU 确认阀门关闭");
    }
}

void MainWindow::startSensorCalibrationFill()
{
    if (!m_sensorCalibrationSupported || !m_sensorCalibrationStatus.active ||
        !m_sensorCalibrationStatus.sourceValid || m_sensorCalibrationStatus.sourceFault ||
        m_sensorCalibrationStatus.autoVentActive) {
        return;
    }
    m_sensorCalibrationJogActuator = usb::SensorCalibrationFill;
    if (sendSensorCalibrationAction(
            usb::buildSensorCalibrationJog(nextSequence(), m_sensorCalibrationJogActuator, 500),
            "SENSOR_CALIBRATION JOG 充气V1 租约500ms")) {
        m_sensorCalibrationJogTimer.start();
        updateSensorCalibrationPanel();
    } else {
        m_sensorCalibrationJogActuator = usb::SensorCalibrationStop;
    }
}

void MainWindow::startSensorCalibrationRelease()
{
    if (!m_sensorCalibrationSupported || !m_sensorCalibrationStatus.active ||
        !m_sensorCalibrationStatus.sourceValid || m_sensorCalibrationStatus.sourceFault ||
        m_sensorCalibrationStatus.autoVentActive) {
        return;
    }
    m_sensorCalibrationJogActuator = usb::SensorCalibrationRelease;
    if (sendSensorCalibrationAction(
            usb::buildSensorCalibrationJog(nextSequence(), m_sensorCalibrationJogActuator, 500),
            "SENSOR_CALIBRATION JOG 微量放气V21 租约500ms")) {
        m_sensorCalibrationJogTimer.start();
        updateSensorCalibrationPanel();
    } else {
        m_sensorCalibrationJogActuator = usb::SensorCalibrationStop;
    }
}

void MainWindow::serviceSensorCalibrationJog()
{
    if (m_sensorCalibrationJogActuator == usb::SensorCalibrationStop ||
        !m_sensorCalibrationSupported || !m_transport.isOpen()) {
        m_sensorCalibrationJogTimer.stop();
        return;
    }
    if (!sendSensorCalibrationAction(
            usb::buildSensorCalibrationJog(nextSequence(), m_sensorCalibrationJogActuator, 500),
            QString("SENSOR_CALIBRATION JOG续租 actuator=%1 500ms")
                .arg(m_sensorCalibrationJogActuator))) {
        m_sensorCalibrationJogTimer.stop();
        m_sensorCalibrationJogActuator = usb::SensorCalibrationStop;
        updateSensorCalibrationPanel();
    }
}

void MainWindow::stopSensorCalibrationJog()
{
    m_sensorCalibrationJogTimer.stop();
    if (m_sensorCalibrationJogActuator == usb::SensorCalibrationStop) {
        return;
    }
    m_sensorCalibrationJogActuator = usb::SensorCalibrationStop;
    if (m_sensorCalibrationSupported && m_transport.isOpen()) {
        sendSensorCalibrationAction(
            usb::buildSensorCalibrationJog(nextSequence(), usb::SensorCalibrationStop, 0),
            "SENSOR_CALIBRATION JOG STOP");
    }
    updateSensorCalibrationPanel();
}

void MainWindow::startSensorCalibrationVent()
{
    stopSensorCalibrationJog();
    sendSensorCalibrationAction(
        usb::buildSensorCalibrationSimpleAction(nextSequence(), usb::SensorCalibrationStartAutoVent),
        "SENSOR_CALIBRATION START_AUTO_VENT");
}

void MainWindow::cancelSensorCalibrationVent()
{
    sendSensorCalibrationAction(
        usb::buildSensorCalibrationSimpleAction(nextSequence(), usb::SensorCalibrationCancelAutoVent),
        "SENSOR_CALIBRATION CANCEL_AUTO_VENT");
}

void MainWindow::recordSensorCalibrationPoint(int point)
{
    if (point < 0 || point >= 4 || !m_sensorCalibrationActualSpins[static_cast<size_t>(point)]) {
        return;
    }
    const double actualMmHg = point == 0
        ? 0.0
        : m_sensorCalibrationActualSpins[static_cast<size_t>(point)]->value();
    if (point != 0) {
        if (actualMmHg <= 0.0) {
            QMessageBox::warning(this, "请输入实测值", "请填写外部标定计当前显示的气压值。");
            return;
        }
        for (int other = 1; other < 4; ++other) {
            if (other == point ||
                (m_sensorCalibrationStatus.capturedMask & (1u << other)) == 0u) {
                continue;
            }
            const double recordedMmHg = toMmHg(static_cast<int>(
                m_sensorCalibrationStatus.actual001mmHg[static_cast<size_t>(other)]));
            if ((other < point && recordedMmHg >= actualMmHg) ||
                (other > point && recordedMmHg <= actualMmHg)) {
                QMessageBox::warning(this, "实测值顺序错误",
                                     "外部标定计值必须与已记录点保持严格递增。");
                return;
            }
        }
    }
    const uint32_t actual001 = point == 0
        ? 0u
        : static_cast<uint32_t>(to001mmHg(actualMmHg));
    const bool rerecord = (m_sensorCalibrationStatus.capturedMask & (1u << point)) != 0u;
    m_sensorCalibrationPendingRecordPoint = point;
    m_sensorCalibrationPendingRecordActual001mmHg = actual001;
    m_sensorCalibrationPendingRecordAcknowledged = false;
    if (sendSensorCalibrationAction(
        usb::buildSensorCalibrationRecord(nextSequence(), static_cast<uint8_t>(point), actual001),
        QString("SENSOR_CALIBRATION RECORD point=%1 actual=%2")
            .arg(point)
            .arg(actual001))) {
        updateSensorCalibrationPanel();
        const QString pointText = point == 0 ? QStringLiteral("零点") : QString("标定点%1").arg(point);
        statusBar()->showMessage(
            QString("%1%2命令已发送，等待 MCU 确认")
                .arg(pointText, rerecord ? QStringLiteral("重新记录") : QStringLiteral("记录")),
            5000);
    } else {
        m_sensorCalibrationPendingRecordPoint = -1;
        m_sensorCalibrationPendingRecordActual001mmHg = 0;
        m_sensorCalibrationPendingRecordAcknowledged = false;
    }
}

void MainWindow::saveSensorCalibration()
{
    if (!m_sensorCalibrationSlotCombo) {
        return;
    }
    const int slot = m_sensorCalibrationSlotCombo->currentData().toInt();
    if (slot < 1 || slot > kPressureSensorCount) {
        return;
    }
    if ((m_sensorCalibrationStatus.calibratedMask & (1u << (slot - 1))) != 0u) {
        const auto answer = QMessageBox::question(
            this, "覆盖已有标定",
            QString("压力检测%1 已校准。确定用本次四点数据覆盖吗？").arg(slot));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    sendSensorCalibrationAction(
        usb::buildSensorCalibrationSlotAction(nextSequence(), usb::SensorCalibrationSaveSlot,
                                              static_cast<uint8_t>(slot)),
        QString("SENSOR_CALIBRATION SAVE_SLOT %1").arg(slot));
}

void MainWindow::resetSensorCalibrationSession()
{
    stopSensorCalibrationJog();
    if (sendSensorCalibrationAction(
            usb::buildSensorCalibrationSimpleAction(nextSequence(), usb::SensorCalibrationResetSession),
            "SENSOR_CALIBRATION RESET_SESSION")) {
        for (int point = 1; point < 4; ++point) {
            if (auto *spin = m_sensorCalibrationActualSpins[static_cast<size_t>(point)]) {
                const QSignalBlocker blocker(spin);
                spin->setValue(kSensorCalibrationDefaultActualMmHg[static_cast<size_t>(point)]);
                spin->setProperty(kSensorCalibrationCapturedRawProperty, QVariant());
            }
        }
        updateSensorCalibrationPanel();
    }
}

void MainWindow::clearSensorCalibrationSlot()
{
    if (!m_sensorCalibrationSlotCombo) {
        return;
    }
    const int slot = m_sensorCalibrationSlotCombo->currentData().toInt();
    if (slot < 1 || slot > kPressureSensorCount) {
        return;
    }
    const auto answer = QMessageBox::warning(
        this, "清除标定数据",
        QString("确定清除压力检测%1的持久化标定数据？该位置随后会显示未校准。").arg(slot),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    sendSensorCalibrationAction(
        usb::buildSensorCalibrationSlotAction(nextSequence(), usb::SensorCalibrationClearSlot,
                                              static_cast<uint8_t>(slot)),
        QString("SENSOR_CALIBRATION CLEAR_SLOT %1").arg(slot));
}

void MainWindow::handleSensorCalibrationSlotChanged(int index)
{
    Q_UNUSED(index)
    updateSensorCalibrationPanel();
    if (m_sensorCalibrationSupported && m_transport.isOpen() &&
        m_sensorCalibrationStatus.capturedMask == 0u && m_sensorCalibrationSlotCombo) {
        const int slot = m_sensorCalibrationSlotCombo->currentData().toInt();
        sendFrame(usb::buildGetSensorCalibrationStatus(nextSequence(), static_cast<uint8_t>(slot)),
                  QString("GET_SENSOR_CALIBRATION_STATUS slot=%1").arg(slot));
    }
}

void MainWindow::requestSensorCalibrationStatus()
{
    if (!m_sensorCalibrationSupported || !m_transport.isOpen()) {
        return;
    }
    sendFrame(usb::buildGetSensorCalibrationStatus(nextSequence()),
              "GET_SENSOR_CALIBRATION_STATUS staged");
}

void MainWindow::updateSensorCalibrationPanel()
{
    if (!m_debugSensorCalibrationBox) {
        return;
    }
    bool connected = m_transport.isOpen();
#ifdef PRESSURE_FIXTURE_HOST_TEST_ACCESS
    connected = connected || m_sensorCalibrationTestConnected;
#endif
    const bool supported = connected && m_sensorCalibrationSupported;
    const bool active = supported && m_sensorCalibrationStatus.active;
    const bool sourceReady = active && m_sensorCalibrationStatus.sourceValid &&
                             !m_sensorCalibrationStatus.sourceFault;
    const bool venting = active && m_sensorCalibrationStatus.autoVentActive;
    const bool jogging = m_sensorCalibrationJogActuator != usb::SensorCalibrationStop;
    if (active && m_sensorCalibrationModeCombo) {
        const QSignalBlocker blocker(m_sensorCalibrationModeCombo);
        const int modeIndex = m_sensorCalibrationModeCombo->findData(
            m_sensorCalibrationStatus.inPlaceMode ? 1 : 0);
        if (modeIndex >= 0) {
            m_sensorCalibrationModeCombo->setCurrentIndex(modeIndex);
        }
    }
    if (active && m_sensorCalibrationSlotCombo && m_sensorCalibrationStatus.selectedSlot >= 1 &&
        m_sensorCalibrationStatus.selectedSlot <= kPressureSensorCount) {
        const QSignalBlocker blocker(m_sensorCalibrationSlotCombo);
        const int slotIndex = m_sensorCalibrationSlotCombo->findData(m_sensorCalibrationStatus.selectedSlot);
        if (slotIndex >= 0) {
            m_sensorCalibrationSlotCombo->setCurrentIndex(slotIndex);
        }
    }
    const int slot = m_sensorCalibrationSlotCombo
        ? m_sensorCalibrationSlotCombo->currentData().toInt()
        : 0;
    const bool selectedInPlaceMode = m_sensorCalibrationModeCombo &&
        m_sensorCalibrationModeCombo->currentData().toInt() == 1;
    const bool inPlaceMode = active ? m_sensorCalibrationStatus.inPlaceMode : selectedInPlaceMode;
    const int resourceSlot = active && m_sensorCalibrationStatus.selectedSlot >= 1 &&
                             m_sensorCalibrationStatus.selectedSlot <= kPressureSensorCount
        ? m_sensorCalibrationStatus.selectedSlot
        : slot;
    const int sourceSensor = inPlaceMode ? resourceSlot : 1;
    const int tankNumber = sourceSensor <= 6 ? sourceSensor : 1;
    const int inletValve = tankNumber * 2 - 1;
    const int reliefValve = 20 + tankNumber;
    const int routeValve = sourceSensor >= 7 ? 13 + (sourceSensor - 7) : 0;
    const bool slotCalibrated = slot >= 1 && slot <= kPressureSensorCount &&
        (m_sensorCalibrationStatus.calibratedMask & (1u << (slot - 1))) != 0u;

    if (m_sensorCalibrationSlotLabel) {
        m_sensorCalibrationSlotLabel->setText(slotCalibrated ? "已校准" : "未校准");
        m_sensorCalibrationSlotLabel->setStyleSheet(slotCalibrated
            ? "font-weight: 700; color: #15803d;"
            : "font-weight: 700; color: #b45309;");
    }
    if (m_sensorCalibrationLiveLabel) {
        const QString livePressure = m_sensorCalibrationStatus.sourceValid
            ? formatPressure001mmHg(static_cast<int>(m_sensorCalibrationStatus.liveNominal001mmHg),
                                    true, 3, true)
            : QStringLiteral("--");
        QString resourceText;
        if (!inPlaceMode) {
            resourceText = QString("借用IIC1工位 | 源=压力检测1/IIC1 | 罐1 | 进气V1 | 泄压V21 | 保存目标=压力检测%1")
                .arg(resourceSlot);
        } else if (sourceSensor <= 6) {
            resourceText = QString("原位 | 源/目标=压力检测%1/IIC%1 | 罐%1 | 进气V%2 | 泄压V%3")
                .arg(sourceSensor)
                .arg(inletValve)
                .arg(reliefValve);
        } else {
            resourceText = QString("原位 | 源/目标=压力检测%1/IIC%1 | 罐1 | 通路V2+V%2 | 进气V1 | 泄压V21")
                .arg(sourceSensor)
                .arg(routeValve);
        }
        m_sensorCalibrationLiveLabel->setText(
            QString("%1\n实时原始计数 %2 | 原始换算 %3 | 进气阀V%4 %5 | 泄压阀V%6 %7")
                .arg(resourceText)
                .arg(m_sensorCalibrationStatus.sourceValid
                         ? QString::number(m_sensorCalibrationStatus.liveRaw)
                         : QStringLiteral("--"))
                .arg(livePressure)
                .arg(inletValve)
                .arg(m_sensorCalibrationStatus.actuator == usb::SensorCalibrationFill ? "打开" : "关闭")
                .arg(reliefValve)
                .arg(m_sensorCalibrationStatus.actuator == usb::SensorCalibrationRelease ? "打开" : "关闭"));
    }
    if (m_sensorCalibrationStateLabel) {
        QString stateText;
        if (!connected) {
            stateText = "等待 MCU 连接";
        } else if (!supported) {
            stateText = "校准不可用：需要支持 capability bit2 的 MCU 固件";
        } else if (m_sensorCalibrationStatus.storageFault) {
            stateText = QString("标定存储故障 | detail=%1").arg(m_sensorCalibrationStatus.detail);
        } else if (!active) {
            stateText = "校准模式未进入，所有校准阀门应保持关闭";
        } else if (venting) {
            stateText = "正在自动 VENT；达到零压稳定条件或超时后 MCU 会自动关闭 V21";
        } else if (jogging || m_sensorCalibrationStatus.actuator != usb::SensorCalibrationStop) {
            stateText = m_sensorCalibrationStatus.actuator == usb::SensorCalibrationFill
                ? "点动充气中：V1租约由Qt每150ms续期"
                : "点动放气中：V21租约由Qt每150ms续期";
        } else if (!m_sensorCalibrationStatus.sourceValid || m_sensorCalibrationStatus.sourceFault) {
            stateText = QString("压力检测1不可用或故障 | detail=%1").arg(m_sensorCalibrationStatus.detail);
        } else {
            int capturedCount = 0;
            for (int point = 0; point < 4; ++point) {
                if ((m_sensorCalibrationStatus.capturedMask & (1u << point)) != 0u) {
                    ++capturedCount;
                }
            }
            stateText = QString("校准模式已进入 | 已记录 %1/4 点 | 零点%2")
                .arg(capturedCount)
                .arg(m_sensorCalibrationStatus.zeroReady ? "已就绪" : "未就绪");
        }
        if (m_sensorCalibrationPendingRecordPoint >= 0) {
            const int point = m_sensorCalibrationPendingRecordPoint;
            const bool rerecord = (m_sensorCalibrationStatus.capturedMask & (1u << point)) != 0u;
            const QString pointText = point == 0 ? QStringLiteral("零点") : QString("标定点%1").arg(point);
            stateText += QString(" | %1%2%3")
                .arg(pointText,
                     rerecord ? QStringLiteral("重新记录") : QStringLiteral("记录"),
                     m_sensorCalibrationPendingRecordAcknowledged
                         ? QStringLiteral("已确认，等待状态回传")
                         : QStringLiteral("命令已发送，等待MCU确认"));
        } else {
            const QString detailText = sensorCalibrationDetailText(m_sensorCalibrationStatus.detail);
            if (!detailText.isEmpty()) {
                stateText += QString(" | 最近结果：%1").arg(detailText);
            }
        }
        m_sensorCalibrationStateLabel->setText(stateText);
        m_sensorCalibrationStateLabel->setStyleSheet(m_sensorCalibrationStatus.storageFault ||
                                                      m_sensorCalibrationStatus.sourceFault ||
                                                      sensorCalibrationDetailIsError(m_sensorCalibrationStatus.detail)
            ? "font-weight: 700; color: #b91c1c;"
            : "color: #475569;");
    }

    if (m_sensorCalibrationSlotCombo) {
        m_sensorCalibrationSlotCombo->setEnabled(supported && !active && !jogging && !venting);
    }
    if (m_sensorCalibrationModeCombo) {
        m_sensorCalibrationModeCombo->setEnabled(supported && !active && !jogging && !venting);
    }
    if (m_sensorCalibrationSlotTitleLabel) {
        m_sensorCalibrationSlotTitleLabel->setText(inPlaceMode
            ? "原位传感器 / IIC"
            : "保存到传感器位置");
    }
    if (m_sensorCalibrationEnterButton) {
        m_sensorCalibrationEnterButton->setEnabled(supported && !active);
    }
    if (m_sensorCalibrationExitButton) {
        m_sensorCalibrationExitButton->setEnabled(supported && active);
    }
    if (m_sensorCalibrationFillButton) {
        m_sensorCalibrationFillButton->setEnabled(sourceReady && !venting);
        m_sensorCalibrationFillButton->setText(
            m_sensorCalibrationJogActuator == usb::SensorCalibrationFill
                ? QString("V%1充气中（松开停止）").arg(inletValve)
                : QString("按住充气（V%1）").arg(inletValve));
    }
    if (m_sensorCalibrationReleaseButton) {
        m_sensorCalibrationReleaseButton->setEnabled(sourceReady && !venting);
        m_sensorCalibrationReleaseButton->setText(
            m_sensorCalibrationJogActuator == usb::SensorCalibrationRelease
                ? QString("V%1放气中（松开停止）").arg(reliefValve)
                : QString("按住微量放气（V%1）").arg(reliefValve));
    }
    if (m_sensorCalibrationVentButton) {
        m_sensorCalibrationVentButton->setEnabled(sourceReady && !venting && !jogging);
    }
    if (m_sensorCalibrationCancelVentButton) {
        m_sensorCalibrationCancelVentButton->setEnabled(supported && active && venting);
    }

    if (m_sensorCalibrationPointTable) {
        for (int point = 0; point < 4; ++point) {
            const bool captured = (m_sensorCalibrationStatus.capturedMask & (1u << point)) != 0u;
            auto *rawItem = m_sensorCalibrationPointTable->item(point, 2);
            auto *stateItem = m_sensorCalibrationPointTable->item(point, 3);
            if (rawItem) {
                rawItem->setText(captured
                    ? QString::number(m_sensorCalibrationStatus.rawPoints[static_cast<size_t>(point)])
                    : QStringLiteral("--"));
            }
            if (stateItem) {
                stateItem->setText(captured ? "已记录" : "待记录");
                stateItem->setForeground(QBrush(captured ? QColor("#15803d") : QColor("#b45309")));
            }
            auto *spin = m_sensorCalibrationActualSpins[static_cast<size_t>(point)];
            if (spin) {
                if (!captured) {
                    spin->setProperty(kSensorCalibrationCapturedRawProperty, QVariant());
                } else {
                    const uint32_t capturedRaw =
                        m_sensorCalibrationStatus.rawPoints[static_cast<size_t>(point)];
                    const QVariant syncedRaw = spin->property(kSensorCalibrationCapturedRawProperty);
                    if (!syncedRaw.isValid() || syncedRaw.toUInt() != capturedRaw) {
                        const QSignalBlocker blocker(spin);
                        spin->setValue(toMmHg(static_cast<int>(
                            m_sensorCalibrationStatus.actual001mmHg[static_cast<size_t>(point)])));
                        spin->setProperty(kSensorCalibrationCapturedRawProperty, capturedRaw);
                    }
                }
                spin->setEnabled(point != 0 && supported && active);
            }
            auto *recordButton = m_sensorCalibrationRecordButtons[static_cast<size_t>(point)];
            if (recordButton) {
                const bool pointReady = point == 0 || (spin && spin->value() > 0.0);
                const bool recordEnabled = sourceReady && !venting && !jogging && pointReady &&
                    (point != 0 || m_sensorCalibrationStatus.zeroReady);
                recordButton->setEnabled(recordEnabled);
                recordButton->setText(captured ? "重新记录" : (point == 0 ? "记录零点" : "记录当前点"));
            }
        }
    }
    if (m_sensorCalibrationSaveButton) {
        m_sensorCalibrationSaveButton->setEnabled(active && m_sensorCalibrationStatus.stagedComplete &&
                                                  !venting && !jogging);
        m_sensorCalibrationSaveButton->setText(slot >= 1
            ? QString("写入压力检测%1位置").arg(slot)
            : QStringLiteral("写入所选传感器位置"));
    }
    if (m_sensorCalibrationResetButton) {
        m_sensorCalibrationResetButton->setEnabled(active && !venting && !jogging);
    }
    if (m_sensorCalibrationClearSlotButton) {
        m_sensorCalibrationClearSlotButton->setEnabled(active && slot >= 1 && !venting && !jogging);
    }
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
    const QDateTime now = QDateTime::currentDateTime();
    if (m_log) {
        m_log->appendPlainText(now.toString("HH:mm:ss.zzz ") + line);
    }
    if (m_singleTankPcbaLogActive && m_singleTankPcbaSessionLogFile.isOpen()) {
        m_singleTankPcbaSessionLogFile.write(
            (now.toString(Qt::ISODateWithMs) + " " + line + "\n").toUtf8());
        m_singleTankPcbaSessionLogFile.flush();
    }
}

QString MainWindow::projectRootPath() const
{
    const QStringList candidates{
        QDir::currentPath(),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../../"),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(QDir(candidate).absoluteFilePath("Firmware/STM32F103ZET6_MDK_HAL"))) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return QFileInfo(candidates.first()).absoluteFilePath();
}

QString MainWindow::defaultSingleTankLogDir() const
{
    return QDir(projectRootPath()).absoluteFilePath("runtime-logs/single-tank-loop");
}

QString MainWindow::defaultSingleTankPcbaLogDir() const
{
    const QString overrideRoot = qEnvironmentVariable("PRESSURE_FIXTURE_LOG_ROOT").trimmed();
    const QString logRoot = overrideRoot.isEmpty()
        ? QDir(projectRootPath()).absoluteFilePath("runtime-logs")
        : QFileInfo(overrideRoot).absoluteFilePath();
    return QDir(logRoot).absoluteFilePath("single-tank-pcba");
}

void MainWindow::updateSingleTankPcbaLogUi()
{
    if (!m_singleTankPcbaLogStatusLabel) {
        return;
    }
    if (m_singleTankPcbaLogActive) {
        m_singleTankPcbaLogStatusLabel->setText(
            QString("LOG记录中 | %1")
                .arg(QDir::toNativeSeparators(m_singleTankPcbaSessionDirPath)));
        m_singleTankPcbaLogStatusLabel->setStyleSheet("font-weight: 700; color: #166534;");
    } else if (!m_singleTankPcbaSessionDirPath.isEmpty()) {
        m_singleTankPcbaLogStatusLabel->setText(
            QString("上次LOG | %1")
                .arg(QDir::toNativeSeparators(m_singleTankPcbaSessionDirPath)));
        m_singleTankPcbaLogStatusLabel->setStyleSheet("color: #475569;");
    } else {
        m_singleTankPcbaLogStatusLabel->setText(
            QString("每次测试自动记录LOG | %1")
                .arg(QDir::toNativeSeparators(defaultSingleTankPcbaLogDir())));
        m_singleTankPcbaLogStatusLabel->setStyleSheet("color: #475569;");
    }
}

bool MainWindow::startSingleTankPcbaLogSession()
{
    if (m_singleTankPcbaLogActive) {
        stopSingleTankPcbaLogSession(QStringLiteral("new_session"));
    }

    m_singleTankPcbaLogDirPath = defaultSingleTankPcbaLogDir();
    const QString sessionName = QString("single-tank-pcba_%1")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    m_singleTankPcbaSessionDirPath = QDir(m_singleTankPcbaLogDirPath).absoluteFilePath(sessionName);
    if (!QDir().mkpath(m_singleTankPcbaSessionDirPath)) {
        m_singleTankPcbaSessionDirPath.clear();
        updateSingleTankPcbaLogUi();
        appendLog("单罐单PCBA LOG目录创建失败");
        return false;
    }

    m_singleTankPcbaSessionLogFile.setFileName(
        QDir(m_singleTankPcbaSessionDirPath).absoluteFilePath("session.log"));
    m_singleTankPcbaSnapshotLogFile.setFileName(
        QDir(m_singleTankPcbaSessionDirPath).absoluteFilePath("snapshots.csv"));
    m_singleTankPcbaStepLogFile.setFileName(
        QDir(m_singleTankPcbaSessionDirPath).absoluteFilePath("steps.csv"));

    const auto openLogFile = [](QFile &file) {
        return file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    };
    if (!openLogFile(m_singleTankPcbaSessionLogFile) ||
        !openLogFile(m_singleTankPcbaSnapshotLogFile) ||
        !openLogFile(m_singleTankPcbaStepLogFile)) {
        m_singleTankPcbaSessionLogFile.close();
        m_singleTankPcbaSnapshotLogFile.close();
        m_singleTankPcbaStepLogFile.close();
        updateSingleTankPcbaLogUi();
        appendLog(QString("单罐单PCBA LOG文件打开失败: %1")
                      .arg(QDir::toNativeSeparators(m_singleTankPcbaSessionDirPath)));
        return false;
    }

    m_singleTankPcbaLogActive = true;
    m_singleTankPcbaLoggedStepCount = 0u;
    writeSingleTankPcbaSnapshotHeader();
    writeSingleTankPcbaStepHeader();
    updateSingleTankPcbaLogUi();
    appendLog(QString("单罐单PCBA LOG已开始: %1")
                  .arg(QDir::toNativeSeparators(m_singleTankPcbaSessionDirPath)));
    recordSingleTankPcbaSnapshot(m_snapshot, QStringLiteral("session_start"));
    return true;
}

void MainWindow::stopSingleTankPcbaLogSession(const QString &reason)
{
    if (m_singleTankPcbaLogActive) {
        recordSingleTankPcbaSnapshot(m_snapshot, QStringLiteral("session_stop:%1").arg(reason));
        appendLog(QString("单罐单PCBA LOG结束: %1").arg(reason));
    }
    m_singleTankPcbaSessionLogFile.close();
    m_singleTankPcbaSnapshotLogFile.close();
    m_singleTankPcbaStepLogFile.close();
    m_singleTankPcbaLogActive = false;
    m_singleTankPcbaLoggedStepCount = 0u;
    updateSingleTankPcbaLogUi();
}

void MainWindow::writeSingleTankPcbaSnapshotHeader()
{
    if (!m_singleTankPcbaSnapshotLogFile.isOpen()) {
        return;
    }
    QStringList columns{
        "host_time", "snapshot_seq", "runtime_state", "runtime_state_text",
        "elapsed_ms", "workflow_running", "workflow_paused", "report_count",
        "valve_mask_hex", "event"
    };
    for (int valve = 1; valve <= kValveCount; ++valve) {
        columns << QString("valve%1_open").arg(valve);
    }
    for (int sensor = 0; sensor < kPressureSensorCount; ++sensor) {
        columns << QString("pressure%1_mmhg").arg(sensor + 1)
                << QString("pressure%1_valid").arg(sensor + 1)
                << QString("pressure%1_fault").arg(sensor + 1)
                << QString("pressure%1_status_byte").arg(sensor + 1)
                << QString("pressure%1_fault_code").arg(sensor + 1);
    }
    m_singleTankPcbaSnapshotLogFile.write((columns.join(',') + "\n").toUtf8());
    m_singleTankPcbaSnapshotLogFile.flush();
}

void MainWindow::writeSingleTankPcbaStepHeader()
{
    if (!m_singleTankPcbaStepLogFile.isOpen()) {
        return;
    }
    const QStringList columns{
        "host_time", "step_no", "step_name", "kind", "command_or_failure_code",
        "ok", "flags", "action", "tx", "rx", "response_command", "response_channel",
        "response_length", "response_data_hex", "raw_response_hex", "elapsed_us",
        "current_ua_x100", "comparison_pressure_001mmhg", "parsed_value", "trend_sample_count",
        "trend_slope_001mmhg_per_s", "trend_max_residual_001mmhg",
        "trend_observation_us", "trend_predicted_pressure_001mmhg", "parsed_text", "reason"
    };
    m_singleTankPcbaStepLogFile.write((columns.join(',') + "\n").toUtf8());
    m_singleTankPcbaStepLogFile.flush();
}

void MainWindow::recordSingleTankPcbaSnapshot(const FixtureSnapshot &previous,
                                              const QString &extraEvent)
{
    if (!m_singleTankPcbaLogActive || !m_singleTankPcbaSnapshotLogFile.isOpen()) {
        return;
    }
    QStringList events;
    if (!extraEvent.isEmpty()) {
        events << extraEvent;
    }
    if (previous.sequence != 0u && previous.state != m_snapshot.state) {
        events << QString("state:%1->%2")
                      .arg(stateDisplayName(previous.state), stateDisplayName(m_snapshot.state));
    }
    if (previous.sequence != 0u && valveMaskText(previous) != valveMaskText(m_snapshot)) {
        events << QString("valves:%1->%2")
                      .arg(valveMaskText(previous), valveMaskText(m_snapshot));
    }

    QStringList fields{
        csvField(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)),
        QString::number(m_snapshot.sequence),
        QString::number(stateIndex(m_snapshot.state)),
        csvField(stateDisplayName(m_snapshot.state)),
        QString::number(m_snapshot.elapsedMs),
        boolText(m_snapshot.running),
        boolText(m_snapshot.paused),
        QString::number(m_singleTankPcbaReport.count),
        csvField(valveMaskText(m_snapshot)),
        csvField(events.join(" | "))
    };
    for (int valve = 1; valve <= kValveCount; ++valve) {
        fields << boolText(m_snapshot.valvesOpen[valve]);
    }
    for (int sensor = 0; sensor < kPressureSensorCount; ++sensor) {
        fields << QString::number(toMmHg(m_snapshot.pressure001mmHg[sensor]), 'f', 3)
               << boolText(m_snapshot.pressureValid[sensor])
               << boolText(m_snapshot.pressureFaultLatched[sensor])
               << csvField(hexByteText(m_snapshot.pressureStatusByte[sensor]))
               << QString::number(m_snapshot.pressureFaultCode[sensor]);
    }
    m_singleTankPcbaSnapshotLogFile.write((fields.join(',') + "\n").toUtf8());
    m_singleTankPcbaSnapshotLogFile.flush();
}

void MainWindow::recordSingleTankPcbaReport(const SingleTankPcbaReport &report)
{
    if (!m_singleTankPcbaLogActive || !m_singleTankPcbaStepLogFile.isOpen()) {
        return;
    }
    if (report.count < m_singleTankPcbaLoggedStepCount) {
        m_singleTankPcbaLoggedStepCount = 0u;
    }
    for (uint8_t step = m_singleTankPcbaLoggedStepCount;
         step < report.count && step < report.entries.size();
         ++step) {
        const SingleTankPcbaEntry &entry = report.entries[step];
        const SingleTankPcbaEntry *trendSourceEntry =
            singleTankPcbaTrendSource(report, step);
        const QString responseData = responseDataText(entry.responseData, entry.responseLength);
        const bool isTrendEntry = entry.kind == 4u;
        const QStringList fields{
            csvField(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)),
            QString::number(step + 1u),
            csvField(singleTankPcbaStepText(step)),
            QString::number(entry.kind),
            csvField(hexByteText(entry.command)),
            boolText(entry.ok),
            csvField(hexByteText(entry.flags)),
            csvField(singleTankPcbaActionText(step, entry.flags)),
            csvField(singleTankPcbaTxTextForEntry(step, entry)),
            csvField(singleTankPcbaRxText(entry)),
            isTrendEntry ? QString() : csvField(hexByteText(entry.responseCommandOrByte)),
            isTrendEntry ? QString() : QString::number(entry.responseChannel),
            isTrendEntry ? QString() : QString::number(entry.responseLength),
            isTrendEntry ? QString() : csvField(responseData),
            csvField(rawBytesToHexText(entry.rawResponse, entry.rawResponseLength)),
            QString::number(entry.elapsedUs),
            entry.kind == 2u ? QString::number(entry.currentUaX100) : QString(),
            entry.command == 0x11u && entry.comparisonPressure001mmHg != 0u
                ? QString::number(entry.comparisonPressure001mmHg)
                : QString(),
            QString::number(entry.parsedValue),
            isTrendEntry ? QString::number(entry.trendSampleCount) : QString(),
            isTrendEntry ? QString::number(entry.trendSlope001mmHgPerSecond) : QString(),
            isTrendEntry ? QString::number(entry.trendMaxResidual001mmHg) : QString(),
            isTrendEntry ? QString::number(entry.trendObservationUs) : QString(),
            isTrendEntry ? QString::number(entry.trendPredictedPressure001mmHg) : QString(),
            csvField(singleTankPcbaParsedText(step, entry, trendSourceEntry)),
            csvField(singleTankPcbaReasonText(entry, trendSourceEntry))
        };
        m_singleTankPcbaStepLogFile.write((fields.join(',') + "\n").toUtf8());
    }
    m_singleTankPcbaStepLogFile.flush();
    m_singleTankPcbaLoggedStepCount = report.count;
}

void MainWindow::updateSingleTankLogUi()
{
    if (!m_singleTankLogStatusLabel) {
        return;
    }
    if (m_singleTankLogCheck) {
        m_singleTankLogCheck->setEnabled(!m_singleTankRunning);
    }

    const QString logDir = QDir::toNativeSeparators(defaultSingleTankLogDir());
    if (m_singleTankLogActive) {
        m_singleTankLogStatusLabel->setText(
            QString("日志已记录中 | 文件: %1")
                .arg(QDir::toNativeSeparators(m_singleTankLogFilePath)));
    } else if (!m_singleTankLogFilePath.isEmpty()) {
        m_singleTankLogStatusLabel->setText(
            QString("上次日志文件: %1")
                .arg(QDir::toNativeSeparators(m_singleTankLogFilePath)));
    } else if (m_singleTankLogCheck && m_singleTankLogCheck->isChecked()) {
        m_singleTankLogStatusLabel->setText(QString("日志待命 | 启动单罐闭环后会自动写入: %1").arg(logDir));
    } else {
        m_singleTankLogStatusLabel->setText(QString("日志关闭 | 如需长时间排查，建议开启自动记录。目录: %1").arg(logDir));
    }
}

bool MainWindow::startSingleTankLogSession()
{
    if (!m_singleTankLogCheck || !m_singleTankLogCheck->isChecked()) {
        m_singleTankLogActive = false;
        m_singleTankLogLastWriteMs = 0;
        updateSingleTankLogUi();
        return false;
    }

    if (m_singleTankLogFile.isOpen()) {
        m_singleTankLogFile.close();
    }

    m_singleTankLogDirPath = defaultSingleTankLogDir();
    QDir().mkpath(m_singleTankLogDirPath);

    const int tankIndex = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
    const QString tankToken = tankIndex >= 0 ? QString("tank%1").arg(tankIndex + 1) : QString("tankX");
    const QString fileName = QString("single-tank-loop_%1_%2.csv")
                                 .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"))
                                 .arg(tankToken);
    m_singleTankLogFilePath = QDir(m_singleTankLogDirPath).absoluteFilePath(fileName);
    m_singleTankLogFile.setFileName(m_singleTankLogFilePath);
    if (!m_singleTankLogFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendLog(QString("单罐闭环日志打开失败: %1").arg(QDir::toNativeSeparators(m_singleTankLogFilePath)));
        m_singleTankLogActive = false;
        m_singleTankLogFilePath.clear();
        updateSingleTankLogUi();
        return false;
    }

    m_singleTankLogActive = true;
    m_singleTankLogLastWriteMs = 0;
    writeSingleTankLogHeader();
    updateSingleTankLogUi();
    appendLog(QString("单罐闭环日志已开始: %1").arg(QDir::toNativeSeparators(m_singleTankLogFilePath)));
    return true;
}

void MainWindow::stopSingleTankLogSession(const QString &reason)
{
    if (m_singleTankLogActive) {
        recordSingleTankSnapshot(m_snapshot, QStringLiteral("session_stop:%1").arg(reason));
    }
    if (m_singleTankLogFile.isOpen()) {
        m_singleTankLogFile.close();
    }
    if (m_singleTankLogActive) {
        appendLog(QString("单罐闭环日志已结束: %1").arg(reason));
    }
    m_singleTankLogActive = false;
    m_singleTankLogLastWriteMs = 0;
    updateSingleTankLogUi();
}

void MainWindow::writeSingleTankLogHeader()
{
    if (!m_singleTankLogFile.isOpen()) {
        return;
    }

    QStringList columns{
        "host_time",
        "snapshot_seq",
        "runtime_state",
        "runtime_state_text",
        "elapsed_ms",
        "workflow_running",
        "workflow_paused",
        "selected_tank_index",
        "selected_tank_name",
        "target_mmhg",
        "tolerance_mmhg",
        "selected_sensor_no",
        "selected_sensor_mmhg",
        "selected_sensor_valid",
        "selected_sensor_fault",
        "selected_sensor_status_byte",
        "selected_sensor_fault_code",
        "selected_sensor_fault_reason",
        "single_tank_protection_active",
        "single_tank_protection_reason",
        "single_tank_protection_tank_index",
        "single_tank_protection_sensor_no",
        "single_tank_protection_inlet_valve",
        "valve_mask_hex",
        "event"
    };
    for (int sensor = 0; sensor < kPressureSensorCount; ++sensor) {
        columns << QString("pressure%1_mmhg").arg(sensor + 1);
        columns << QString("pressure%1_valid").arg(sensor + 1);
        columns << QString("pressure%1_fault").arg(sensor + 1);
        columns << QString("pressure%1_status_byte").arg(sensor + 1);
        columns << QString("pressure%1_fault_code").arg(sensor + 1);
    }

    m_singleTankLogFile.write((columns.join(',') + "\n").toUtf8());
    m_singleTankLogFile.flush();
}

void MainWindow::recordSingleTankSnapshot(const FixtureSnapshot &previous, const QString &extraEvent)
{
    if (!m_singleTankLogActive || !m_singleTankLogFile.isOpen()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int tankIndex = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
    const QString tankName = (tankIndex >= 0 && tankIndex < kTankCount) ? tankSpecs()[tankIndex].name : QString();
    const int sensorNo = (tankIndex >= 0 && tankIndex < kTankCount) ? tankSpecs()[tankIndex].pressureSensor : 0;
    const int sensorIndex = sensorNo > 0 ? sensorNo - 1 : -1;
    const double targetMmHg = m_singleTankTargetSpin ? m_singleTankTargetSpin->value() : 0.0;
    const double toleranceMmHg = m_singleTankToleranceSpin ? m_singleTankToleranceSpin->value() : 0.0;

    QStringList events;
    if (!extraEvent.isEmpty()) {
        events << extraEvent;
    }
    if (previous.sequence != 0 && previous.state != m_snapshot.state) {
        events << QString("state:%1->%2").arg(stateDisplayName(previous.state), stateDisplayName(m_snapshot.state));
    }
    if (previous.sequence != 0 && valveMaskText(previous) != valveMaskText(m_snapshot)) {
        events << QString("valve_mask:%1->%2").arg(valveMaskText(previous), valveMaskText(m_snapshot));
    }
    if (previous.sequence != 0 &&
        previous.singleTankProtectionActive != m_snapshot.singleTankProtectionActive) {
        events << QString("single_tank_protection:%1->%2")
                      .arg(previous.singleTankProtectionActive ? 1 : 0)
                      .arg(m_snapshot.singleTankProtectionActive ? 1 : 0);
    }
    if (previous.sequence != 0 &&
        (previous.singleTankProtectionReason != m_snapshot.singleTankProtectionReason ||
         previous.singleTankProtectionInletValve != m_snapshot.singleTankProtectionInletValve ||
         previous.singleTankProtectionSensorIndex != m_snapshot.singleTankProtectionSensorIndex)) {
        events << QString("single_tank_protection_detail:%1")
                      .arg(singleTankProtectionUiText(m_snapshot));
    }
    for (int i = 0; i < kPressureSensorCount; ++i) {
        if (previous.sequence != 0 && previous.pressureValid[i] != m_snapshot.pressureValid[i]) {
            events << QString("P%1_valid:%2->%3").arg(i + 1).arg(previous.pressureValid[i] ? 1 : 0).arg(m_snapshot.pressureValid[i] ? 1 : 0);
        }
        if (previous.sequence != 0 && previous.pressureFaultLatched[i] != m_snapshot.pressureFaultLatched[i]) {
            events << QString("P%1_fault:%2->%3").arg(i + 1).arg(previous.pressureFaultLatched[i] ? 1 : 0).arg(m_snapshot.pressureFaultLatched[i] ? 1 : 0);
        }
        if (previous.sequence != 0 &&
            (previous.pressureStatusByte[i] != m_snapshot.pressureStatusByte[i] ||
             previous.pressureFaultCode[i] != m_snapshot.pressureFaultCode[i])) {
            events << QString("P%1_code:%2/%3->%4/%5")
                          .arg(i + 1)
                          .arg(hexByteText(previous.pressureStatusByte[i]))
                          .arg(hexByteText(previous.pressureFaultCode[i]))
                          .arg(hexByteText(m_snapshot.pressureStatusByte[i]))
                          .arg(hexByteText(m_snapshot.pressureFaultCode[i]));
        }
    }
    if (sensorIndex >= 0 && sensorIndex < kPressureSensorCount &&
        previous.sequence != 0 &&
        pressureSensorValid(m_snapshot, sensorIndex)) {
        const double currentMmHg = toMmHg(m_snapshot.pressure001mmHg[sensorIndex]);
        const double previousMmHg = toMmHg(previous.pressure001mmHg[sensorIndex]);
        const double warnThreshold = targetMmHg + qMax(5.0, toleranceMmHg * 2.0);
        const double dangerThreshold = 290.0;
        if (previousMmHg <= warnThreshold && currentMmHg > warnThreshold) {
            events << QString("selected_pressure_warn:%1mmHg").arg(currentMmHg, 0, 'f', 2);
        }
        if (previousMmHg <= dangerThreshold && currentMmHg > dangerThreshold) {
            events << QString("selected_pressure_danger:%1mmHg").arg(currentMmHg, 0, 'f', 2);
        }
    }

    const bool periodicDue = m_singleTankLogLastWriteMs == 0 || (nowMs - m_singleTankLogLastWriteMs) >= 1000;
    if (!periodicDue && events.isEmpty()) {
        return;
    }

    QStringList fields{
        csvField(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)),
        QString::number(m_snapshot.sequence),
        QString::number(stateIndex(m_snapshot.state)),
        csvField(stateDisplayName(m_snapshot.state)),
        QString::number(m_snapshot.elapsedMs),
        boolText(m_snapshot.running),
        boolText(m_snapshot.paused),
        QString::number(tankIndex + 1),
        csvField(tankName),
        QString::number(targetMmHg, 'f', 1),
        QString::number(toleranceMmHg, 'f', 1),
        QString::number(sensorNo),
        sensorIndex >= 0 ? QString::number(toMmHg(m_snapshot.pressure001mmHg[sensorIndex]), 'f', 3) : QString(),
        sensorIndex >= 0 ? boolText(pressureSensorValid(m_snapshot, sensorIndex)) : QString(),
        sensorIndex >= 0 ? boolText(pressureSensorFaultLatched(m_snapshot, sensorIndex)) : QString(),
        sensorIndex >= 0 ? csvField(hexByteText(m_snapshot.pressureStatusByte[sensorIndex])) : QString(),
        sensorIndex >= 0 ? csvField(hexByteText(m_snapshot.pressureFaultCode[sensorIndex])) : QString(),
        sensorIndex >= 0 ? csvField(pressureSensorFaultReasonText(m_snapshot, sensorIndex)) : QString(),
        boolText(m_snapshot.singleTankProtectionActive),
        csvField(m_snapshot.singleTankProtectionActive ? singleTankProtectionReasonText(m_snapshot) : QString()),
        m_snapshot.singleTankProtectionActive ? QString::number(m_snapshot.singleTankProtectionTankIndex + 1) : QString(),
        m_snapshot.singleTankProtectionActive ? QString::number(m_snapshot.singleTankProtectionSensorIndex + 1) : QString(),
        m_snapshot.singleTankProtectionActive ? QString::number(m_snapshot.singleTankProtectionInletValve) : QString(),
        csvField(valveMaskText(m_snapshot)),
        csvField(events.join(" | "))
    };

    for (int sensor = 0; sensor < kPressureSensorCount; ++sensor) {
        fields << QString::number(toMmHg(m_snapshot.pressure001mmHg[sensor]), 'f', 3);
        fields << boolText(m_snapshot.pressureValid[sensor]);
        fields << boolText(m_snapshot.pressureFaultLatched[sensor]);
        fields << csvField(hexByteText(m_snapshot.pressureStatusByte[sensor]));
        fields << csvField(hexByteText(m_snapshot.pressureFaultCode[sensor]));
    }

    m_singleTankLogFile.write((fields.join(',') + "\n").toUtf8());
    m_singleTankLogFile.flush();
    m_singleTankLogLastWriteMs = nowMs;
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
            pcbaCurrent50mA ? "10次平均电流 mA(已矫正)" : "10次平均电流 uA(已矫正)",
            pcbaCurrent50mA ? "均方差 mA^2" : "均方差 uA^2",
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
        const bool calibrated = (m_snapshot.pressureCalibrationValidMask & (1u << (sensor - 1))) != 0u;
        auto *calibrationItem = new QTableWidgetItem(calibrated ? "已校准" : "未校准");
        const auto diagnosticIndex = static_cast<size_t>(sensor - 1);
        const uint16_t saturationEvents =
            m_snapshot.pressureMathSaturationEventCount[diagnosticIndex];
        const uint16_t recoveryAttempts =
            m_snapshot.pressureMathSaturationAttemptCount[diagnosticIndex];
        const uint16_t recoverySuccesses =
            m_snapshot.pressureMathSaturationSuccessCount[diagnosticIndex];
        auto *diagnosticItem = new QTableWidgetItem(saturationEvents == 0u
            ? QStringLiteral("0 次")
            : QString("成功%1/%2 | 尝试%3")
                  .arg(recoverySuccesses)
                  .arg(saturationEvents)
                  .arg(recoveryAttempts));
        diagnosticItem->setForeground(QBrush(QColor(
            saturationEvents == 0u || recoverySuccesses == saturationEvents ? "#15803d" : "#b45309")));
        diagnosticItem->setToolTip(
            QString("本次上电累计：数学饱和事件 %1 次；自动总线恢复/重测 %2 次；恢复成功 %3 次。MCU重启后清零。")
                .arg(saturationEvents)
                .arg(recoveryAttempts)
                .arg(recoverySuccesses));
        calibrationItem->setForeground(QBrush(QColor(calibrated ? "#15803d" : "#b45309")));
        calibrationItem->setToolTip(m_snapshot.pressureCalibrationStatusAvailable
            ? QStringLiteral("MCU 标定存储状态")
            : QStringLiteral("当前固件未上报标定有效位，按未校准显示"));
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
        m_pressureTable->setItem(sensor - 1, 2, calibrationItem);
        m_pressureTable->setItem(sensor - 1, 3, diagnosticItem);
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
            const QString varianceText = pcbaCurrent50mA
                ? formatVarianceUa2AsMa2(channel.workCurrentVarianceUa2, channel.workCurrentValid)
                : formatVarianceUa2(channel.standbyCurrentVarianceUa2, channel.standbyCurrentValid);
            m_pcbaCurrentTable->setItem(i, 0, new QTableWidgetItem(QString("通道%1").arg(i + 1)));
            m_pcbaCurrentTable->setItem(i, 1, new QTableWidgetItem(realtimeCurrent));
            m_pcbaCurrentTable->setItem(i, 2, new QTableWidgetItem(varianceText));
            m_pcbaCurrentTable->setItem(i, 3, new QTableWidgetItem(channel.currentRawAdcValid
                                                                   ? QString::number(channel.currentRawAdc)
                                                                   : "--"));
            m_pcbaCurrentTable->setItem(i, 4, new QTableWidgetItem(m_snapshot.adcReferenceValid &&
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
                                                                ? "mA模式，PB1共享低阻支路已打开，按10次/0.1s平均"
                                                                : "uA模式，PB1共享低阻支路已关闭，按10次/0.1s平均"))
                                              .arg(validRealtimeCount)
                                              .arg(kChannelCount));
    }
    updatePcbaCurrentChart();

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
    updateSinglePcbaCurrentCharts();

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
    updateSingleTankPcbaCurrentCharts();

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

void MainWindow::updatePcbaCurrentChart()
{
    if (!m_pcbaCurrentChartView || !m_pcbaCurrentSeries || !m_pcbaCurrentAverageSeries ||
        !m_pcbaCurrentAxisX || !m_pcbaCurrentAxisY) {
        return;
    }

    const bool pcbaCurrent50mA = m_snapshot.pcbaCurrent50mAEnabled;
    const int channelIndex = m_pcbaCurrentChartChannelCombo
        ? std::clamp(m_pcbaCurrentChartChannelCombo->currentIndex(), 0, kChannelCount - 1)
        : 0;
    const auto &channel = m_snapshot.channels[channelIndex];
    const bool valid = pcbaCurrent50mA ? channel.workCurrentValid : channel.standbyCurrentValid;
    const auto &samples = pcbaCurrent50mA ? channel.workCurrentSamplesUaX100 : channel.standbyCurrentSamplesUaX100;
    const uint32_t averageUaX100 = pcbaCurrent50mA ? channel.workCurrentUaX100 : channel.standbyCurrentUaX100;
    const uint32_t varianceUa2 = pcbaCurrent50mA ? channel.workCurrentVarianceUa2 : channel.standbyCurrentVarianceUa2;

    m_pcbaCurrentSeries->clear();
    m_pcbaCurrentAverageSeries->clear();

    double minValue = std::numeric_limits<double>::max();
    double maxValue = 0.0;
    bool hasPoint = false;
    for (int sample = 0; sample < kCurrentSampleCount; ++sample) {
        const double value = pcbaCurrent50mA
            ? samples[static_cast<size_t>(sample)] / 100000.0
            : samples[static_cast<size_t>(sample)] / 100.0;
        m_pcbaCurrentSeries->append(sample + 1, value);
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
        hasPoint = true;
    }

    const double averageValue = pcbaCurrent50mA ? averageUaX100 / 100000.0 : averageUaX100 / 100.0;
    if (valid) {
        m_pcbaCurrentAverageSeries->append(1, averageValue);
        m_pcbaCurrentAverageSeries->append(kCurrentSampleCount, averageValue);
        minValue = std::min(minValue, averageValue);
        maxValue = std::max(maxValue, averageValue);
        hasPoint = true;
    }

    if (!hasPoint || maxValue <= 0.0) {
        m_pcbaCurrentAxisY->setRange(0.0, 1.0);
    } else {
        const double span = std::max(maxValue - minValue, pcbaCurrent50mA ? 0.001 : 0.05);
        const double margin = std::max(span * 0.25, pcbaCurrent50mA ? 0.001 : 0.05);
        const double axisMin = std::max(0.0, minValue - margin);
        const double axisMax = maxValue + margin;
        m_pcbaCurrentAxisY->setRange(axisMin, axisMax);
    }

    m_pcbaCurrentAxisY->setTitleText(pcbaCurrent50mA ? "电流 (mA)" : "电流 (uA)");
    m_pcbaCurrentAxisY->setLabelFormat(pcbaCurrent50mA ? "%.3f" : "%.2f");
    if (m_pcbaCurrentChartView->chart()) {
        m_pcbaCurrentChartView->chart()->setTitle(
            QString("PCBA 电流 10次采样曲线 | 通道%1 | %2")
                .arg(channelIndex + 1)
                .arg(pcbaCurrent50mA ? "mA模式" : "uA模式"));
    }

    if (m_pcbaCurrentChartSummaryLabel) {
        m_pcbaCurrentChartSummaryLabel->setText(
            QString("通道%1 | %2 | 10次平均 %3 | 均方差 %4")
                .arg(channelIndex + 1)
                .arg(pcbaCurrent50mA ? "mA模式" : "uA模式")
                .arg(pcbaCurrent50mA
                         ? formatCurrentUaX100AsMa(averageUaX100, valid, 3, true)
                         : formatCurrentUaX100(averageUaX100, valid, 2, true))
                .arg(pcbaCurrent50mA
                         ? formatVarianceUa2AsMa2(varianceUa2, valid, 6, true)
                         : formatVarianceUa2(varianceUa2, valid, 2, true)));
    }
}

void MainWindow::initializeCurrentChartWidgets(MainWindow::CurrentChartWidgets &widgets,
                                               QWidget *parent,
                                               QVBoxLayout *layout,
                                               const QString &title)
{
    widgets.summaryLabel = new QLabel(parent);
    widgets.summaryLabel->setWordWrap(true);
    widgets.summaryLabel->setStyleSheet("color: #475569;");
    widgets.summaryLabel->setText("等待电流采样结果");
    layout->addWidget(widgets.summaryLabel);

    auto *chart = new QChart();
    chart->legend()->setVisible(true);
    chart->setTitle(title);
    widgets.samplesSeries = new QLineSeries(chart);
    widgets.samplesSeries->setName("10次采样");
    widgets.averageSeries = new QLineSeries(chart);
    widgets.averageSeries->setName("平均值");
    {
        QPen averagePen(QColor("#dc2626"));
        averagePen.setWidth(2);
        averagePen.setStyle(Qt::DashLine);
        widgets.averageSeries->setPen(averagePen);
    }
    chart->addSeries(widgets.samplesSeries);
    chart->addSeries(widgets.averageSeries);

    widgets.axisX = new QValueAxis(chart);
    widgets.axisX->setTitleText("采样序号");
    widgets.axisX->setLabelFormat("%d");
    widgets.axisX->setTickCount(kCurrentSampleCount);
    widgets.axisX->setRange(1, kCurrentSampleCount);
    chart->addAxis(widgets.axisX, Qt::AlignBottom);
    widgets.samplesSeries->attachAxis(widgets.axisX);
    widgets.averageSeries->attachAxis(widgets.axisX);

    widgets.axisY = new QValueAxis(chart);
    widgets.axisY->setTitleText("电流 (uA)");
    widgets.axisY->setLabelFormat("%.2f");
    chart->addAxis(widgets.axisY, Qt::AlignLeft);
    widgets.samplesSeries->attachAxis(widgets.axisY);
    widgets.averageSeries->attachAxis(widgets.axisY);

    widgets.view = new QChartView(chart, parent);
    widgets.view->setRenderHint(QPainter::Antialiasing);
    widgets.view->setMinimumHeight(220);
    layout->addWidget(widgets.view);
}

void MainWindow::updateCurrentChartWidgets(MainWindow::CurrentChartWidgets &widgets,
                                           const QString &title,
                                           const QString &waitingText,
                                           const std::array<uint32_t, kCurrentSampleCount> &samplesUaX100,
                                           uint32_t averageUaX100,
                                           uint32_t varianceUa2,
                                           bool valid,
                                           bool displayAsMa)
{
    if (!widgets.view || !widgets.samplesSeries || !widgets.averageSeries ||
        !widgets.axisX || !widgets.axisY || !widgets.summaryLabel) {
        return;
    }

    widgets.samplesSeries->clear();
    widgets.averageSeries->clear();
    widgets.axisY->setTitleText(displayAsMa ? "电流 (mA)" : "电流 (uA)");
    widgets.axisY->setLabelFormat(displayAsMa ? "%.3f" : "%.2f");
    if (widgets.view->chart()) {
        widgets.view->chart()->setTitle(title);
    }

    if (!valid) {
        widgets.axisY->setRange(0.0, 1.0);
        widgets.summaryLabel->setText(waitingText);
        return;
    }

    double minValue = std::numeric_limits<double>::max();
    double maxValue = 0.0;
    for (int sample = 0; sample < kCurrentSampleCount; ++sample) {
        const double value = displayAsMa
            ? samplesUaX100[static_cast<size_t>(sample)] / 100000.0
            : samplesUaX100[static_cast<size_t>(sample)] / 100.0;
        widgets.samplesSeries->append(sample + 1, value);
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }

    const double averageValue = displayAsMa ? averageUaX100 / 100000.0 : averageUaX100 / 100.0;
    widgets.averageSeries->append(1, averageValue);
    widgets.averageSeries->append(kCurrentSampleCount, averageValue);
    minValue = std::min(minValue, averageValue);
    maxValue = std::max(maxValue, averageValue);

    const double span = std::max(maxValue - minValue, displayAsMa ? 0.001 : 0.05);
    const double margin = std::max(span * 0.25, displayAsMa ? 0.001 : 0.05);
    const double axisMin = std::max(0.0, minValue - margin);
    const double axisMax = maxValue + margin;
    widgets.axisY->setRange(axisMin, axisMax);

    widgets.summaryLabel->setText(
        QString("%1 | 10次平均 %2 | 均方差 %3")
            .arg(displayAsMa ? "工作电流" : "待机电流")
            .arg(displayAsMa
                     ? formatCurrentUaX100AsMa(averageUaX100, true, 3, true)
                     : formatCurrentUaX100(averageUaX100, true, 2, true))
            .arg(displayAsMa
                     ? formatVarianceUa2AsMa2(varianceUa2, true, 6, true)
                     : formatVarianceUa2(varianceUa2, true, 2, true)));
}

void MainWindow::updateSinglePcbaCurrentCharts()
{
    const auto &channel = m_snapshot.channels[0];
    const bool flowActive = m_snapshot.singlePcbaFlowActive ||
                            m_singlePcbaTimingRunning ||
                            m_singlePcbaStartPending ||
                            m_singlePcbaTimingReport.done ||
                            m_singlePcbaTimingReport.count > 0;

    updateCurrentChartWidgets(m_singlePcbaStandbyChart,
                              QStringLiteral("单PCBA全流程测试 | 待机电流 10次采样曲线 | 1号位"),
                              QStringLiteral("等待本次单PCBA全流程测试的待机 uA 电流结果"),
                              channel.standbyCurrentSamplesUaX100,
                              channel.standbyCurrentUaX100,
                              channel.standbyCurrentVarianceUa2,
                              flowActive && channel.standbyCurrentValid,
                              false);
    updateCurrentChartWidgets(m_singlePcbaWorkChart,
                              QStringLiteral("单PCBA全流程测试 | 工作电流 10次采样曲线 | 1号位"),
                              QStringLiteral("等待本次单PCBA全流程测试的工作 mA 电流结果"),
                              channel.workCurrentSamplesUaX100,
                              channel.workCurrentUaX100,
                              channel.workCurrentVarianceUa2,
                              flowActive && channel.workCurrentValid,
                              true);
}

void MainWindow::updateSingleTankPcbaCurrentCharts()
{
    const auto &channel = m_snapshot.channels[0];
    const bool flowActive = m_singleTankPcbaRunning ||
                            m_singleTankPcbaStartPending ||
                            m_singleTankPcbaReport.done ||
                            m_singleTankPcbaReport.count > 0;
    const uint32_t standbyAverageUaX100 =
        m_singleTankPcbaReport.standbyCurrentUaX100 > 0u
            ? m_singleTankPcbaReport.standbyCurrentUaX100
            : channel.standbyCurrentUaX100;
    const uint32_t workAverageUaX100 =
        m_singleTankPcbaReport.workCurrentUaX100 > 0u
            ? m_singleTankPcbaReport.workCurrentUaX100
            : channel.workCurrentUaX100;

    updateCurrentChartWidgets(m_singleTankPcbaStandbyChart,
                              QStringLiteral("单罐单PCBA测试 | 待机电流 10次采样曲线 | 1号位"),
                              QStringLiteral("等待本次单罐单PCBA测试的待机 uA 电流结果"),
                              channel.standbyCurrentSamplesUaX100,
                              standbyAverageUaX100,
                              channel.standbyCurrentVarianceUa2,
                              flowActive && channel.standbyCurrentValid,
                              false);
    updateCurrentChartWidgets(m_singleTankPcbaWorkChart,
                              QStringLiteral("单罐单PCBA测试 | 工作电流 10次采样曲线 | 1号位"),
                              QStringLiteral("等待本次单罐单PCBA测试的工作 mA 电流结果"),
                              channel.workCurrentSamplesUaX100,
                              workAverageUaX100,
                              channel.workCurrentVarianceUa2,
                              flowActive && channel.workCurrentValid,
                              true);
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
    const int visibleRows = kSingleTankPcbaStepCount;
    m_singleTankPcbaTable->setRowCount(visibleRows);
    for (int row = 0; row < visibleRows; ++row) {
        const bool hasEntry = row < report.count;
        const SingleTankPcbaEntry entry = hasEntry ? report.entries[static_cast<size_t>(row)] : SingleTankPcbaEntry{};
        const SingleTankPcbaEntry *trendEntry = hasEntry
            ? singleTankPcbaTrendSource(report, row)
            : nullptr;
        const bool pass = hasEntry && entry.ok;
        const bool fail = hasEntry && !entry.ok;
        if (pass) {
            ++passCount;
        } else if (fail) {
            ++failCount;
        }
        if (hasEntry && entry.kind <= 1u && entry.elapsedUs > maxElapsedUs) {
            maxElapsedUs = entry.elapsedUs;
        }

        const QString elapsedText = !hasEntry || entry.kind == 2u || entry.elapsedUs == 0u
            ? QStringLiteral("--")
            : (entry.kind == 4u
                ? QString("观察 %1 ms").arg(entry.trendObservationUs / 1000.0, 0, 'f', 0)
                : QString("回包 %1 ms").arg(entry.elapsedUs / 1000.0, 0, 'f', 3));
        const QString currentText = hasEntry && ((entry.flags & kSingleTankPcbaFlagCurrent) != 0)
            ? formatCurrentUaX100(entry.currentUaX100, entry.ok, 2, true)
            : "--";
        const bool flowActive = report.running || m_singleTankPcbaRunning || m_singleTankPcbaStartPending;
        const uint8_t displayFlags = hasEntry ? entry.flags : expectedSingleTankPcbaFlags(row);
        const bool trendStep = singleTankPcbaStepSpecs()[static_cast<size_t>(row)].skipOnly;
        const QString pendingText = !flowActive
            ? QStringLiteral("未执行")
            : ((row == 0 || row == 2)
                ? QStringLiteral("等待电流采样结果")
                : (row == 5
                    ? QStringLiteral("MCU正在VENT：每次开阀至少30秒，达到0.0±0.1mmHg后继续VENT 30秒，再关阀均压确认零点")
                    : (trendStep
                        ? QStringLiteral("MCU正在单次加压并采集自然趋势")
                        : QStringLiteral("等待PCBA回包；10ms内耗时仅统计"))));
        const QStringList values{
            singleTankPcbaStepText(row),
            singleTankPcbaActionText(row, displayFlags),
            hasEntry ? singleTankPcbaTxTextForEntry(row, entry) : singleTankPcbaTxText(row),
            hasEntry ? singleTankPcbaRxText(entry) : "--",
            hasEntry ? singleTankPcbaParsedText(row, entry, trendEntry) : pendingText,
            elapsedText,
            currentText,
            hasEntry ? singleTankPcbaReasonText(entry, trendEntry) : (flowActive ? "等待" : "未执行"),
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
            : ((report.running || m_singleTankPcbaRunning) &&
                       report.count >= kSingleTankPcbaStepCount
                   ? "测试结束，VENT至零中"
                   : (report.running || m_singleTankPcbaRunning ? "测试运行中" : "等待启动"));
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
    bool showValveStatus = !isDebugMode();
    bool showPressureStatus = !isDebugMode();
    bool showPcbaStatus = !isDebugMode();
    if (isDebugMode() && m_flowList && m_flowList->currentItem()) {
        const auto kind = static_cast<LeftItemKind>(m_flowList->currentItem()->data(kLeftKindRole).toInt());
        if (kind == LeftItemKind::DebugTool) {
            const auto tool = selectedDebugTool();
            showValveStatus = tool == DebugTool::SingleTank ||
                              tool == DebugTool::ManualValve;
            showPressureStatus = showValveStatus || tool == DebugTool::SensorCalibration;
            showPcbaStatus = showValveStatus;
        }
    }

    if (m_valveTable) {
        m_valveTable->setVisible(showValveStatus);
    }
    if (m_pressureTable) {
        m_pressureTable->setVisible(showPressureStatus);
    }
    if (m_pcbaTable) {
        m_pcbaTable->setVisible(showPcbaStatus);
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
                            (tool == DebugTool::SensorCalibration &&
                             (m_sensorCalibrationStatus.active || m_snapshot.pressureCalibrationModeActive ||
                              m_snapshot.state == RuntimeState::SensorCalibration)) ||
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
