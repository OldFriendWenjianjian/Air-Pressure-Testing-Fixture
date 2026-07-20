#pragma once

#include "PressureFixtureModel.h"
#include "UsbControlProtocol.h"
#include "WindowsSerialTransport.h"

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <array>

class ArchitectureView;
class QDateTimeEdit;
class QGroupBox;
class QTableWidget;
class QVBoxLayout;
#ifdef PRESSURE_FIXTURE_HOST_TEST_ACCESS
class MainWindowWatchdogSmokeAccessor;
class SensorCalibrationUiSmokeAccessor;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    void enterSensorCalibration();
    void exitSensorCalibration();
    void startSensorCalibrationFill();
    void startSensorCalibrationRelease();
    void stopSensorCalibrationJog();
    void serviceSensorCalibrationJog();
    void startSensorCalibrationVent();
    void cancelSensorCalibrationVent();
    void saveSensorCalibration();
    void resetSensorCalibrationSession();
    void clearSensorCalibrationSlot();
    void handleSensorCalibrationSlotChanged(int index);
    void requestSensorCalibrationStatus();
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
    void openSingleTankLogFolder();
    void openSingleTankPcbaLogFolder();
    void browseFirmwareHex();
    void startFirmwareDownload();
    void handleFirmwareDownloadFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
#ifdef PRESSURE_FIXTURE_HOST_TEST_ACCESS
    friend class MainWindowWatchdogSmokeAccessor;
    friend class SensorCalibrationUiSmokeAccessor;
#endif

    struct CurrentChartWidgets {
        QLabel *summaryLabel = nullptr;
        QChartView *view = nullptr;
        QLineSeries *samplesSeries = nullptr;
        QLineSeries *averageSeries = nullptr;
        QValueAxis *axisX = nullptr;
        QValueAxis *axisY = nullptr;
    };

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
        SensorCalibration,
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
    void updateSensorCalibrationPanel();
    void recordSensorCalibrationPoint(int point);
    bool sendSensorCalibrationAction(const QByteArray &frame, const QString &description);
    void setSensorCalibrationCapability(bool supported);
    void selectSensorCalibrationTool();
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
    void autoConnectFixtureUsbCdc();
    bool tryOpenNextConnectCandidate();
    uint16_t nextSequence();
    void appendLog(const QString &line);
    QString projectRootPath() const;
    QString defaultSingleTankLogDir() const;
    void updateSingleTankLogUi();
    bool startSingleTankLogSession();
    void stopSingleTankLogSession(const QString &reason);
    void writeSingleTankLogHeader();
    void recordSingleTankSnapshot(const fixture::FixtureSnapshot &previous, const QString &extraEvent = QString());
    QString defaultSingleTankPcbaLogDir() const;
    void updateSingleTankPcbaLogUi();
    bool startSingleTankPcbaLogSession();
    void stopSingleTankPcbaLogSession(const QString &reason);
    void writeSingleTankPcbaSnapshotHeader();
    void writeSingleTankPcbaStepHeader();
    void recordSingleTankPcbaSnapshot(const fixture::FixtureSnapshot &previous,
                                      const QString &extraEvent = QString());
    void recordSingleTankPcbaReport(const fixture::SingleTankPcbaReport &report);
    void updateTables();
    void updatePcbaCurrentChart();
    void initializeCurrentChartWidgets(CurrentChartWidgets &widgets,
                                       QWidget *parent,
                                       QVBoxLayout *layout,
                                       const QString &title);
    void updateCurrentChartWidgets(CurrentChartWidgets &widgets,
                                   const QString &title,
                                   const QString &waitingText,
                                   const std::array<uint32_t, fixture::kCurrentSampleCount> &samplesUaX100,
                                   uint32_t averageUaX100,
                                   uint32_t varianceUa2,
                                   bool valid,
                                   bool displayAsMa);
    void updateSinglePcbaCurrentCharts();
    void updateSingleTankPcbaCurrentCharts();
    void updateSinglePcbaTimingTable();
    void updateSingleTankPcbaTable();
    void setSingleTankPcbaProfileControlsEnabled(bool enabled);
    void refreshStatusTablesVisibility();
    void updateFlowList();
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
    QGroupBox *m_debugSensorCalibrationBox = nullptr;
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
    QCheckBox *m_singleTankLogCheck = nullptr;
    QLabel *m_singleTankStatusLabel = nullptr;
    QLabel *m_singleTankLogStatusLabel = nullptr;
    QLabel *m_sensorCalibrationCapabilityLabel = nullptr;
    QLabel *m_sensorCalibrationLiveLabel = nullptr;
    QLabel *m_sensorCalibrationStateLabel = nullptr;
    QLabel *m_sensorCalibrationSlotLabel = nullptr;
    QLabel *m_sensorCalibrationSlotTitleLabel = nullptr;
    QComboBox *m_sensorCalibrationModeCombo = nullptr;
    QComboBox *m_sensorCalibrationSlotCombo = nullptr;
    QPushButton *m_sensorCalibrationEnterButton = nullptr;
    QPushButton *m_sensorCalibrationExitButton = nullptr;
    QPushButton *m_sensorCalibrationFillButton = nullptr;
    QPushButton *m_sensorCalibrationReleaseButton = nullptr;
    QPushButton *m_sensorCalibrationVentButton = nullptr;
    QPushButton *m_sensorCalibrationCancelVentButton = nullptr;
    QPushButton *m_sensorCalibrationSaveButton = nullptr;
    QPushButton *m_sensorCalibrationResetButton = nullptr;
    QPushButton *m_sensorCalibrationClearSlotButton = nullptr;
    QTableWidget *m_sensorCalibrationPointTable = nullptr;
    std::array<QDoubleSpinBox *, 4> m_sensorCalibrationActualSpins{};
    std::array<QPushButton *, 4> m_sensorCalibrationRecordButtons{};
    QLabel *m_singlePcbaStatusLabel = nullptr;
    QLabel *m_singlePcbaSummaryLabel = nullptr;
    QCheckBox *m_singlePcbaStopOnFailCheck = nullptr;
    CurrentChartWidgets m_singlePcbaStandbyChart;
    CurrentChartWidgets m_singlePcbaWorkChart;
    QTableWidget *m_singlePcbaCommandTable = nullptr;
    QPlainTextEdit *m_singlePcbaSerialLog = nullptr;
    QLabel *m_singleTankPcbaStatusLabel = nullptr;
    QLabel *m_singleTankPcbaSummaryLabel = nullptr;
    QLabel *m_singleTankPcbaLogStatusLabel = nullptr;
    QCheckBox *m_singleTankPcbaContinueOnFailCheck = nullptr;
    QDoubleSpinBox *m_singleTankPcbaMaxDeviationSpin = nullptr;
    QSpinBox *m_singleTankPcbaTrendWindowSpin = nullptr;
    QDoubleSpinBox *m_singleTankPcbaMaxDropRateSpin = nullptr;
    CurrentChartWidgets m_singleTankPcbaStandbyChart;
    CurrentChartWidgets m_singleTankPcbaWorkChart;
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
    QComboBox *m_pcbaCurrentChartChannelCombo = nullptr;
    QLabel *m_pcbaCurrentStatusLabel = nullptr;
    QLabel *m_pcbaCurrentChartSummaryLabel = nullptr;
    QChartView *m_pcbaCurrentChartView = nullptr;
    QLineSeries *m_pcbaCurrentSeries = nullptr;
    QLineSeries *m_pcbaCurrentAverageSeries = nullptr;
    QValueAxis *m_pcbaCurrentAxisX = nullptr;
    QValueAxis *m_pcbaCurrentAxisY = nullptr;
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

    WindowsSerialTransport m_transport;
    QProcess m_firmwareProcess;
    QTimer m_handshakeTimer;
    QTimer m_singleTankTimer;
    QTimer m_singlePcbaTimingPollTimer;
    QTimer m_singleTankPcbaPollTimer;
    QTimer m_sensorCalibrationJogTimer;
    QTimer m_sensorCalibrationPollTimer;
    QByteArray m_rxBuffer;
    int m_rxDiscardBurstCount = 0;
    fixture::FixtureSnapshot m_snapshot;
    uint16_t m_sequence = 1;
    bool m_waitingForHello = false;
    bool m_firmwareDownloadRunning = false;
    bool m_firmwareDownloadSawError = false;
    QString m_firmwareCommandFile;
    bool m_singleTankRunning = false;
    bool m_singleTankLogActive = false;
    qint64 m_singleTankLogLastWriteMs = 0;
    QString m_singleTankLogDirPath;
    QString m_singleTankLogFilePath;
    QFile m_singleTankLogFile;
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
    bool m_singleTankPcbaContinueOnFail = true;
    bool m_singleTankPcbaLogActive = false;
    uint8_t m_singleTankPcbaLoggedStepCount = 0;
    QString m_singleTankPcbaLogDirPath;
    QString m_singleTankPcbaSessionDirPath;
    QFile m_singleTankPcbaSessionLogFile;
    QFile m_singleTankPcbaSnapshotLogFile;
    QFile m_singleTankPcbaStepLogFile;
    fixture::SingleTankPcbaReport m_singleTankPcbaReport;
    fixture::SensorCalibrationStatus m_sensorCalibrationStatus;
    bool m_sensorCalibrationCapabilityKnown = false;
    bool m_sensorCalibrationSupported = false;
    uint8_t m_sensorCalibrationJogActuator = fixture::usb::SensorCalibrationStop;
    int m_sensorCalibrationPendingRecordPoint = -1;
    uint32_t m_sensorCalibrationPendingRecordActual001mmHg = 0;
    bool m_sensorCalibrationPendingRecordAcknowledged = false;
#ifdef PRESSURE_FIXTURE_HOST_TEST_ACCESS
    bool m_sensorCalibrationTestConnected = false;
#endif
};
