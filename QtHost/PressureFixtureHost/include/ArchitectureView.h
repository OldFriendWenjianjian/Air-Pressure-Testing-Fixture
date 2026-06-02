#pragma once

#include "PressureFixtureModel.h"

#include <QHash>
#include <QRectF>
#include <QTimer>
#include <QWidget>

class ArchitectureView : public QWidget {
    Q_OBJECT

public:
    explicit ArchitectureView(QWidget *parent = nullptr);

    void setSnapshot(const fixture::FixtureSnapshot &snapshot);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void toggleBlink();

private:
    QRectF mapRect(const QRectF &logical) const;
    QPointF mapPoint(const QPointF &logical) const;
    QColor valveColor(int valve) const;
    QColor tankColor(int tankIndex) const;
    QColor channelColor(int channelIndex) const;
    void drawPipe(QPainter &painter, const QPointF &a, const QPointF &b, const QColor &color, qreal width = 3.0) const;
    void drawBox(QPainter &painter, const QRectF &rect, const QString &text, const QColor &fill, const QColor &stroke, bool strong = false) const;
    void drawValve(QPainter &painter, int valve, const QRectF &rect) const;
    void drawTank(QPainter &painter, int index, const QRectF &rect) const;
    void drawSensor(QPainter &painter, int sensorNumber, const QRectF &rect) const;
    void drawChannel(QPainter &painter, int channelIndex, const QRectF &rect) const;
    void rebuildHitMap();
    QString tooltipForKey(const QString &key) const;

    fixture::FixtureSnapshot m_snapshot;
    QTimer m_blinkTimer;
    bool m_blinkOn = true;
    QHash<QString, QRectF> m_hitRects;
};
