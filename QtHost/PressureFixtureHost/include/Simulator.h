#pragma once

#include "PressureFixtureModel.h"

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>

class Simulator : public QObject {
    Q_OBJECT

public:
    explicit Simulator(QObject *parent = nullptr);

    fixture::FixtureSnapshot snapshot() const;

public slots:
    void start();
    void stop();
    void pause();
    void resume();
    void setState(fixture::RuntimeState state);
    void setThreshold(double thresholdMmHg);
    void setManualValve(int valveNumber, bool open);

signals:
    void snapshotChanged(const fixture::FixtureSnapshot &snapshot);

private slots:
    void tick();

private:
    void publish();
    void enterState(fixture::RuntimeState state);

    fixture::FixtureSnapshot m_snapshot;
    QTimer m_timer;
    QElapsedTimer m_stateTimer;
    QElapsedTimer m_uptimeTimer;
    int m_manualValve = 0;
    bool m_manualValveOpen = false;
};
