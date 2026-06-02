#pragma once

#include "PressureFixtureModel.h"
#include "Simulator.h"
#include "UsbControlProtocol.h"
#include "WindowsSerialTransport.h"

#include <QByteArray>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>

class ArchitectureView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void refreshPorts();
    void connectOrDisconnect();
    void handleSerialBytes(const QByteArray &bytes);
    void handleSerialError(const QString &message);
    void applySnapshot(const fixture::FixtureSnapshot &snapshot);
    void sendStart();
    void sendStop();
    void sendPause();
    void sendResume();
    void sendSelectedState();
    void sendThreshold();
    void sendManualValve();
    void sendEnterMsc();

private:
    QWidget *buildLeftPanel();
    QWidget *buildRightPanel();
    void sendFrame(const QByteArray &frame, const QString &description);
    uint16_t nextSequence();
    void appendLog(const QString &line);
    void updateTables();
    void updateFlowList();
    void dispatchLocalCommand(fixture::usb::Command command, const QByteArray &payload = {});

    ArchitectureView *m_architectureView = nullptr;
    QListWidget *m_flowList = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_linkLabel = nullptr;
    QComboBox *m_portCombo = nullptr;
    QPushButton *m_connectButton = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
    QSpinBox *m_valveSpin = nullptr;
    QComboBox *m_valveActionCombo = nullptr;
    QTableWidget *m_valveTable = nullptr;
    QTableWidget *m_pressureTable = nullptr;
    QTableWidget *m_pcbaTable = nullptr;
    QPlainTextEdit *m_log = nullptr;

    Simulator m_simulator;
    WindowsSerialTransport m_transport;
    QByteArray m_rxBuffer;
    fixture::FixtureSnapshot m_snapshot;
    uint16_t m_sequence = 1;
};
