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

QString debugToolDisplayName(int tool)
{
    switch (tool) {
    case 0: return "U盘维护模式";
    case 1: return "PCBA电流测试";
    case 2: return "单罐体闭环测试";
    case 3: return "阈值与手动阀";
    case 4: return "ADC实时基准";
    case 5: return "RTC时钟调试模式";
    case 6: return "固件烧录";
    default: return "未知调试项";
    }
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
    const bool valid = pressureSensorValid(m_snapshot, index);
    m_statusLabel->setText(QString("连接: %1 | 当前读数: %2")
                               .arg(valid ? "已连接" : "未连接")
                               .arg(sensorPressureText(m_snapshot, index, 2, true)));
    m_statusLabel->setStyleSheet(valid ? "color: #0f766e;" : "color: #b45309;");
}

void SensorCalibrationDialog::captureSelectedPoint()
{
    const int index = m_sensorNumber - 1;
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
    setWindowTitle("气压检测工装 J-Link RTT 上位机");
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
                resetSingleTankCommandCache();
                updateSingleTankPanel();
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
            appendLog("HELLO 未收到响应，重试");
            sendFrame(usb::buildHello(nextSequence()), "HELLO");
            m_handshakeTimer.start(1500);
        }
    });
    m_singleTankTimer.setInterval(400);
    connect(&m_singleTankTimer, &QTimer::timeout, this, &MainWindow::serviceSingleTankLoop);

    refreshPorts();
    FixtureSnapshot initialSnapshot;
    initialSnapshot.linkMode = LinkMode::Disconnected;
    applySnapshot(initialSnapshot);
    updateModeUi();
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

    auto *usbBox = new QGroupBox("J-Link RTT / 串口备用", panel);
    auto *usbLayout = new QGridLayout(usbBox);
    m_portCombo = new QComboBox(usbBox);
    m_baudCombo = new QComboBox(usbBox);
    m_baudCombo->addItems({"9600", "115200"});
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
    connect(m_pcbaCurrent50mACheck, &QCheckBox::toggled, this, &MainWindow::setPcbaCurrent50mAEnabled);
    debugPageLayout->addWidget(m_debugCurrentBox);

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
            m_singleTankStatusLabel->setText(QString("%1 | 入口阀%2 / 出口阀%3 / 泄压阀%4 / 压力检测%5")
                                                 .arg(tank.name)
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

void MainWindow::resetSingleTankCommandCache()
{
    m_singleTankCommandKnown.fill(false);
    m_singleTankCommandOpen.fill(false);
}

void MainWindow::commandSingleTankValve(int valveNumber, bool open, const QString &reason, bool force)
{
    if (valveNumber < 1 || valveNumber > kValveCount) {
        return;
    }

    if (!force &&
        m_singleTankCommandKnown[valveNumber] &&
        m_singleTankCommandOpen[valveNumber] == open) {
        return;
    }

    const QString description = QString("单罐闭环 %1 阀%2 %3")
                                    .arg(reason)
                                    .arg(valveNumber)
                                    .arg(open ? "打开" : "关闭");
    if (sendManualValveCommand(valveNumber, open, description)) {
        m_singleTankCommandKnown[valveNumber] = true;
        m_singleTankCommandOpen[valveNumber] = open;
    }
}

void MainWindow::closeSingleTankValves(const TankSpec &tank, const QString &reason, bool force)
{
    commandSingleTankValve(tank.inletValve, false, reason, force);
    commandSingleTankValve(tank.reliefValve, false, reason, force);
    commandSingleTankValve(tank.outletValve, false, reason, force);
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

    resetSingleTankCommandCache();
    m_singleTankRunning = true;
    updateSingleTankPanel();

    const auto &tank = tankSpecs()[index];
    appendLog(QString("单罐闭环启动: %1 目标 %2mmHg 容差 %3mmHg")
                  .arg(tank.name)
                  .arg(m_singleTankTargetSpin->value(), 0, 'f', 1)
                  .arg(m_singleTankToleranceSpin->value(), 0, 'f', 1));
    closeSingleTankValves(tank, "启动隔离", true);
    serviceSingleTankLoop();
    m_singleTankTimer.start();
}

void MainWindow::stopSingleTankLoop()
{
    const bool wasRunning = m_singleTankRunning;
    m_singleTankTimer.stop();

    const int index = m_singleTankCombo ? m_singleTankCombo->currentIndex() : -1;
    if (wasRunning && index >= 0 && index < kTankCount) {
        closeSingleTankValves(tankSpecs()[index], "停止", true);
        appendLog(QString("单罐闭环停止: %1").arg(tankSpecs()[index].name));
    }

    m_singleTankRunning = false;
    resetSingleTankCommandCache();
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
        resetSingleTankCommandCache();
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
    commandSingleTankValve(tank.outletValve, false, "隔离出口");

    if (!pressureSensorValid(m_snapshot, sensorIndex)) {
        commandSingleTankValve(tank.inletValve, false, "等待压力");
        commandSingleTankValve(tank.reliefValve, false, "等待压力");
        m_singleTankStatusLabel->setText(QString("%1 | 压力检测%2无有效读数，已关闭入口/泄压/出口阀")
                                             .arg(tank.name)
                                             .arg(tank.pressureSensor));
        return;
    }

    const double current = toMmHg(m_snapshot.pressure001mmHg[sensorIndex]);
    const double target = m_singleTankTargetSpin->value();
    const double tolerance = m_singleTankToleranceSpin->value();
    QString action;

    if (current < target - tolerance) {
        commandSingleTankValve(tank.reliefValve, false, "补气");
        commandSingleTankValve(tank.inletValve, true, "补气");
        action = "补气";
    } else if (current > target + tolerance) {
        commandSingleTankValve(tank.inletValve, false, "泄压修正");
        commandSingleTankValve(tank.reliefValve, true, "泄压修正");
        action = "泄压修正";
    } else {
        commandSingleTankValve(tank.inletValve, false, "目标保持");
        commandSingleTankValve(tank.reliefValve, false, "目标保持");
        action = "目标窗口内保持";
    }

    m_singleTankStatusLabel->setText(QString("%1 | 当前 %2 mmHg / 目标 %3±%4 mmHg | %5")
                                         .arg(tank.name)
                                         .arg(current, 0, 'f', 1)
                                         .arg(target, 0, 'f', 1)
                                         .arg(tolerance, 0, 'f', 1)
                                         .arg(action));
}

void MainWindow::refreshPorts()
{
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();
    const auto ports = WindowsSerialTransport::availablePortInfos();
    int preferredIndex = -1;
    for (const auto &port : ports) {
        m_portCombo->addItem(port.displayName, port.portName);
        if (port.displayName == current || port.portName == current) {
            preferredIndex = m_portCombo->count() - 1;
        }
        if (preferredIndex < 0 && port.isRtt) {
            preferredIndex = m_portCombo->count() - 1;
        } else if (preferredIndex < 0 && port.isSegger) {
            preferredIndex = m_portCombo->count() - 1;
        }
    }
    if (preferredIndex >= 0) {
        m_portCombo->setCurrentIndex(preferredIndex);
    }
    appendLog(QString("发现 %1 个连接入口").arg(ports.size()));
}

void MainWindow::connectOrDisconnect()
{
    if (m_transport.isOpen()) {
        m_transport.close();
        return;
    }
    if (m_portCombo->currentData().toString().isEmpty()) {
        QMessageBox::information(this, "未找到连接入口", "当前没有可打开的 J-Link RTT 或 COM 口。请确认 J-Link 已接入电脑。");
        return;
    }
    const QString portName = m_portCombo->currentData().toString();
    const int baudRate = m_baudCombo->currentText().toInt();
    if (m_transport.open(portName, baudRate)) {
        m_rxBuffer.clear();
        m_waitingForHello = true;
        appendLog(QString("已打开 %1").arg(m_portCombo->currentText()));
        sendFrame(usb::buildHello(nextSequence()), "HELLO");
        m_handshakeTimer.start(1500);
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
        if (parsed.consumed > 0) {
            m_rxBuffer.remove(0, static_cast<int>(parsed.consumed));
        }
        if (!parsed.ok) {
            if (!parsed.error.isEmpty()) {
                appendLog("RX 丢弃: " + parsed.error);
            }
            continue;
        }
        appendLog("RX " + usb::frameSummary(parsed.frame));
        if (parsed.frame.command == usb::Hello) {
            m_waitingForHello = false;
            m_handshakeTimer.stop();
            sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::GetStatus), "GET_STATUS");
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
    }
}

void MainWindow::handleSerialError(const QString &message)
{
    appendLog("连接错误: " + message);
}

void MainWindow::applySnapshot(const FixtureSnapshot &snapshot)
{
    m_snapshot = snapshot;
    if (m_transport.isOpen()) {
        m_snapshot.linkMode = LinkMode::UsbCdc;
    }
    m_architectureView->setSnapshot(m_snapshot);
    m_stateLabel->setText(stateDisplayName(m_snapshot.state));
    m_linkLabel->setText(QString("%1 | seq %2 | elapsed %3ms")
                             .arg(m_snapshot.linkMode == LinkMode::UsbCdc ? "J-Link RTT 联机" : "未连接")
                             .arg(m_snapshot.sequence)
                             .arg(m_snapshot.elapsedMs));
    updateFlowList();
    updateTables();
    updateCalibrationDialog();
    if (m_singleTankRunning) {
        serviceSingleTankLoop();
    }
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
    sendFrame(usb::buildSetPcbaCurrentRange(nextSequence(), enable50mA),
              QString("SET_PCBA_CURRENT_RANGE %1").arg(enable50mA ? "50mA" : "uA"));
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
        m_pressureTable->setItem(sensor - 1, 0, new QTableWidgetItem(QString("压力检测%1").arg(sensor)));
        m_pressureTable->setItem(sensor - 1, 1, new QTableWidgetItem(sensorPressureText(m_snapshot, sensor - 1)));
    }
    for (int i = 0; i < kChannelCount; ++i) {
        const auto &channel = m_snapshot.channels[i];
        const bool fixturePressureValid = pressureSensorValid(m_snapshot, 6 + i);
        const bool pcbaPressureValid = channel.online && channel.pressure001mmHg > 0;
        m_pcbaTable->setItem(i, 0, new QTableWidgetItem(QString("通道%1").arg(i + 1)));
        m_pcbaTable->setItem(i, 1, new QTableWidgetItem(channel.online ? "在线" : "离线"));
        m_pcbaTable->setItem(i, 2, new QTableWidgetItem(formatPressure001mmHg(channel.fixturePressure001mmHg, fixturePressureValid)));
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
                                              .arg(pcbaCurrent50mA
                                                       ? "mA模式，PB1共享低阻支路已打开"
                                                       : "uA模式，PB1共享低阻支路已关闭")
                                              .arg(validRealtimeCount)
                                              .arg(kChannelCount));
    }

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
                            (tool == DebugTool::RtcDebug && m_snapshot.state == RuntimeState::RtcDebug);
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
