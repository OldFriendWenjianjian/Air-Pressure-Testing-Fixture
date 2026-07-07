#pragma once

#include "PressureFixtureModel.h"
#include "UsbControlProtocol.h"
#include "WindowsSerialTransport.h"

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QDialog>
#include <QTimer>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <array>

class ArchitectureView;
class QDateTimeEdit;
class QGroupBox;
class QTableWidget;

class SensorCalibrationDialog : public QDialog {
    Q_OBJECT

public:
    explicit SensorCalibrationDialog(int sensorNumber, QWidget *parent = nullptr);
    int sensorNumber() const;
    void setSnapshot(const fixture::FixtureSnapshot &snapshot);

private slots:
    void captureSelectedPoint();
    void saveCalibration();

private:
    int m_sensorNumber = 0;
    fixture::FixtureSnapshot m_snapshot;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTableWidget *m_pointTable = nullptr;
};

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
    void handleModeChanged(int index);
    void selectProductionMode();
    void selectDebugMode();
    void handleLeftItemChanged();
    void sendProductionStart();
    void sendStart();
    void sendStop();
    void sendPause();
    void sendResume();
    void sendSelectedState();
    void sendThreshold();
    void sendManualValve();
    void toggleValveFromDiagram(int valveNumber);
    void openSensorCalibration(int sensorNumber);
    void sendEnterMsc();
    void handleSingleTankSelectionChanged(int index);
    void startSingleTankLoop();
    void stopSingleTankLoop();
    void serviceSingleTankLoop();
    void enterPcbaCurrentTest();
    void startSinglePcbaFlow();
    void requestSinglePcbaTimingReport();
    void startSingleTankPcbaFlow();
    void requestSingleTankPcbaReport();
    void setPcbaCurrent50mAEnabled(bool enabled);
    void handlePcbaSupplyVoltageChanged(int index);
    void sendAdcCalibration();
    void setRtcEditorToComputerTime();
    void sendRtcTime();
    void browseFirmwareHex();
    void startFirmwareDownload();
    void handleFirmwareDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    enum class HostRunMode {
        Production = 0,
        Debug = 1
    };

    enum class LeftItemKind {
        RuntimeState = 0,
        DebugTool = 1
    };

    enum class DebugTool {
        UsbMsc = 0,
        PcbaCurrent,
        SinglePcbaFlow,
        SingleTankPcba,
        SingleTank,
        ManualValve,
        AdcReference,
        RtcDebug,
        FirmwareDownload
    };

    QWidget *buildLeftPanel();
    QWidget *buildRightPanel();
    HostRunMode currentMode() const;
    bool isDebugMode() const;
    void updateModeUi();
    void updateSingleTankPanel();
    void rebuildFlowList();
    fixture::RuntimeState selectedFlowState() const;
    DebugTool selectedDebugTool() const;
    void showDebugTool(DebugTool tool);
    bool activateSelectedLeftItem();
    bool sendFrame(const QByteArray &frame, const QString &description);
    bool sendManualValveCommand(int valveNumber, bool open, const QString &description);
    bool sendValveMaskCommand(uint32_t valveMask, uint32_t openMask, const QString &description);
    void resetSingleTankLoopControl();
    bool sendSingleTankLoopCommand(uint8_t tankIndex,
                                   double targetMmHg,
                                   double toleranceMmHg,
                                   bool enable,
                                   const QString &description);
    void resetConnectAttempts();
    void buildConnectCandidates(const QString &selectedPortName);
    bool tryOpenNextConnectCandidate();
    uint16_t nextSequence();
    void appendLog(const QString &line);
    void updateTables();
    void updateSinglePcbaTimingTable();
    void updateSingleTankPcbaTable();
    void refreshStatusTablesVisibility();
    void updateFlowList();
    void updateCalibrationDialog();
    QString defaultFirmwareHexPath() const;
    QString defaultJLinkPath() const;
    void appendFirmwareLog(const QString &line);

    ArchitectureView *m_architectureView = nullptr;
    QListWidget *m_flowList = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_linkLabel = nullptr;
    QLabel *m_modeHintLabel = nullptr;
    QLabel *m_flowHintLabel = nullptr;
    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QTabWidget *m_modeTabs = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_productionModeButton = nullptr;
    QPushButton *m_debugModeButton = nullptr;
    QGroupBox *m_productionBox = nullptr;
    QGroupBox *m_debugFlowBox = nullptr;
    QGroupBox *m_debugSinglePcbaBox = nullptr;
    QGroupBox *m_debugSingleTankPcbaBox = nullptr;
    QGroupBox *m_debugSingleTankBox = nullptr;
    QGroupBox *m_debugManualBox = nullptr;
    QGroupBox *m_debugCurrentBox = nullptr;
    QGroupBox *m_debugAdcBox = nullptr;
    QGroupBox *m_debugRtcBox = nullptr;
    QGroupBox *m_debugFirmwareBox = nullptr;
    QComboBox *m_singleTankCombo = nullptr;
    QDoubleSpinBox *m_singleTankTargetSpin = nullptr;
    QDoubleSpinBox *m_singleTankToleranceSpin = nullptr;
    QPushButton *m_singleTankStartButton = nullptr;
    QPushButton *m_singleTankStopButton = nullptr;
    QLabel *m_singleTankStatusLabel = nullptr;
    QLabel *m_singlePcbaStatusLabel = nullptr;
    QLabel *m_singlePcbaSummaryLabel = nullptr;
    QCheckBox *m_singlePcbaStopOnFailCheck = nullptr;
    QTableWidget *m_singlePcbaCommandTable = nullptr;
    QPlainTextEdit *m_singlePcbaSerialLog = nullptr;
    QLabel *m_singleTankPcbaStatusLabel = nullptr;
    QLabel *m_singleTankPcbaSummaryLabel = nullptr;
    QTableWidget *m_singleTankPcbaTable = nullptr;
    QPlainTextEdit *m_singleTankPcbaSerialLog = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
    QSpinBox *m_valveSpin = nullptr;
    QComboBox *m_valveActionCombo = nullptr;
    QTableWidget *m_valveTable = nullptr;
    QTableWidget *m_pressureTable = nullptr;
    QTableWidget *m_pcbaTable = nullptr;
    QTableWidget *m_pcbaCurrentTable = nullptr;
    QCheckBox *m_pcbaCurrent50mACheck = nullptr;
    QComboBox *m_pcbaSupplyVoltageCombo = nullptr;
    QLabel *m_pcbaCurrentStatusLabel = nullptr;
    QLabel *m_adcReferenceStatusLabel = nullptr;
    QLabel *m_adcReferenceVddaLabel = nullptr;
    QLabel *m_adcReferenceRawLabel = nullptr;
    QLabel *m_adcReferenceScaleLabel = nullptr;
    QLabel *m_rtcTimeLabel = nullptr;
    QLabel *m_rtcBatteryLabel = nullptr;
    QLabel *m_rtcOscillatorLabel = nullptr;
    QDateTimeEdit *m_rtcDateTimeEdit = nullptr;
    QLineEdit *m_firmwareHexEdit = nullptr;
    QLineEdit *m_jlinkPathEdit = nullptr;
    QLabel *m_firmwareStatusLabel = nullptr;
    QPlainTextEdit *m_firmwareLog = nullptr;
    QPushButton *m_firmwareDownloadButton = nullptr;
    QPlainTextEdit *m_log = nullptr;
    SensorCalibrationDialog *m_calibrationDialog = nullptr;

    WindowsSerialTransport m_transport;
    QProcess m_firmwareProcess;
    QTimer m_handshakeTimer;
    QTimer m_singleTankTimer;
    QTimer m_singlePcbaTimingPollTimer;
    QTimer m_singleTankPcbaPollTimer;
    QByteArray m_rxBuffer;
    int m_rxDiscardBurstCount = 0;
    fixture::FixtureSnapshot m_snapshot;
    uint16_t m_sequence = 1;
    bool m_waitingForHello = false;
    bool m_firmwareDownloadRunning = false;
    bool m_firmwareDownloadSawError = false;
    QString m_firmwareCommandFile;
    bool m_singleTankRunning = false;
    QStringList m_connectCandidatePorts;
    QStringList m_connectCandidateDisplays;
    QStringList m_deferredConnectPorts;
    QStringList m_deferredConnectDisplays;
    int m_connectCandidateIndex = -1;
    int m_connectDeferredRound = 0;
    int m_helloRetryCount = 0;
    bool m_singleTankAwaitingReady = false;
    bool m_singlePcbaStartPending = false;
    bool m_singlePcbaTimingRunning = false;
    fixture::PcbaTimingReport m_singlePcbaTimingReport;
    bool m_singleTankPcbaStartPending = false;
    bool m_singleTankPcbaRunning = false;
    fixture::SingleTankPcbaReport m_singleTankPcbaReport;
};
