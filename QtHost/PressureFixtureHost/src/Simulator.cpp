#include "Simulator.h"

#include <algorithm>
#include <cmath>

using namespace fixture;

Simulator::Simulator(QObject *parent)
    : QObject(parent)
{
    m_snapshot.linkMode = LinkMode::Simulation;
    m_snapshot.thresholdMmHg = 3.0;
    for (int i = 0; i < kTankCount; ++i) {
        m_snapshot.pressure001mmHg[i] = to001mmHg(tankSpecs()[i].targetMmHg);
    }
    enterState(RuntimeState::Ready);
    m_uptimeTimer.start();
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &Simulator::tick);
    m_timer.start();
}

FixtureSnapshot Simulator::snapshot() const
{
    return m_snapshot;
}

void Simulator::start()
{
    m_snapshot.running = true;
    m_snapshot.paused = false;
    enterState(RuntimeState::PcbaPowerOn);
}

void Simulator::stop()
{
    m_snapshot.running = false;
    m_snapshot.paused = false;
    enterState(RuntimeState::Ready);
}

void Simulator::pause()
{
    m_snapshot.paused = true;
    publish();
}

void Simulator::resume()
{
    m_snapshot.paused = false;
    publish();
}

void Simulator::setState(RuntimeState state)
{
    m_snapshot.running = state != RuntimeState::Ready && state != RuntimeState::UsbMsc;
    m_snapshot.paused = false;
    enterState(state);
}

void Simulator::setThreshold(double thresholdMmHg)
{
    m_snapshot.thresholdMmHg = thresholdMmHg;
    publish();
}

void Simulator::setManualValve(int valveNumber, bool open)
{
    if (valveNumber >= 1 && valveNumber <= kValveCount) {
        m_manualValve = valveNumber;
        m_manualValveOpen = open;
        m_snapshot.valvesOpen[valveNumber] = open;
        publish();
    }
}

void Simulator::tick()
{
    if (!m_snapshot.paused) {
        m_snapshot.elapsedMs = static_cast<uint32_t>(m_stateTimer.elapsed());
        for (int i = 0; i < kTankCount; ++i) {
            const int target = to001mmHg(tankSpecs()[i].targetMmHg);
            const double wave = std::sin((m_uptimeTimer.elapsed() / 1000.0) + i * 0.6) * 250.0;
            m_snapshot.pressure001mmHg[i] = target + static_cast<int>(wave);
        }

        if (m_snapshot.running && m_stateTimer.elapsed() > 2200) {
            int next = stateIndex(m_snapshot.state) + 1;
            if (m_snapshot.state == RuntimeState::Result) {
                next = stateIndex(RuntimeState::Refill);
            } else if (m_snapshot.state == RuntimeState::Refill) {
                next = stateIndex(RuntimeState::Ready);
                m_snapshot.running = false;
            } else if (next >= stateIndex(RuntimeState::Result)) {
                next = stateIndex(RuntimeState::Result);
            }
            enterState(stateFromIndex(next));
            return;
        }
    }
    publish();
}

void Simulator::publish()
{
    applyStateOutputs(m_snapshot, m_snapshot.state);
    if (m_manualValve >= 1 && m_manualValve <= kValveCount) {
        m_snapshot.valvesOpen[m_manualValve] = m_manualValveOpen;
    }
    m_snapshot.sequence++;
    emit snapshotChanged(m_snapshot);
}

void Simulator::enterState(RuntimeState state)
{
    m_stateTimer.restart();
    m_snapshot.state = state;
    m_snapshot.elapsedMs = 0;
    if (state == RuntimeState::Ready) {
        for (auto &channel : m_snapshot.channels) {
            channel = PcbaChannelStatus{};
        }
    }
    publish();
}
