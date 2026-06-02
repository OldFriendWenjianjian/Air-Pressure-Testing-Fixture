#include "ArchitectureView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

using namespace fixture;

namespace {
constexpr qreal kLogicalWidth = 2760.0;
constexpr qreal kLogicalHeight = 980.0;
constexpr qreal kTopY = 300.0;
constexpr qreal kRegulatorY = 365.0;
constexpr qreal kInletValveY = 430.0;
constexpr qreal kTankY = 510.0;
constexpr qreal kReliefY = 620.0;
constexpr qreal kSensorY = 705.0;
constexpr qreal kOutletValveY = 815.0;
constexpr qreal kBusY = 910.0;

const std::array<qreal, kTankCount> tankX{170, 335, 500, 675, 860, 1045};
const std::array<qreal, kChannelCount> channelX{1220, 1405, 1585, 1765, 1945, 2125, 2305, 2485};

}

ArchitectureView::ArchitectureView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(980, 620);
    setAutoFillBackground(false);
    m_blinkTimer.setInterval(420);
    connect(&m_blinkTimer, &QTimer::timeout, this, &ArchitectureView::toggleBlink);
    m_blinkTimer.start();
    rebuildHitMap();
}

void ArchitectureView::setSnapshot(const FixtureSnapshot &snapshot)
{
    m_snapshot = snapshot;
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

    drawPipe(painter, {60, 300}, {150, 300}, pipeIdle, 4);
    drawBox(painter, {40, 220, 40, 180}, "公司\n空压机\n气源", QColor("#e5edf7"), QColor("#65758b"));
    drawBox(painter, {140, 285, 160, 30}, "车间气路通道", QColor("#e5edf7"), QColor("#65758b"));
    drawPipe(painter, {300, 300}, {1140, 300}, pipeIdle, 4);
    drawBox(painter, {140, kTopY - 15, 1000, 30}, "标定机内部气源母管", QColor("#eaf2ff"), QColor("#64748b"));

    for (int i = 0; i < kTankCount; ++i) {
        const qreal x = tankX[i];
        const auto &tank = tankSpecs()[i];
        drawPipe(painter, {x + 55, kTopY + 15}, {x + 55, kRegulatorY}, pipeIdle, 3);
        drawBox(painter, {x, kRegulatorY, 130, 30}, QString("减压阀 %1").arg(tank.targetMmHg), QColor("#eef2f7"), QColor("#94a3b8"));
        drawPipe(painter, {x + 65, kRegulatorY + 30}, {x + 65, kInletValveY}, pipeIdle, 3);
        drawValve(painter, tank.inletValve, {x, kInletValveY, 130, 36});
        drawPipe(painter, {x + 65, kInletValveY + 36}, {x + 65, kTankY}, m_snapshot.valvesOpen[tank.inletValve] ? pipeActive : pipeIdle, 3);
        drawTank(painter, i, {x + 10, kTankY, 110, 76});
        drawPipe(painter, {x + 40, kTankY + 76}, {x + 40, kReliefY}, m_snapshot.valvesOpen[tank.reliefValve] ? pipeActive : pipeIdle, 3);
        drawValve(painter, tank.reliefValve, {x + 10, kReliefY, 62, 46});
        drawPipe(painter, {x + 75, kTankY + 76}, {x + 75, kSensorY}, pipeIdle, 2);
        drawSensor(painter, tank.pressureSensor, {x + 10, kSensorY, 110, 58});
        drawPipe(painter, {x + 65, kSensorY + 58}, {x + 65, kOutletValveY}, m_snapshot.valvesOpen[tank.outletValve] ? pipeActive : pipeIdle, 3);
        drawValve(painter, tank.outletValve, {x + 10, kOutletValveY, 110, 54});
        drawPipe(painter, {x + 65, kOutletValveY + 54}, {x + 65, kBusY}, m_snapshot.valvesOpen[tank.outletValve] ? pipeActive : pipeIdle, 3);
    }

    drawBox(painter, {145, kBusY - 22, 2585, 44}, "八路 PCBA 共用压力母管", QColor("#f0f7ff"), QColor("#64748b"));
    drawPipe(painter, {155, kBusY}, {2720, kBusY}, pipeIdle, 5);

    for (int i = 0; i < kChannelCount; ++i) {
        const qreal x = channelX[i];
        const auto &channel = channelSpecs()[i];
        const bool open = m_snapshot.valvesOpen[channel.valve];
        drawPipe(painter, {x + 60, kBusY}, {x + 60, kOutletValveY + 54}, open ? pipeActive : pipeIdle, 3);
        drawValve(painter, channel.valve, {x, kOutletValveY, 120, 54});
        drawPipe(painter, {x + 60, kOutletValveY}, {x + 60, kSensorY + 58}, open ? pipeActive : pipeIdle, 3);
        drawSensor(painter, channel.pressureSensor, {x, kSensorY, 120, 58});
        drawPipe(painter, {x + 60, kSensorY}, {x + 60, kTankY + 76}, open ? pipeActive : pipeIdle, 3);
        drawChannel(painter, i, {x, kTankY, 120, 88});
    }

    drawBox(painter, {1570, 225, 490, 130},
            "主控板 / USB CDC 控制\n状态: " + stateDisplayName(m_snapshot.state) +
                QString("\n链路: %1").arg(m_snapshot.linkMode == LinkMode::UsbCdc ? "USB CDC" : "本地仿真"),
            QColor("#ecfdf5"), QColor("#0f766e"), m_snapshot.remoteControlEnabled);
}

void ArchitectureView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF logical = QPointF(event->position().x(), event->position().y());
    QString text;
    for (auto it = m_hitRects.cbegin(); it != m_hitRects.cend(); ++it) {
        if (mapRect(it.value()).contains(logical)) {
            text = tooltipForKey(it.key());
            break;
        }
    }
    setToolTip(text);
}

void ArchitectureView::toggleBlink()
{
    m_blinkOn = !m_blinkOn;
    update();
}

QRectF ArchitectureView::mapRect(const QRectF &logical) const
{
    const qreal sx = width() / kLogicalWidth;
    const qreal sy = height() / kLogicalHeight;
    const qreal scale = std::min(sx, sy);
    const qreal ox = (width() - kLogicalWidth * scale) / 2.0;
    const qreal oy = (height() - kLogicalHeight * scale) / 2.0;
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
    if (m_snapshot.valvesOpen[valve]) {
        return m_blinkOn ? QColor("#22c55e") : QColor("#bbf7d0");
    }
    return QColor("#e2e8f0");
}

QColor ArchitectureView::tankColor(int tankIndex) const
{
    const int pressure = m_snapshot.pressure001mmHg[tankIndex];
    const int target = to001mmHg(tankSpecs()[tankIndex].targetMmHg);
    const bool close = std::abs(pressure - target) <= to001mmHg(3.0);
    if (close) {
        return m_blinkOn ? QColor("#dbeafe") : QColor("#f8fafc");
    }
    return m_blinkOn ? QColor("#fde68a") : QColor("#fff7ed");
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
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(mapPoint(a), mapPoint(b));
}

void ArchitectureView::drawBox(QPainter &painter, const QRectF &rect, const QString &text, const QColor &fill, const QColor &stroke, bool strong) const
{
    const QRectF r = mapRect(rect);
    painter.setPen(QPen(stroke, strong ? 2.5 : 1.4));
    painter.setBrush(fill);
    painter.drawRoundedRect(r, 7, 7);
    painter.setPen(QColor("#1f2937"));
    QFont f = painter.font();
    f.setPointSizeF(std::max(7.5, r.height() / 7.0));
    f.setBold(strong);
    painter.setFont(f);
    painter.drawText(r.adjusted(5, 3, -5, -3), Qt::AlignCenter | Qt::TextWordWrap, text);
}

void ArchitectureView::drawValve(QPainter &painter, int valve, const QRectF &rect) const
{
    const bool open = valve >= 1 && valve <= kValveCount && m_snapshot.valvesOpen[valve];
    const QColor stroke = open ? QColor("#15803d") : QColor("#64748b");
    drawBox(painter, rect, QString("阀%1\n%2").arg(valve).arg(open ? "开" : "关"), valveColor(valve), stroke, open);
}

void ArchitectureView::drawTank(QPainter &painter, int index, const QRectF &rect) const
{
    const auto &tank = tankSpecs()[index];
    const double pressure = toMmHg(m_snapshot.pressure001mmHg[index]);
    const QString text = QString("%1\n%2 mmHg").arg(tank.name).arg(pressure, 0, 'f', 1);
    drawBox(painter, rect, text, tankColor(index), QColor("#2563eb"), true);
}

void ArchitectureView::drawSensor(QPainter &painter, int sensorNumber, const QRectF &rect) const
{
    const int index = sensorNumber - 1;
    const double pressure = index >= 0 && index < kPressureSensorCount ? toMmHg(m_snapshot.pressure001mmHg[index]) : 0.0;
    drawBox(painter, rect,
            QString("压力检测%1\n%2 mmHg").arg(sensorNumber).arg(pressure, 0, 'f', 1),
            QColor("#fff7ed"), QColor("#ea580c"));
}

void ArchitectureView::drawChannel(QPainter &painter, int channelIndex, const QRectF &rect) const
{
    const auto &status = m_snapshot.channels[channelIndex];
    const QString text = QString("PCBA%1\n%2\n测值 %3")
                             .arg(channelIndex + 1)
                             .arg(status.online ? "在线" : "离线")
                             .arg(status.pressure001mmHg > 0 ? QString("%1mmHg").arg(toMmHg(status.pressure001mmHg), 0, 'f', 1) : "--");
    drawBox(painter, rect, text, channelColor(channelIndex), status.online ? QColor("#0f766e") : QColor("#64748b"), status.online);
}

void ArchitectureView::rebuildHitMap()
{
    for (int i = 0; i < kTankCount; ++i) {
        const qreal x = tankX[i];
        const auto &tank = tankSpecs()[i];
        m_hitRects.insert(QString("valve:%1").arg(tank.inletValve), {x, kInletValveY, 130, 36});
        m_hitRects.insert(QString("tank:%1").arg(i), {x + 10, kTankY, 110, 76});
        m_hitRects.insert(QString("valve:%1").arg(tank.reliefValve), {x + 10, kReliefY, 62, 46});
        m_hitRects.insert(QString("sensor:%1").arg(tank.pressureSensor), {x + 10, kSensorY, 110, 58});
        m_hitRects.insert(QString("valve:%1").arg(tank.outletValve), {x + 10, kOutletValveY, 110, 54});
    }
    for (int i = 0; i < kChannelCount; ++i) {
        const qreal x = channelX[i];
        const auto &channel = channelSpecs()[i];
        m_hitRects.insert(QString("channel:%1").arg(i), {x, kTankY, 120, 88});
        m_hitRects.insert(QString("sensor:%1").arg(channel.pressureSensor), {x, kSensorY, 120, 58});
        m_hitRects.insert(QString("valve:%1").arg(channel.valve), {x, kOutletValveY, 120, 54});
    }
}

QString ArchitectureView::tooltipForKey(const QString &key) const
{
    const QStringList parts = key.split(':');
    if (parts.size() != 2) {
        return {};
    }
    const int number = parts[1].toInt();
    if (parts[0] == "valve") {
        return QString("阀%1: %2").arg(number).arg(m_snapshot.valvesOpen[number] ? "打开" : "关闭");
    }
    if (parts[0] == "tank") {
        const auto &tank = tankSpecs()[number];
        return QString("%1\n目标 %2mmHg\n当前 %3mmHg")
            .arg(tank.name)
            .arg(tank.targetMmHg)
            .arg(toMmHg(m_snapshot.pressure001mmHg[number]), 0, 'f', 1);
    }
    if (parts[0] == "sensor") {
        return QString("压力检测%1: %2mmHg")
            .arg(number)
            .arg(toMmHg(m_snapshot.pressure001mmHg[number - 1]), 0, 'f', 1);
    }
    if (parts[0] == "channel") {
        const auto &channel = m_snapshot.channels[number];
        return QString("PCBA%1\n连接: %2\n测得气压: %3mmHg\n误差: %4mmHg")
            .arg(number + 1)
            .arg(channel.online ? "在线" : "离线")
            .arg(toMmHg(channel.pressure001mmHg), 0, 'f', 1)
            .arg(toMmHg(channel.error001mmHg), 0, 'f', 2);
    }
    return {};
}
