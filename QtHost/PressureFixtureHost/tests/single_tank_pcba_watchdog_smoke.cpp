#include <QApplication>
#include <QColor>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFile>
#include <QGroupBox>
#include <QLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>

#include "MainWindow.h"
#include "UsbControlProtocol.h"

#include <cstdio>

using namespace fixture;

class MainWindowWatchdogSmokeAccessor {
public:
    static void feed(MainWindow &window, const QByteArray &bytes)
    {
        window.handleSerialBytes(bytes);
    }

    static const SingleTankPcbaReport &report(const MainWindow &window)
    {
        return window.m_singleTankPcbaReport;
    }

    static QTableWidget *table(MainWindow &window)
    {
        return window.m_singleTankPcbaTable;
    }

    static QLabel *summaryLabel(MainWindow &window)
    {
        return window.m_singleTankPcbaSummaryLabel;
    }

    static QCheckBox *continueOnFailCheck(MainWindow &window)
    {
        return window.m_singleTankPcbaContinueOnFailCheck;
    }

    static QDoubleSpinBox *maxDeviationSpin(MainWindow &window)
    {
        return window.m_singleTankPcbaMaxDeviationSpin;
    }

    static QSpinBox *trendWindowSpin(MainWindow &window)
    {
        return window.m_singleTankPcbaTrendWindowSpin;
    }

    static QDoubleSpinBox *maxDropRateSpin(MainWindow &window)
    {
        return window.m_singleTankPcbaMaxDropRateSpin;
    }

    static void setProfileEnabled(MainWindow &window, bool enabled)
    {
        window.setSingleTankPcbaProfileControlsEnabled(enabled);
    }

    static void markFlowStarting(MainWindow &window)
    {
        window.m_singleTankPcbaStartPending = true;
        window.m_singleTankPcbaRunning = true;
        window.setSingleTankPcbaProfileControlsEnabled(false);
    }

    static bool flowStartPending(const MainWindow &window)
    {
        return window.m_singleTankPcbaStartPending;
    }

    static bool flowRunning(const MainWindow &window)
    {
        return window.m_singleTankPcbaRunning;
    }

    static bool logActive(const MainWindow &window)
    {
        return window.m_singleTankPcbaLogActive;
    }

    static void startFlow(MainWindow &window)
    {
        window.startSingleTankPcbaFlow();
    }

    static QGroupBox *panel(MainWindow &window)
    {
        return window.m_debugSingleTankPcbaBox;
    }

    static bool startLog(MainWindow &window)
    {
        return window.startSingleTankPcbaLogSession();
    }

    static void stopLog(MainWindow &window, const QString &reason)
    {
        window.stopSingleTankPcbaLogSession(reason);
    }

    static QString logDir(const MainWindow &window)
    {
        return window.m_singleTankPcbaSessionDirPath;
    }

    static void applySnapshot(MainWindow &window, const FixtureSnapshot &snapshot)
    {
        window.applySnapshot(snapshot);
    }
};

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("PRESSURE_FIXTURE_UI_SCREENSHOT")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    QTemporaryDir logRoot;
    if (!logRoot.isValid()) {
        std::fprintf(stderr, "failed to create temporary log root\n");
        return 7;
    }
    qputenv("PRESSURE_FIXTURE_LOG_ROOT", logRoot.path().toUtf8());
    MainWindow window;
    auto putU32 = [](QByteArray &bytes, qsizetype offset, uint32_t value) {
        bytes[offset + 0] = static_cast<char>(value & 0xFFu);
        bytes[offset + 1] = static_cast<char>((value >> 8) & 0xFFu);
        bytes[offset + 2] = static_cast<char>((value >> 16) & 0xFFu);
        bytes[offset + 3] = static_cast<char>((value >> 24) & 0xFFu);
    };
    auto *continueOnFailCheck = MainWindowWatchdogSmokeAccessor::continueOnFailCheck(window);
    if (!continueOnFailCheck || !continueOnFailCheck->isChecked()) {
        std::fprintf(stderr, "single tank PCBA continue-on-fail checkbox default is invalid\n");
        return 2;
    }
    auto *maxDeviationSpin = MainWindowWatchdogSmokeAccessor::maxDeviationSpin(window);
    auto *trendWindowSpin = MainWindowWatchdogSmokeAccessor::trendWindowSpin(window);
    auto *maxDropRateSpin = MainWindowWatchdogSmokeAccessor::maxDropRateSpin(window);
    if (!maxDeviationSpin || !trendWindowSpin || !maxDropRateSpin ||
        maxDeviationSpin->value() != 0.50 ||
        maxDeviationSpin->minimum() != 0.05 ||
        maxDeviationSpin->maximum() != 5.0 ||
        maxDeviationSpin->decimals() != 2 ||
        trendWindowSpin->value() != 3000 ||
        trendWindowSpin->minimum() != 1000 ||
        trendWindowSpin->maximum() != 20000 ||
        maxDropRateSpin->value() != 3.0 ||
        maxDropRateSpin->minimum() != 0.05 ||
        maxDropRateSpin->maximum() != 10.0 ||
        maxDropRateSpin->decimals() != 2) {
        std::fprintf(stderr, "single tank PCBA pressure profile defaults are invalid\n");
        return 12;
    }
    QStringList panelTexts;
    const auto panelLabels = MainWindowWatchdogSmokeAccessor::panel(window)->findChildren<QLabel *>();
    for (const QLabel *label : panelLabels) {
        panelTexts << label->text();
    }
    const QString panelText = panelTexts.join(' ');
    if (!panelText.contains(QStringLiteral("趋势最大偏差")) ||
        !panelText.contains(QStringLiteral("趋势观察时间")) ||
        !panelText.contains(QStringLiteral("最大允许下降率")) ||
        !panelText.contains(QStringLiteral("测试结束后")) ||
        panelText.contains(QStringLiteral("闭环超调容差"))) {
        std::fprintf(stderr, "single tank PCBA trend profile labels are invalid\n");
        return 21;
    }
    MainWindowWatchdogSmokeAccessor::setProfileEnabled(window, false);
    if (continueOnFailCheck->isEnabled() || maxDeviationSpin->isEnabled() ||
        trendWindowSpin->isEnabled() || maxDropRateSpin->isEnabled()) {
        std::fprintf(stderr, "single tank PCBA pressure profile did not lock as one unit\n");
        return 13;
    }
    MainWindowWatchdogSmokeAccessor::setProfileEnabled(window, true);
    if (!continueOnFailCheck->isEnabled() || !maxDeviationSpin->isEnabled() ||
        !trendWindowSpin->isEnabled() || !maxDropRateSpin->isEnabled()) {
        std::fprintf(stderr, "single tank PCBA pressure profile did not unlock as one unit\n");
        return 14;
    }
    const QString screenshotPath = QString::fromLocal8Bit(qgetenv("PRESSURE_FIXTURE_UI_SCREENSHOT"));
    if (!screenshotPath.isEmpty()) {
        auto *panel = MainWindowWatchdogSmokeAccessor::panel(window);
        panel->setParent(nullptr);
        panel->setWindowFlag(Qt::Window, true);
        panel->resize(1200, qMax(900, panel->sizeHint().height()));
        panel->show();
        panel->layout()->activate();
        app.processEvents();
        const QPixmap screenshot = panel->grab(QRect(0, 0, panel->width(), qMin(360, panel->height())));
        if (!screenshot.save(screenshotPath)) {
            std::fprintf(stderr, "failed to save single tank PCBA panel screenshot\n");
            return 6;
        }
    }
    if (!MainWindowWatchdogSmokeAccessor::startLog(window)) {
        std::fprintf(stderr, "single tank PCBA automatic log did not start\n");
        return 8;
    }

    QByteArray stalePayload(12 + 52, '\0');
    stalePayload[0] = 1;
    stalePayload[2] = 1;
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Response, 1, usb::GetSingleTankPcba, stalePayload));
    app.processEvents();
    if (MainWindowWatchdogSmokeAccessor::report(window).count != 1) {
        std::fprintf(stderr, "Qt changed the MCU report before receiving another MCU frame\n");
        return 3;
    }

    QByteArray timeoutPayload(12 + (2 * 52), '\0');
    timeoutPayload[0] = 1;
    timeoutPayload[2] = 2;
    const qsizetype timeoutBase = 12 + 52;
    timeoutPayload[timeoutBase + 0] = 1;
    timeoutPayload[timeoutBase + 1] = 0;
    timeoutPayload[timeoutBase + 2] = 0;
    timeoutPayload[timeoutBase + 3] = 0x0A;
    const uint32_t elapsedUs = 2000000u;
    timeoutPayload[timeoutBase + 16] = static_cast<char>(elapsedUs & 0xFFu);
    timeoutPayload[timeoutBase + 17] = static_cast<char>((elapsedUs >> 8) & 0xFFu);
    timeoutPayload[timeoutBase + 18] = static_cast<char>((elapsedUs >> 16) & 0xFFu);
    timeoutPayload[timeoutBase + 19] = static_cast<char>((elapsedUs >> 24) & 0xFFu);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 2, usb::GetSingleTankPcba, timeoutPayload));

    const auto &timeoutReport = MainWindowWatchdogSmokeAccessor::report(window);
    const auto &timeout = timeoutReport.entries[1];
    if (timeoutReport.count != 2 || timeout.ok || timeout.command != 0x00 ||
        timeout.elapsedUs != elapsedUs) {
        std::fprintf(stderr, "Qt did not preserve the MCU timeout entry\n");
        return 4;
    }

    auto *table = MainWindowWatchdogSmokeAccessor::table(window);
    auto *query100ActionItem = table->item(16, 1);
    auto *shutdownActionItem = table->item(22, 1);
    if (!query100ActionItem ||
        !query100ActionItem->text().contains(QStringLiteral("写Flash后先一次性VENT至零")) ||
        !query100ActionItem->text().contains(QStringLiteral("达到100mmHg关进气")) ||
        !query100ActionItem->text().contains(QStringLiteral("自然下降趋势同步查询")) ||
        !shutdownActionItem ||
        !shutdownActionItem->text().contains(QStringLiteral("持续VENT")) ||
        !shutdownActionItem->text().contains(QStringLiteral("至少30秒")) ||
        !shutdownActionItem->text().contains(QStringLiteral("继续VENT 30秒")) ||
        !shutdownActionItem->text().contains(QStringLiteral("0.0±0.1mmHg"))) {
        std::fprintf(stderr, "100mmHg trend action does not describe the one-shot VENT sequence\n");
        return 22;
    }
    auto *rxItem = table->item(1, 3);
    auto *reasonItem = table->item(1, 7);
    if (!rxItem || !reasonItem ||
        !rxItem->text().contains(QStringLiteral("超时")) ||
        !reasonItem->text().contains(QStringLiteral("未收到任何PCBA回包")) ||
        reasonItem->background().color() != QColor(QStringLiteral("#fee2e2"))) {
        std::fprintf(stderr, "timeout row is not visibly red\n");
        return 5;
    }

    FixtureSnapshot snapshot;
    snapshot.sequence = 99;
    snapshot.pressure001mmHg[0] = 123000;
    snapshot.pressureValid[0] = true;
    snapshot.valvesOpen[1] = true;
    snapshot.valvesOpen[2] = true;
    snapshot.valvesOpen[13] = true;
    MainWindowWatchdogSmokeAccessor::applySnapshot(window, snapshot);

    QByteArray pressureFailurePayload(12 + (7 * 52), '\0');
    pressureFailurePayload[0] = 1;
    pressureFailurePayload[2] = 7;
    const qsizetype pressureBase = 12 + (6 * 52);
    pressureFailurePayload[12 + 0] = 1;
    pressureFailurePayload[12 + 2] = 1;
    putU32(pressureFailurePayload, 12 + 16, 500000u);
    pressureFailurePayload[pressureBase + 0] = 4;
    pressureFailurePayload[pressureBase + 1] = 3;
    pressureFailurePayload[pressureBase + 4] = 12;
    putU32(pressureFailurePayload, pressureBase + 7, 250u);
    putU32(pressureFailurePayload, pressureBase + 12, static_cast<uint32_t>(-1250));
    putU32(pressureFailurePayload, pressureBase + 16, 3000000u);
    const uint32_t lastPressure001mmHg = 176000u;
    pressureFailurePayload[pressureBase + 20] = static_cast<char>(lastPressure001mmHg & 0xFFu);
    pressureFailurePayload[pressureBase + 21] = static_cast<char>((lastPressure001mmHg >> 8) & 0xFFu);
    pressureFailurePayload[pressureBase + 22] = static_cast<char>((lastPressure001mmHg >> 16) & 0xFFu);
    pressureFailurePayload[pressureBase + 23] = static_cast<char>((lastPressure001mmHg >> 24) & 0xFFu);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 3, usb::GetSingleTankPcba, pressureFailurePayload));
    app.processEvents();

    auto *pressureItem = table->item(6, 4);
    auto *pressureElapsedItem = table->item(6, 5);
    auto *pressureReasonItem = table->item(6, 7);
    auto *summaryLabel = MainWindowWatchdogSmokeAccessor::summaryLabel(window);
    if (!pressureItem || !pressureReasonItem ||
        !pressureItem->text().contains(QStringLiteral("176.0")) ||
        !pressureItem->text().contains(QStringLiteral("斜率=-1.250")) ||
        !pressureItem->text().contains(QStringLiteral("新样本=12")) ||
        !pressureElapsedItem || !pressureElapsedItem->text().contains(QStringLiteral("观察 3000 ms")) ||
        !summaryLabel || !summaryLabel->text().contains(QStringLiteral("最大串口耗时 500.000 ms")) ||
        !pressureReasonItem->text().contains(QStringLiteral("安全上限"))) {
        std::fprintf(stderr, "closed-loop failure pressure/reason is not visible\n");
        return 9;
    }

    const auto &trendFailure = MainWindowWatchdogSmokeAccessor::report(window).entries[6];
    if (trendFailure.trendSampleCount != 12u ||
        trendFailure.trendSlope001mmHgPerSecond != -1250 ||
        trendFailure.trendMaxResidual001mmHg != 250u ||
        trendFailure.trendObservationUs != 3000000u ||
        trendFailure.trendPredictedPressure001mmHg != lastPressure001mmHg) {
        std::fprintf(stderr, "trend fields were not decoded from the reused kind=4 entry\n");
        return 23;
    }

    QByteArray pressurePassPayload = pressureFailurePayload;
    pressurePassPayload[pressureBase + 1] = 0;
    pressurePassPayload[pressureBase + 2] = 1;
    putU32(pressurePassPayload, pressureBase + 20, 50600u);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 9, usb::GetSingleTankPcba, pressurePassPayload));
    app.processEvents();
    pressureItem = table->item(6, 4);
    pressureReasonItem = table->item(6, 7);
    if (!pressureItem || !pressureReasonItem ||
        !pressureItem->text().contains(QStringLiteral("预测=50.60")) ||
        !pressureReasonItem->text().contains(QStringLiteral("趋势采样通过")) ||
        pressureReasonItem->background().color() != QColor(QStringLiteral("#dcfce7"))) {
        std::fprintf(stderr, "successful trend result is not visibly decoded\n");
        return 24;
    }

    QByteArray queryFailurePayload(12 + (9 * 52), '\0');
    queryFailurePayload[0] = 1;
    queryFailurePayload[2] = 9;
    const qsizetype queryTrendBase = 12 + (6 * 52);
    queryFailurePayload[queryTrendBase + 0] = 4;
    queryFailurePayload[queryTrendBase + 2] = 1;
    putU32(queryFailurePayload, queryTrendBase + 20, 46923u);
    const qsizetype calibrationBaseForQuery = 12 + (7 * 52);
    queryFailurePayload[calibrationBaseForQuery + 0] = 1;
    queryFailurePayload[calibrationBaseForQuery + 1] = 0x10;
    queryFailurePayload[calibrationBaseForQuery + 2] = 1;
    putU32(queryFailurePayload, calibrationBaseForQuery + 20, 46923u);
    const qsizetype queryFailureBase = 12 + (8 * 52);
    queryFailurePayload[queryFailureBase + 0] = 1;
    queryFailurePayload[queryFailureBase + 1] = 0x11;
    queryFailurePayload[queryFailureBase + 4] = 0x11;
    queryFailurePayload[queryFailureBase + 6] = 4;
    queryFailurePayload[queryFailureBase + 24] = 1;
    queryFailurePayload[queryFailureBase + 28] = 0x55;
    putU32(queryFailurePayload, queryFailureBase + 12, 46923u);
    putU32(queryFailurePayload, queryFailureBase + 16, 2491u);
    putU32(queryFailurePayload, queryFailureBase + 20, 66000u);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 22, usb::GetSingleTankPcba, queryFailurePayload));
    app.processEvents();
    auto *queryFailureNameItem = table->item(8, 0);
    auto *calibrationTxItem = table->item(7, 2);
    auto *calibrationParsedItem = table->item(7, 4);
    auto *queryFailureParsedItem = table->item(8, 4);
    auto *queryFailureReasonItem = table->item(8, 7);
    if (!queryFailureNameItem || !calibrationTxItem || !calibrationParsedItem ||
        !queryFailureParsedItem || !queryFailureReasonItem ||
        !calibrationTxItem->text().contains(QStringLiteral("4B B7 00 00")) ||
        !calibrationParsedItem->text().contains(
            QStringLiteral("协议下发=46923（0.001mmHg）")) ||
        !queryFailureNameItem->text().contains(QStringLiteral("标后查询50mmHg")) ||
        !queryFailureParsedItem->text().contains(QStringLiteral("PCBA实测=66.0 mmHg")) ||
        !queryFailureParsedItem->text().contains(QStringLiteral("MPRLS1同步气压=46.923 mmHg")) ||
        !queryFailureParsedItem->text().contains(QStringLiteral("误差=+19.077mmHg")) ||
        !queryFailureReasonItem->text().contains(QStringLiteral("66.0 mmHg")) ||
        !queryFailureReasonItem->text().contains(QStringLiteral("46.923 mmHg")) ||
        !queryFailureReasonItem->text().contains(QStringLiteral("+19.077mmHg")) ||
        !queryFailureReasonItem->text().contains(QStringLiteral("绝对误差19.077mmHg")) ||
        !queryFailureReasonItem->text().contains(QStringLiteral("超过允许误差"))) {
        std::fprintf(stderr,
                     "post-calibration pressure query did not show its actual error details: %s\n",
                     queryFailureReasonItem
                         ? qPrintable(queryFailureReasonItem->text())
                         : "<missing reason item>");
        return 27;
    }

    QByteArray partialQueryPayload = queryFailurePayload;
    partialQueryPayload[queryFailureBase + 6] = 0;
    putU32(partialQueryPayload, queryFailureBase + 20, 0xFFFFFFFFu);
    partialQueryPayload[queryFailureBase + 24] = 6;
    for (qsizetype i = 0; i < 24; ++i) {
        partialQueryPayload[queryFailureBase + 28 + i] = 0;
    }
    partialQueryPayload[queryFailureBase + 28] = 0x55;
    partialQueryPayload[queryFailureBase + 29] = static_cast<char>(0xAA);
    partialQueryPayload[queryFailureBase + 30] = 0x11;
    partialQueryPayload[queryFailureBase + 31] = 0x00;
    partialQueryPayload[queryFailureBase + 32] = 0x00;
    partialQueryPayload[queryFailureBase + 33] = static_cast<char>(0xA4);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 23, usb::GetSingleTankPcba, partialQueryPayload));
    app.processEvents();
    queryFailureParsedItem = table->item(8, 4);
    queryFailureReasonItem = table->item(8, 7);
    if (!queryFailureParsedItem || !queryFailureReasonItem ||
        !queryFailureParsedItem->text().contains(QStringLiteral("未收到完整有效压力帧")) ||
        !queryFailureReasonItem->text().contains(QStringLiteral("残帧")) ||
        queryFailureParsedItem->text().contains(QStringLiteral("0xFFFFFFFF")) ||
        queryFailureReasonItem->text().contains(QStringLiteral("0xFFFFFFFF"))) {
        std::fprintf(stderr, "partial pressure frame was mislabeled as PCBA 0xFFFFFFFF\n");
        return 28;
    }

    struct TrendFailureExpectation {
        uint8_t code;
        const char *reason;
    };
    const std::array<TrendFailureExpectation, 4> trendFailureExpectations{{
        {6u, "新样本数量或采样跨度不足"},
        {7u, "最大拟合残差超限"},
        {8u, "自然下降率超限"},
        {9u, "趋势仍在上升，禁止标定或查询"},
    }};
    for (size_t i = 0; i < trendFailureExpectations.size(); ++i) {
        QByteArray trendReasonPayload = pressurePassPayload;
        trendReasonPayload[pressureBase + 1] = static_cast<char>(trendFailureExpectations[i].code);
        trendReasonPayload[pressureBase + 2] = 0;
        if (trendFailureExpectations[i].code == 9u) {
            putU32(trendReasonPayload, pressureBase + 12, 1250u);
        }
        MainWindowWatchdogSmokeAccessor::feed(
            window,
            usb::buildFrame(usb::Report,
                            static_cast<uint16_t>(10u + i),
                            usb::GetSingleTankPcba,
                            trendReasonPayload));
        app.processEvents();
        pressureReasonItem = table->item(6, 7);
        if (!pressureReasonItem ||
            !pressureReasonItem->text().contains(QString::fromUtf8(trendFailureExpectations[i].reason)) ||
            pressureReasonItem->background().color() != QColor(QStringLiteral("#fee2e2"))) {
            std::fprintf(stderr, "trend failure code %u reason is not explicit\n",
                         static_cast<unsigned>(trendFailureExpectations[i].code));
            return 25;
        }
    }

    const QString sessionDir = MainWindowWatchdogSmokeAccessor::logDir(window);
    MainWindowWatchdogSmokeAccessor::stopLog(window, QStringLiteral("smoke_complete"));
    QFile sessionLog(sessionDir + QStringLiteral("/session.log"));
    QFile snapshotLog(sessionDir + QStringLiteral("/snapshots.csv"));
    QFile stepLog(sessionDir + QStringLiteral("/steps.csv"));
    if (!sessionLog.open(QIODevice::ReadOnly) ||
        !snapshotLog.open(QIODevice::ReadOnly) ||
        !stepLog.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "automatic log files were not created\n");
        return 10;
    }
    const QByteArray sessionBytes = sessionLog.readAll();
    const QByteArray snapshotBytes = snapshotLog.readAll();
    const QByteArray stepBytes = stepLog.readAll();
    if (!sessionBytes.contains("GET_SINGLE_TANK_PCBA") ||
        !snapshotBytes.contains("valve13_open") ||
        !stepBytes.contains("176.0") ||
        !stepBytes.contains("0X03") ||
        !stepBytes.contains("trend_sample_count") ||
        !stepBytes.contains(",12,-1250,250,3000000,176000,")) {
        std::fprintf(stderr, "automatic log files missed diagnostic content\n");
        return 11;
    }

    if (!MainWindowWatchdogSmokeAccessor::startLog(window)) {
        std::fprintf(stderr, "start-race log session did not open\n");
        return 26;
    }
    MainWindowWatchdogSmokeAccessor::markFlowStarting(window);
    QByteArray waitingForStartPayload(12, '\0');
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Response, 20, usb::GetSingleTankPcba, waitingForStartPayload));
    app.processEvents();
    if (!MainWindowWatchdogSmokeAccessor::flowStartPending(window) ||
        !MainWindowWatchdogSmokeAccessor::flowRunning(window) ||
        !MainWindowWatchdogSmokeAccessor::logActive(window) ||
        continueOnFailCheck->isEnabled() ||
        maxDeviationSpin->isEnabled() ||
        trendWindowSpin->isEnabled() ||
        maxDropRateSpin->isEnabled()) {
        std::fprintf(stderr, "empty pre-start MCU report prematurely stopped the flow log\n");
        return 26;
    }
    QByteArray startRaceDonePayload(12, '\0');
    startRaceDonePayload[1] = 1;
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 21, usb::GetSingleTankPcba, startRaceDonePayload));
    app.processEvents();
    if (MainWindowWatchdogSmokeAccessor::flowStartPending(window) ||
        MainWindowWatchdogSmokeAccessor::flowRunning(window) ||
        MainWindowWatchdogSmokeAccessor::logActive(window) ||
        !continueOnFailCheck->isEnabled() ||
        !maxDeviationSpin->isEnabled() ||
        !trendWindowSpin->isEnabled() ||
        !maxDropRateSpin->isEnabled()) {
        std::fprintf(stderr, "completed MCU report did not close the start-race log\n");
        return 26;
    }

    QByteArray invalidAckPayload(12 + (8 * 52), '\0');
    invalidAckPayload[0] = 1;
    invalidAckPayload[2] = 8;
    const qsizetype calibrationBase = 12 + (7 * 52);
    invalidAckPayload[calibrationBase + 0] = 1;
    invalidAckPayload[calibrationBase + 1] = 0x10;
    invalidAckPayload[calibrationBase + 4] = 0x7F;
    invalidAckPayload[calibrationBase + 24] = 8;
    const QByteArray invalidAckRaw = QByteArray::fromHex("55AA7F000000600A");
    for (qsizetype i = 0; i < invalidAckRaw.size(); ++i) {
        invalidAckPayload[calibrationBase + 28 + i] = invalidAckRaw[i];
    }
    putU32(invalidAckPayload, calibrationBase + 16, 2297u);
    putU32(invalidAckPayload, calibrationBase + 20, 52664u);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 6, usb::GetSingleTankPcba, invalidAckPayload));
    app.processEvents();
    auto *invalidAckRxItem = table->item(7, 3);
    auto *invalidAckReasonItem = table->item(7, 7);
    if (!invalidAckRxItem || !invalidAckReasonItem ||
        !invalidAckRxItem->text().contains(QStringLiteral("55 AA 7F")) ||
        invalidAckReasonItem->text().contains(QStringLiteral("超时")) ||
        invalidAckReasonItem->text().contains(QStringLiteral("未收到")) ||
        !invalidAckReasonItem->text().contains(QStringLiteral("已收到ACK"))) {
        std::fprintf(stderr, "received invalid ACK was mislabeled as a timeout\n");
        return 18;
    }

    QByteArray slowAckPayload = invalidAckPayload;
    slowAckPayload[calibrationBase + 2] = 1;
    slowAckPayload[calibrationBase + 6] = 1;
    slowAckPayload[calibrationBase + 7] = 0;
    slowAckPayload[calibrationBase + 24] = 9;
    const QByteArray slowAckRaw = QByteArray::fromHex("55AA7F00010000600A");
    for (qsizetype i = 0; i < 24; ++i) {
        slowAckPayload[calibrationBase + 28 + i] =
            i < slowAckRaw.size() ? slowAckRaw[i] : '\0';
    }
    putU32(slowAckPayload, calibrationBase + 16, 500000u);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 7, usb::GetSingleTankPcba, slowAckPayload));
    app.processEvents();
    auto *slowAckReasonItem = table->item(7, 7);
    if (!slowAckReasonItem ||
        !slowAckReasonItem->text().contains(QStringLiteral("通过")) ||
        !slowAckReasonItem->text().contains(QStringLiteral("500.000ms")) ||
        !slowAckReasonItem->text().contains(QStringLiteral("仅统计")) ||
        slowAckReasonItem->background().color() != QColor(QStringLiteral("#dcfce7"))) {
        std::fprintf(stderr, "valid ACK inside 10ms was not treated as pass-only timing\n");
        return 19;
    }

    QByteArray ventFailurePayload(12 + (6 * 52), '\0');
    ventFailurePayload[0] = 1;
    ventFailurePayload[2] = 6;
    const qsizetype ventBase = 12 + (5 * 52);
    ventFailurePayload[ventBase + 0] = 5;
    ventFailurePayload[ventBase + 1] = 1;
    putU32(ventFailurePayload, ventBase + 20, 250u);
    MainWindowWatchdogSmokeAccessor::feed(
        window,
        usb::buildFrame(usb::Report, 8, usb::GetSingleTankPcba, ventFailurePayload));
    app.processEvents();
    auto *ventParsedItem = table->item(5, 4);
    auto *ventReasonItem = table->item(5, 7);
    if (!ventParsedItem || !ventReasonItem ||
        !ventParsedItem->text().contains(QStringLiteral("0.1")) ||
        !ventReasonItem->text().contains(QStringLiteral("VENT失败")) ||
        !ventReasonItem->text().contains(QStringLiteral("未发送记录零点"))) {
        std::fprintf(stderr, "zero VENT gate failure was not visible\n");
        return 20;
    }

    MainWindowWatchdogSmokeAccessor::markFlowStarting(window);
    const QByteArray nakPayload = QByteArray::fromRawData("\x12\x04", 2);
    MainWindowWatchdogSmokeAccessor::feed(
        window, usb::buildFrame(usb::Response, 4, usb::Nak, nakPayload));
    app.processEvents();
    if (!continueOnFailCheck->isEnabled() || !maxDeviationSpin->isEnabled() ||
        !trendWindowSpin->isEnabled() || !maxDropRateSpin->isEnabled()) {
        std::fprintf(stderr, "RUN_SINGLE_TANK_PCBA NAK did not unlock pressure profile\n");
        return 15;
    }

    MainWindowWatchdogSmokeAccessor::markFlowStarting(window);
    QByteArray donePayload(12, '\0');
    donePayload[1] = 1;
    MainWindowWatchdogSmokeAccessor::feed(
        window, usb::buildFrame(usb::Report, 5, usb::GetSingleTankPcba, donePayload));
    app.processEvents();
    if (!continueOnFailCheck->isEnabled() || !maxDeviationSpin->isEnabled() ||
        !trendWindowSpin->isEnabled() || !maxDropRateSpin->isEnabled()) {
        std::fprintf(stderr, "completed MCU report did not unlock pressure profile\n");
        return 16;
    }

    MainWindowWatchdogSmokeAccessor::markFlowStarting(window);
    MainWindowWatchdogSmokeAccessor::startFlow(window);
    if (!MainWindowWatchdogSmokeAccessor::report(window).done ||
        continueOnFailCheck->isEnabled() || maxDeviationSpin->isEnabled() ||
        trendWindowSpin->isEnabled() || maxDropRateSpin->isEnabled()) {
        std::fprintf(stderr, "duplicate RUN was not ignored while flow was active\n");
        return 17;
    }
    MainWindowWatchdogSmokeAccessor::setProfileEnabled(window, true);

    std::fprintf(stdout, "single tank PCBA diagnostics and automatic log smoke ok\n");
    return 0;
}
