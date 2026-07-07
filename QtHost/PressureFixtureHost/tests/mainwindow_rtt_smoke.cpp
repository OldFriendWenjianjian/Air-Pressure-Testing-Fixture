#include "MainWindow.h"

#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QElapsedTimer>
#include <QPlainTextEdit>
#include <QThread>
#include <cstdio>

namespace {

QComboBox *findPortCombo(QWidget *parent)
{
    const auto combos = parent->findChildren<QComboBox *>();
    for (auto *combo : combos) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == QStringLiteral("RTT:JLINK")) {
                return combo;
            }
        }
    }
    return nullptr;
}

QAbstractButton *findButtonByText(QWidget *parent, const QString &text)
{
    const auto buttons = parent->findChildren<QAbstractButton *>();
    for (auto *button : buttons) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QPlainTextEdit *findLogView(QWidget *parent)
{
    const auto logs = parent->findChildren<QPlainTextEdit *>();
    for (auto *log : logs) {
        if (log->placeholderText().contains(QStringLiteral("上位机控制日志"))) {
            return log;
        }
    }
    return logs.isEmpty() ? nullptr : logs.constFirst();
}

bool logContains(QPlainTextEdit *log, const QString &needle)
{
    return log && log->toPlainText().contains(needle, Qt::CaseInsensitive);
}

}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    app.processEvents();

    auto *portCombo = findPortCombo(&window);
    auto *connectButton = findButtonByText(&window, QStringLiteral("连接"));
    auto *logView = findLogView(&window);
    if (!portCombo || !connectButton || !logView) {
        std::fprintf(stderr,
                     "widget lookup failed: portCombo=%d connectButton=%d logView=%d\n",
                     portCombo ? 1 : 0,
                     connectButton ? 1 : 0,
                     logView ? 1 : 0);
        return 2;
    }

    int rttIndex = -1;
    for (int i = 0; i < portCombo->count(); ++i) {
        if (portCombo->itemData(i).toString() == QStringLiteral("RTT:JLINK")) {
            rttIndex = i;
            break;
        }
    }
    if (rttIndex < 0) {
        std::fprintf(stderr, "RTT:JLINK not found in port list\n");
        std::fprintf(stderr, "%s\n", logView->toPlainText().toLocal8Bit().constData());
        return 3;
    }

    portCombo->setCurrentIndex(rttIndex);
    app.processEvents();

    std::fprintf(stdout, "stage: click connect\n");
    connectButton->click();
    app.processEvents();

    QElapsedTimer timer;
    timer.start();
    bool sawOpen = false;
    bool sawTxHello = false;
    bool sawRxHello = false;
    bool sawGetStatus = false;

    while (timer.elapsed() < 8000) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(50);

        const QString logText = logView->toPlainText();
        if (!sawOpen && logText.contains(QStringLiteral("已打开"), Qt::CaseInsensitive)) {
            sawOpen = true;
            std::fprintf(stdout, "stage: open seen\n");
        }
        if (!sawTxHello && logText.contains(QStringLiteral("TX HELLO"), Qt::CaseInsensitive)) {
            sawTxHello = true;
            std::fprintf(stdout, "stage: tx hello seen\n");
        }
        if (!sawRxHello && logText.contains(QStringLiteral("cmd=HELLO"), Qt::CaseInsensitive)) {
            sawRxHello = true;
            std::fprintf(stdout, "stage: rx hello seen\n");
        }
        if (!sawGetStatus && logText.contains(QStringLiteral("TX GET_STATUS"), Qt::CaseInsensitive)) {
            sawGetStatus = true;
            std::fprintf(stdout, "stage: get_status seen\n");
        }

        if (sawOpen && sawTxHello && sawRxHello && sawGetStatus) {
            break;
        }
    }

    const QString finalLog = logView->toPlainText();
    std::fprintf(stdout, "--- LOG BEGIN ---\n%s\n--- LOG END ---\n", finalLog.toLocal8Bit().constData());

    if (connectButton->text() == QStringLiteral("断开")) {
        connectButton->click();
        app.processEvents();
        QThread::msleep(100);
    }

    if (!(sawOpen && sawTxHello && sawRxHello && sawGetStatus)) {
        std::fprintf(stderr,
                     "missing expected states: open=%d txHello=%d rxHello=%d getStatus=%d\n",
                     sawOpen ? 1 : 0,
                     sawTxHello ? 1 : 0,
                     sawRxHello ? 1 : 0,
                     sawGetStatus ? 1 : 0);
        return 4;
    }

    if (logContains(logView, QStringLiteral("RTT buffer 索引无效")) ||
        logContains(logView, QStringLiteral("连接错误")) ||
        logContains(logView, QStringLiteral("HELLO 未收到响应"))) {
        std::fprintf(stderr, "unexpected error found in log\n");
        return 5;
    }

    std::fprintf(stdout, "mainwindow RTT smoke ok\n");
    return 0;
}
