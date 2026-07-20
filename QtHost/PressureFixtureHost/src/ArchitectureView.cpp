#include "ArchitectureView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <algorithm>
#include <cmath>

using namespace fixture;

namespace {
constexpr qreal kLogicalWidth = 3180.0;
constexpr qreal kLogicalHeight = 1180.0;
constexpr qreal kTopY = 315.0;
constexpr qreal kRegulatorY = 395.0;
constexpr qreal kInletValveY = 470.0;
constexpr qreal kTankY = 560.0;
constexpr qreal kReliefY = 705.0;
constexpr qreal kSensorY = 805.0;
constexpr qreal kOutletValveY = 930.0;
constexpr qreal kBusY = 1090.0;

const std::array<qreal, kTankCount> tankX{150, 360, 570, 790, 1010, 1230};
const std::array<qreal, kChannelCount> channelX{1465, 1675, 1885, 2095, 2305, 2515, 2725, 2935};

}

ArchitectureView::ArchitectureView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setZoom(1.0);
    setAutoFillBackground(false);
    m_blinkTimer.setInterval(420);
    connect(&m_blinkTimer, &QTimer::timeout, this, &ArchitectureView::toggleBlink);
    m_blinkTimer.start();
    rebuildHitMap();
}

void ArchitectureView::setSnapshot(const FixtureSnapshot &snapshot)
{
    m_snapshot = snapshot;
    for (auto it = m_pendingValveStates.begin(); it != m_pendingValveStates.end();) {
        const int valve = it.key();
        if (valve >= 1 && valve <= kValveCount && m_snapshot.valvesOpen[valve] == it.value()) {
            it = m_pendingValveStates.erase(it);
        } else {
            ++it;
        }
    }
    update();
}

void ArchitectureView::setPendingValveCommand(int valveNumber, bool open)
{
    if (valveNumber < 1 || valveNumber > kValveCount) {
        return;
    }
    m_pendingValveStates.insert(valveNumber, open);
    update();
}

void ArchitectureView::clearPendingValveCommands()
{
    m_pendingValveStates.clear();
    update();
}

double ArchitectureView::zoom() const
{
    return m_zoom;
}

void ArchitectureView::setZoom(double zoomFactor)
{
    m_zoom = std::clamp(zoomFactor, 0.35, 2.5);
    const QSize scaledSize(static_cast<int>(std::lround(kLogicalWidth * m_zoom)),
                           static_cast<int>(std::lround(kLogicalHeight * m_zoom)));
    setMinimumSize(scaledSize);
    setMaximumSize(scaledSize);
    resize(scaledSize);
    updateGeometry();
    update();
}

void ArchitectureView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor("#f7f8fb"));

    const QColor pipeIdle("#8c98a8");
    const QColor pipeActive = m_blinkOn ? QColor("#1f9d64") : QColor("#a4f0c7");

    drawPipe(painter, {55, kTopY}, {150, kTopY}, pipeIdle, 6);
    drawBox(painter, {20, 205, 70, 220}, "公司\n空压机\n气源", QColor("#e5edf7"), QColor("#65758b"), false, 16.0);
    drawBox(painter, {150, kTopY - 25, 205, 50}, "车间气路通道", QColor("#e5edf7"), QColor("#65758b"), false, 17.0);
    drawPipe(painter, {355, kTopY}, {1370, kTopY}, pipeIdle, 6);
    drawBox(painter, {150, kTopY - 25, 1220, 50}, "标定机内部气源母管", QColor("#eaf2ff"), QColor("#64748b"), false, 17.0);

    for (int i = 0; i < kTankCount; ++i) {
        const qreal x = tankX[i];
        const auto &tank = tankSpecs()[i];
        drawPipe(painter, {x + 80, kTopY + 25}, {x + 80, kRegulatorY}, pipeIdle, 4);
        drawBox(painter, {x, kRegulatorY, 160, 46}, QString("减压阀 %1").arg(tank.targetMmHg), QColor("#eef2f7"), QColor("#94a3b8"), false, 17.0);
        drawPipe(painter, {x + 80, kRegulatorY + 46}, {x + 80, kInletValveY}, pipeIdle, 4);
        drawValve(painter, tank.inletValve, {x + 10, kInletValveY, 140, 52});
        drawPipe(painter, {x + 80, kInletValveY + 52}, {x + 80, kTankY}, m_snapshot.valvesOpen[tank.inletValve] ? pipeActive : pipeIdle, 4);
        drawTank(painter, i, {x + 5, kTankY, 150, 100});
        drawPipe(painter, {x + 48, kTankY + 100}, {x + 48, kReliefY}, m_snapshot.valvesOpen[tank.reliefValve] ? pipeActive : pipeIdle, 4);
        drawValve(painter, tank.reliefValve, {x + 12, kReliefY, 76, 58});
        drawPipe(painter, {x + 112, kTankY + 100}, {x + 112, kSensorY}, pipeIdle, 3);
        drawSensor(painter, tank.pressureSensor, {x + 5, kSensorY, 150, 78});
        drawPipe(painter, {x + 80, kSensorY + 78}, {x + 80, kOutletValveY}, m_snapshot.valvesOpen[tank.outletValve] ? pipeActive : pipeIdle, 4);
        drawValve(painter, tank.outletValve, {x + 15, kOutletValveY, 130, 70});
        drawPipe(painter, {x + 80, kOutletValveY + 70}, {x + 80, kBusY}, m_snapshot.valvesOpen[tank.outletValve] ? pipeActive : pipeIdle, 4);
    }

    drawBox(painter, {135, kBusY - 30, 2980, 60}, "八路 PCBA 共用压力母管", QColor("#f0f7ff"), QColor("#64748b"), false, 18.0);
    drawPipe(painter, {150, kBusY}, {3100, kBusY}, pipeIdle, 7);

    for (int i = 0; i < kChannelCount; ++i) {
        const qreal x = channelX[i];
        const auto &channel = channelSpecs()[i];
        const bool open = m_snapshot.valvesOpen[channel.valve];
        drawPipe(painter, {x + 80, kBusY}, {x + 80, kOutletValveY + 70}, open ? pipeActive : pipeIdle, 4);
        drawValve(painter, channel.valve, {x + 15, kOutletValveY, 130, 70});
        drawPipe(painter, {x + 80, kOutletValveY}, {x + 80, kSensorY + 78}, open ? pipeActive : pipeIdle, 4);
        drawSensor(painter, channel.pressureSensor, {x, kSensorY, 160, 78});
        drawPipe(painter, {x + 80, kSensorY}, {x + 80, kTankY + 100}, open ? pipeActive : pipeIdle, 4);
        drawChannel(painter, i, {x + 10, kTankY, 140, 104});
    }

    drawBox(painter, {1780, 145, 600, 150},
            "主控板 / PC 控制\n状态: " + stateDisplayName(m_snapshot.state) +
                QString("\n链路: %1").arg(m_snapshot.linkMode == LinkMode::UsbCdc ? "USB CDC" : "未连接"),
            QColor("#ecfdf5"), QColor("#0f766e"), m_snapshot.remoteControlEnabled);
}

void ArchitectureView::mouseMoveEvent(QMouseEvent *event)
{
    const QString key = hitKeyAt(event->position());
    setToolTip(tooltipForKey(key));
    setCursor(key.startsWith("valve:") || key.startsWith("sensor:") ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ArchitectureView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QString key = hitKeyAt(event->position());
    if (!key.startsWith("valve:") && !key.startsWith("sensor:")) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QStringList parts = key.split(':');
    if (parts.size() == 2) {
        const int number = parts[1].toInt();
        if (parts[0] == "valve" && number >= 1 && number <= kValveCount) {
            emit valveClicked(number);
            event->accept();
            return;
        }
        if (parts[0] == "sensor" && number >= 1 && number <= kPressureSensorCount) {
            emit sensorClicked(number);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ArchitectureView::toggleBlink()
{
    m_blinkOn = !m_blinkOn;
    update();
}

QRectF ArchitectureView::mapRect(const QRectF &logical) const
{
    const qreal scale = m_zoom;
    const qreal ox = 0.0;
    const qreal oy = 0.0;
    return {ox + logical.x() * scale, oy + logical.y() * scale, logical.width() * scale, logical.height() * scale};
}

QPointF ArchitectureView::mapPoint(const QPointF &logical) const
{
    return mapRect({logical.x(), logical.y(), 0, 0}).topLeft();
}

QColor ArchitectureView::valveColor(int valve) const
{
    if (valve < 1 || valve > kValveCount) {
        return QColor("#cbd5e1");
    }
    if (m_pendingValveStates.contains(valve)) {
        return m_blinkOn ? QColor("#facc15") : QColor("#fef3c7");
    }
    if (m_snapshot.valvesOpen[valve]) {
        return m_blinkOn ? QColor("#22c55e") : QColor("#bbf7d0");
    }
    return QColor("#e2e8f0");
}

QColor ArchitectureView::tankColor(int tankIndex) const
{
    if (pressureSensorFaultLatched(m_snapshot, tankIndex)) {
        return QColor("#fecaca");
    }
    if (!pressureSensorValid(m_snapshot, tankIndex)) {
        return QColor("#f1f5f9");
    }
    const int pressure = m_snapshot.pressure001mmHg[tankIndex];
    const int target = to001mmHg(tankSpecs()[tankIndex].targetMmHg);
    const bool close = std::abs(pressure - target) <= to001mmHg(3.0);
    if (close) {
        return QColor("#dbeafe");
    }
    return QColor("#fde68a");
}

QColor ArchitectureView::channelColor(int channelIndex) const
{
    const auto &channel = m_snapshot.channels[channelIndex];
    if (!channel.online) {
        return QColor("#f1f5f9");
    }
    if (m_snapshot.state == RuntimeState::Result) {
        return channel.pass ? QColor("#dcfce7") : QColor("#fee2e2");
    }
    return m_blinkOn ? QColor("#ccfbf1") : QColor("#f0fdfa");
}

void ArchitectureView::drawPipe(QPainter &painter, const QPointF &a, const QPointF &b, const QColor &color, qreal width) const
{
    QPen pen(color, width);
    pen.setWidthF(width * m_zoom);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(mapPoint(a), mapPoint(b));
}

void ArchitectureView::drawBox(QPainter &painter,
                               const QRectF &rect,
                               const QString &text,
                               const QColor &fill,
                               const QColor &stroke,
                               bool strong,
                               qreal fontPx) const
{
    const QRectF r = mapRect(rect);
    painter.setPen(QPen(stroke, (strong ? 2.5 : 1.4) * m_zoom));
    painter.setBrush(fill);
    painter.drawRoundedRect(r, 7 * m_zoom, 7 * m_zoom);
    painter.setPen(QColor("#1f2937"));
    QFont f = painter.font();
    f.setPixelSize(std::max(7, static_cast<int>(std::lround(fontPx * m_zoom))));
    f.setBold(strong);
    painter.setFont(f);
    QRectF textRect = r.adjusted(8 * m_zoom, 5 * m_zoom, -8 * m_zoom, -5 * m_zoom);
    QFontMetricsF metrics(f);
    QRectF bounds = metrics.boundingRect(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);
    while ((bounds.width() > textRect.width() || bounds.height() > textRect.height()) && f.pixelSize() > std::max(7, static_cast<int>(std::lround(12 * m_zoom)))) {
        f.setPixelSize(f.pixelSize() - 1);
        painter.setFont(f);
        metrics = QFontMetricsF(f);
        bounds = metrics.boundingRect(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);
    }
    painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);
}

void ArchitectureView::drawValve(QPainter &painter, int valve, const QRectF &rect) const
{
    const bool open = valve >= 1 && valve <= kValveCount && m_snapshot.valvesOpen[valve];
    const bool pending = m_pendingValveStates.contains(valve);
    const bool pendingOpen = pending && m_pendingValveStates.value(valve);
    const QColor stroke = pending ? QColor("#d97706") : (open ? QColor("#15803d") : QColor("#64748b"));
    const QString stateText = pending ? QString("待%1").arg(pendingOpen ? "开" : "关") : QString(open ? "开" : "关");
    drawBox(painter, rect, QString("阀%1\n%2").arg(valve).arg(stateText), valveColor(valve), stroke, open || pending, 20.0);
}

void ArchitectureView::drawTank(QPainter &painter, int index, const QRectF &rect) const
{
    const auto &tank = tankSpecs()[index];
    const QString text = QString("%1\n目标 %2 mmHg\n实际 %3")
                             .arg(tank.name)
                             .arg(tank.targetMmHg)
                             .arg(sensorPressureText(m_snapshot, index, 1, true));
    drawBox(painter, rect, text, tankColor(index), QColor("#2563eb"), true, 17.0);
}

void ArchitectureView::drawSensor(QPainter &painter, int sensorNumber, const QRectF &rect) const
{
    const int index = sensorNumber - 1;
    const bool faultLatched = pressureSensorFaultLatched(m_snapshot, index);
    const bool calibrated = index >= 0 && index < kPressureSensorCount &&
        (m_snapshot.pressureCalibrationValidMask & (1u << index)) != 0u;
    drawBox(painter, rect,
            QString("压力检测%1\n%2\n%3")
                .arg(sensorNumber)
                .arg(sensorPressureText(m_snapshot, index, 1, true))
                .arg(calibrated ? "已校准" : "未校准"),
            faultLatched ? QColor("#fee2e2") : (calibrated ? QColor("#ecfdf5") : QColor("#fff7ed")),
            faultLatched ? QColor("#b91c1c") : (calibrated ? QColor("#15803d") : QColor("#ea580c")),
            faultLatched || calibrated, 17.0);
}

void ArchitectureView::drawChannel(QPainter &painter, int channelIndex, const QRectF &rect) const
{
    const auto &status = m_snapshot.channels[channelIndex];
    const bool pcbaPressureValid = status.online && status.pressure001mmHg > 0;
    const QString text = QString("PCBA%1\n%2\n测值 %3")
                             .arg(channelIndex + 1)
                             .arg(status.online ? "在线" : "离线")
                             .arg(formatPressure001mmHg(status.pressure001mmHg, pcbaPressureValid, 1, true));
    drawBox(painter, rect, text, channelColor(channelIndex), status.online ? QColor("#0f766e") : QColor("#64748b"), status.online, 18.0);
}

void ArchitectureView::rebuildHitMap()
{
    for (int i = 0; i < kTankCount; ++i) {
        const qreal x = tankX[i];
        const auto &tank = tankSpecs()[i];
        m_hitRects.insert(QString("valve:%1").arg(tank.inletValve), {x + 10, kInletValveY, 140, 52});
        m_hitRects.insert(QString("tank:%1").arg(i), {x + 5, kTankY, 150, 100});
        m_hitRects.insert(QString("valve:%1").arg(tank.reliefValve), {x + 12, kReliefY, 76, 58});
        m_hitRects.insert(QString("sensor:%1").arg(tank.pressureSensor), {x + 5, kSensorY, 150, 78});
        m_hitRects.insert(QString("valve:%1").arg(tank.outletValve), {x + 15, kOutletValveY, 130, 70});
    }
    for (int i = 0; i < kChannelCount; ++i) {
        const qreal x = channelX[i];
        const auto &channel = channelSpecs()[i];
        m_hitRects.insert(QString("channel:%1").arg(i), {x + 10, kTankY, 140, 104});
        m_hitRects.insert(QString("sensor:%1").arg(channel.pressureSensor), {x, kSensorY, 160, 78});
        m_hitRects.insert(QString("valve:%1").arg(channel.valve), {x + 15, kOutletValveY, 130, 70});
    }
}

QString ArchitectureView::hitKeyAt(const QPointF &widgetPosition) const
{
    for (auto it = m_hitRects.cbegin(); it != m_hitRects.cend(); ++it) {
        if (mapRect(it.value()).contains(widgetPosition)) {
            return it.key();
        }
    }
    return {};
}

QString ArchitectureView::tooltipForKey(const QString &key) const
{
    const QStringList parts = key.split(':');
    if (parts.size() != 2) {
        return {};
    }
    const int number = parts[1].toInt();
    if (parts[0] == "valve") {
        if (m_pendingValveStates.contains(number)) {
            return QString("阀%1: 待 MCU 确认%2，当前%3")
                .arg(number)
                .arg(m_pendingValveStates.value(number) ? "打开" : "关闭")
                .arg(m_snapshot.valvesOpen[number] ? "打开" : "关闭");
        }
        return QString("阀%1: %2").arg(number).arg(m_snapshot.valvesOpen[number] ? "打开" : "关闭");
    }
    if (parts[0] == "tank") {
        const auto &tank = tankSpecs()[number];
        return QString("%1\n目标 %2mmHg\n实际 %3")
            .arg(tank.name)
            .arg(tank.targetMmHg)
            .arg(sensorPressureText(m_snapshot, number, 1, true));
    }
    if (parts[0] == "sensor") {
        const bool calibrated = number >= 1 && number <= kPressureSensorCount &&
            (m_snapshot.pressureCalibrationValidMask & (1u << (number - 1))) != 0u;
        return QString("压力检测%1: %2\n标定: %3%4")
            .arg(number)
            .arg(sensorPressureText(m_snapshot, number - 1, 1, true))
            .arg(calibrated ? "已校准" : "未校准")
            .arg(pressureSensorFaultLatched(m_snapshot, number - 1) ? "\n状态: 故障锁定，需重新上电" : "");
    }
    if (parts[0] == "channel") {
        const auto &channel = m_snapshot.channels[number];
        const bool pressureValid = channel.online && channel.pressure001mmHg > 0;
        return QString("PCBA%1\n连接: %2\n测得气压: %3\n误差: %4")
            .arg(number + 1)
            .arg(channel.online ? "在线" : "离线")
            .arg(formatPressure001mmHg(channel.pressure001mmHg, pressureValid, 1, true))
            .arg(pressureValid ? QString("%1 mmHg").arg(toMmHg(channel.error001mmHg), 0, 'f', 2) : "--");
    }
    return {};
}
