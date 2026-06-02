#include "MainWindow.h"

#include "ArchitectureView.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFont>

using namespace fixture;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("气压检测工装 USB 上位机");
    m_architectureView = new ArchitectureView(this);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(m_architectureView);
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({260, 980, 360});
    setCentralWidget(splitter);

    connect(&m_simulator, &Simulator::snapshotChanged, this, [this](const FixtureSnapshot &snapshot) {
        if (!m_transport.isOpen()) {
            applySnapshot(snapshot);
        }
    });
    connect(&m_transport, &WindowsSerialTransport::bytesReceived, this, &MainWindow::handleSerialBytes);
    connect(&m_transport, &WindowsSerialTransport::errorOccurred, this, &MainWindow::handleSerialError);
    connect(&m_transport, &WindowsSerialTransport::openChanged, this, [this](bool open) {
        m_connectButton->setText(open ? "断开" : "连接");
        m_snapshot.linkMode = open ? LinkMode::UsbCdc : LinkMode::Simulation;
        if (!open) {
            m_simulator.setState(RuntimeState::Ready);
        }
    });

    refreshPorts();
    applySnapshot(m_simulator.snapshot());
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

    m_flowList = new QListWidget(panel);
    m_flowList->addItems(stateDisplayNames());
    connect(m_flowList, &QListWidget::itemDoubleClicked, this, &MainWindow::sendSelectedState);
    layout->addWidget(m_flowList, 1);

    auto *hint = new QLabel("双击流程状态可让上位机请求 MCU 切换状态；未连接 USB 时切换本地仿真。", panel);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #64748b;");
    layout->addWidget(hint);

    return panel;
}

QWidget *MainWindow::buildRightPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);

    auto *usbBox = new QGroupBox("USB CDC 控制", panel);
    auto *usbLayout = new QGridLayout(usbBox);
    m_portCombo = new QComboBox(usbBox);
    auto *refreshButton = new QPushButton("刷新", usbBox);
    m_connectButton = new QPushButton("连接", usbBox);
    usbLayout->addWidget(m_portCombo, 0, 0, 1, 2);
    usbLayout->addWidget(refreshButton, 0, 2);
    usbLayout->addWidget(m_connectButton, 1, 0, 1, 3);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectOrDisconnect);
    layout->addWidget(usbBox);

    auto *controlBox = new QGroupBox("流程控制", panel);
    auto *controlLayout = new QGridLayout(controlBox);
    auto *startButton = new QPushButton("开始", controlBox);
    auto *stopButton = new QPushButton("停止", controlBox);
    auto *pauseButton = new QPushButton("暂停", controlBox);
    auto *resumeButton = new QPushButton("继续", controlBox);
    auto *stateButton = new QPushButton("切到选中状态", controlBox);
    auto *mscButton = new QPushButton("重启到U盘", controlBox);
    controlLayout->addWidget(startButton, 0, 0);
    controlLayout->addWidget(stopButton, 0, 1);
    controlLayout->addWidget(pauseButton, 1, 0);
    controlLayout->addWidget(resumeButton, 1, 1);
    controlLayout->addWidget(stateButton, 2, 0, 1, 2);
    controlLayout->addWidget(mscButton, 3, 0, 1, 2);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::sendStart);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::sendStop);
    connect(pauseButton, &QPushButton::clicked, this, &MainWindow::sendPause);
    connect(resumeButton, &QPushButton::clicked, this, &MainWindow::sendResume);
    connect(stateButton, &QPushButton::clicked, this, &MainWindow::sendSelectedState);
    connect(mscButton, &QPushButton::clicked, this, &MainWindow::sendEnterMsc);
    layout->addWidget(controlBox);

    auto *paramBox = new QGroupBox("阈值与手动阀", panel);
    auto *paramLayout = new QGridLayout(paramBox);
    m_thresholdSpin = new QDoubleSpinBox(paramBox);
    m_thresholdSpin->setRange(0.1, 30.0);
    m_thresholdSpin->setDecimals(2);
    m_thresholdSpin->setValue(3.0);
    m_thresholdSpin->setSuffix(" mmHg");
    auto *thresholdButton = new QPushButton("下发阈值", paramBox);
    m_valveSpin = new QSpinBox(paramBox);
    m_valveSpin->setRange(1, kValveCount);
    m_valveActionCombo = new QComboBox(paramBox);
    m_valveActionCombo->addItems({"关闭", "打开"});
    auto *valveButton = new QPushButton("执行阀门", paramBox);
    paramLayout->addWidget(new QLabel("压力误差阈值"), 0, 0);
    paramLayout->addWidget(m_thresholdSpin, 0, 1);
    paramLayout->addWidget(thresholdButton, 0, 2);
    paramLayout->addWidget(new QLabel("阀号"), 1, 0);
    paramLayout->addWidget(m_valveSpin, 1, 1);
    paramLayout->addWidget(m_valveActionCombo, 1, 2);
    paramLayout->addWidget(valveButton, 2, 0, 1, 3);
    connect(thresholdButton, &QPushButton::clicked, this, &MainWindow::sendThreshold);
    connect(valveButton, &QPushButton::clicked, this, &MainWindow::sendManualValve);
    layout->addWidget(paramBox);

    m_valveTable = new QTableWidget(kValveCount, 2, panel);
    m_valveTable->setHorizontalHeaderLabels({"阀门", "状态"});
    m_valveTable->verticalHeader()->hide();
    m_valveTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_valveTable->setMaximumHeight(150);
    layout->addWidget(m_valveTable);

    m_pressureTable = new QTableWidget(kPressureSensorCount, 2, panel);
    m_pressureTable->setHorizontalHeaderLabels({"压力检测", "mmHg"});
    m_pressureTable->verticalHeader()->hide();
    m_pressureTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pressureTable->setMaximumHeight(170);
    layout->addWidget(m_pressureTable);

    m_pcbaTable = new QTableWidget(kChannelCount, 5, panel);
    m_pcbaTable->setHorizontalHeaderLabels({"通道", "连接", "夹具", "PCBA", "判定"});
    m_pcbaTable->verticalHeader()->hide();
    m_pcbaTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pcbaTable->setMaximumHeight(185);
    layout->addWidget(m_pcbaTable);

    m_log = new QPlainTextEdit(panel);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setPlaceholderText("USB 控制日志");
    layout->addWidget(m_log, 1);

    return panel;
}

void MainWindow::refreshPorts()
{
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();
    const auto ports = WindowsSerialTransport::availablePorts();
    m_portCombo->addItems(ports);
    if (m_portCombo->findText(current) >= 0) {
        m_portCombo->setCurrentText(current);
    }
    appendLog(QString("发现 %1 个 COM 口").arg(ports.size()));
}

void MainWindow::connectOrDisconnect()
{
    if (m_transport.isOpen()) {
        m_transport.close();
        appendLog("已断开 USB CDC，回到本地仿真显示");
        return;
    }
    if (m_portCombo->currentText().isEmpty()) {
        QMessageBox::information(this, "USB 未连接", "当前没有可打开的 COM 口。STM32 USB 接上电脑后点击刷新。");
        return;
    }
    if (m_transport.open(m_portCombo->currentText(), 115200)) {
        appendLog("已打开 " + m_portCombo->currentText());
        sendFrame(usb::buildHello(nextSequence()), "HELLO");
        sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::GetStatus), "GET_STATUS");
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
    appendLog("USB 错误: " + message);
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
                             .arg(m_snapshot.linkMode == LinkMode::UsbCdc ? "USB CDC 联机" : "本地仿真")
                             .arg(m_snapshot.sequence)
                             .arg(m_snapshot.elapsedMs));
    updateFlowList();
    updateTables();
}

void MainWindow::sendStart()
{
    sendFrame(usb::buildStart(nextSequence(), 285), "START 285mmHg");
    dispatchLocalCommand(usb::Start);
}

void MainWindow::sendStop()
{
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Stop), "STOP");
    dispatchLocalCommand(usb::Stop);
}

void MainWindow::sendPause()
{
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Pause), "PAUSE");
    dispatchLocalCommand(usb::Pause);
}

void MainWindow::sendResume()
{
    sendFrame(usb::buildFrame(usb::Request, nextSequence(), usb::Resume), "RESUME");
    dispatchLocalCommand(usb::Resume);
}

void MainWindow::sendSelectedState()
{
    const int row = m_flowList->currentRow();
    const RuntimeState state = stateFromIndex(row);
    sendFrame(usb::buildSetState(nextSequence(), state), "SET_STATE " + stateDisplayName(state));
    dispatchLocalCommand(usb::SetState, QByteArray(1, static_cast<char>(stateIndex(state))));
}

void MainWindow::sendThreshold()
{
    const double threshold = m_thresholdSpin->value();
    sendFrame(usb::buildSetThreshold(nextSequence(), threshold), QString("SET_THRESHOLD %1mmHg").arg(threshold));
    m_simulator.setThreshold(threshold);
}

void MainWindow::sendManualValve()
{
    const int valve = m_valveSpin->value();
    const bool open = m_valveActionCombo->currentIndex() == 1;
    sendFrame(usb::buildManualValve(nextSequence(), static_cast<uint8_t>(valve), open),
              QString("MANUAL_VALVE 阀%1 %2").arg(valve).arg(open ? "打开" : "关闭"));
    m_simulator.setManualValve(valve, open);
}

void MainWindow::sendEnterMsc()
{
    const auto answer = QMessageBox::question(this, "重启到 U 盘维护模式",
                                              "该命令会要求 MCU 关闭输出并重启到 USB MSC U 盘维护模式。继续？");
    if (answer != QMessageBox::Yes) {
        return;
    }
    sendFrame(usb::buildEnterMscReboot(nextSequence()), "ENTER_MSC_REBOOT");
}

void MainWindow::sendFrame(const QByteArray &frame, const QString &description)
{
    if (m_transport.isOpen()) {
        m_transport.writeBytes(frame);
        appendLog("TX " + description);
    } else {
        appendLog("SIM " + description);
    }
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

void MainWindow::updateTables()
{
    for (int valve = 1; valve <= kValveCount; ++valve) {
        m_valveTable->setItem(valve - 1, 0, new QTableWidgetItem(QString("阀%1").arg(valve)));
        m_valveTable->setItem(valve - 1, 1, new QTableWidgetItem(m_snapshot.valvesOpen[valve] ? "打开" : "关闭"));
    }
    for (int sensor = 1; sensor <= kPressureSensorCount; ++sensor) {
        m_pressureTable->setItem(sensor - 1, 0, new QTableWidgetItem(QString("压力检测%1").arg(sensor)));
        m_pressureTable->setItem(sensor - 1, 1, new QTableWidgetItem(QString::number(toMmHg(m_snapshot.pressure001mmHg[sensor - 1]), 'f', 1)));
    }
    for (int i = 0; i < kChannelCount; ++i) {
        const auto &channel = m_snapshot.channels[i];
        m_pcbaTable->setItem(i, 0, new QTableWidgetItem(QString("通道%1").arg(i + 1)));
        m_pcbaTable->setItem(i, 1, new QTableWidgetItem(channel.online ? "在线" : "离线"));
        m_pcbaTable->setItem(i, 2, new QTableWidgetItem(QString::number(toMmHg(channel.fixturePressure001mmHg), 'f', 1)));
        m_pcbaTable->setItem(i, 3, new QTableWidgetItem(channel.pressure001mmHg > 0 ? QString::number(toMmHg(channel.pressure001mmHg), 'f', 1) : "--"));
        m_pcbaTable->setItem(i, 4, new QTableWidgetItem(channel.pressure001mmHg > 0 ? (channel.pass ? "合格" : "不合格") : "--"));
    }
}

void MainWindow::updateFlowList()
{
    const int current = stateIndex(m_snapshot.state);
    if (m_flowList->currentRow() < 0) {
        m_flowList->setCurrentRow(current);
    }
    for (int i = 0; i < m_flowList->count(); ++i) {
        auto *item = m_flowList->item(i);
        QFont font = item->font();
        font.setBold(i == current);
        item->setFont(font);
        item->setText(QString("%1 %2").arg(i == current ? ">" : " ").arg(stateDisplayName(stateFromIndex(i))));
        item->setBackground(i == current ? QColor("#dbeafe") : QColor("#ffffff"));
        item->setForeground(i == current ? QColor("#0f172a") : QColor("#334155"));
    }
}

void MainWindow::dispatchLocalCommand(usb::Command command, const QByteArray &payload)
{
    if (m_transport.isOpen()) {
        return;
    }
    switch (command) {
    case usb::Start:
        m_simulator.start();
        break;
    case usb::Stop:
        m_simulator.stop();
        break;
    case usb::Pause:
        m_simulator.pause();
        break;
    case usb::Resume:
        m_simulator.resume();
        break;
    case usb::SetState:
        if (!payload.isEmpty()) {
            m_simulator.setState(stateFromIndex(static_cast<uint8_t>(payload[0])));
        }
        break;
    default:
        break;
    }
}
