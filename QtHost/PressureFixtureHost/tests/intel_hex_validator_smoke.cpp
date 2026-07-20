#include "IntelHexValidator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

using namespace fixture;

namespace {

QByteArray record(uint8_t type, uint16_t address, const QByteArray &data = {})
{
    QByteArray bytes;
    bytes.append(static_cast<char>(data.size()));
    bytes.append(static_cast<char>((address >> 8) & 0xFFu));
    bytes.append(static_cast<char>(address & 0xFFu));
    bytes.append(static_cast<char>(type));
    bytes.append(data);
    uint8_t sum = 0;
    for (char value : bytes) {
        sum = static_cast<uint8_t>(sum + static_cast<uint8_t>(value));
    }
    bytes.append(static_cast<char>(0u - sum));
    return ':' + bytes.toHex().toUpper() + '\n';
}

QByteArray u16be(uint16_t value)
{
    QByteArray data;
    data.append(static_cast<char>((value >> 8) & 0xFFu));
    data.append(static_cast<char>(value & 0xFFu));
    return data;
}

bool expectFailure(const QByteArray &contents, const QString &needle)
{
    const IntelHexValidationResult result = validateApplicationIntelHex(contents);
    if (result.ok || !result.error.contains(needle)) {
        qCritical() << "expected HEX failure containing" << needle << "but got" << result.ok << result.error;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() == 2) {
        const IntelHexValidationResult fileResult = validateApplicationIntelHexFile(arguments[1]);
        if (!fileResult.ok) {
            qCritical() << "firmware HEX rejected:" << fileResult.error;
            return 12;
        }
        qInfo().noquote() << QStringLiteral("firmware HEX ok: %1 bytes, 0x%2..0x%3")
                                .arg(fileResult.dataBytes)
                                .arg(fileResult.minimumAddress, 8, 16, QLatin1Char('0'))
                                .arg(fileResult.maximumAddress, 8, 16, QLatin1Char('0'))
                                .toUpper();
        return 0;
    }
    if (arguments.size() != 1) {
        qCritical() << "usage: IntelHexValidatorSmoke.exe [firmware.hex]";
        return 13;
    }

    QByteArray valid;
    valid += record(0x02, 0, u16be(0x8000));
    valid += record(0x04, 0, u16be(0x0800));
    valid += record(0x00, 0x0000, QByteArray(16, '\x11'));
    valid += record(0x03, 0, QByteArray(4, '\0'));
    valid += record(0x04, 0, u16be(0x0807));
    valid += record(0x00, 0xEFF0, QByteArray(16, '\x22'));
    valid += record(0x05, 0, QByteArray(4, '\0'));
    valid += record(0x01, 0);
    const IntelHexValidationResult validResult = validateApplicationIntelHex(valid);
    if (!validResult.ok || validResult.minimumAddress != 0x08000000u ||
        validResult.maximumAddress != 0x0807EFFFu || validResult.dataBytes != 32 ||
        validResult.dataRecords != 2) {
        qCritical() << "valid HEX was rejected or summarized incorrectly" << validResult.error;
        return 1;
    }

    QByteArray reserved;
    reserved += record(0x04, 0, u16be(0x0807));
    reserved += record(0x00, 0xF000, QByteArray(1, '\x55'));
    reserved += record(0x01, 0);
    if (!expectFailure(reserved, QStringLiteral("覆盖传感器标定保留区"))) {
        return 2;
    }

    QByteArray crossing;
    crossing += record(0x04, 0, u16be(0x0807));
    crossing += record(0x00, 0xEFF8, QByteArray(16, '\x66'));
    crossing += record(0x01, 0);
    if (!expectFailure(crossing, QStringLiteral("覆盖传感器标定保留区"))) {
        return 3;
    }

    QByteArray beyond;
    beyond += record(0x04, 0, u16be(0x0808));
    beyond += record(0x00, 0x0000, QByteArray(1, '\x77'));
    beyond += record(0x01, 0);
    if (!expectFailure(beyond, QStringLiteral("超出允许的固件应用区"))) {
        return 4;
    }

    QByteArray segmentOutside;
    segmentOutside += record(0x02, 0, u16be(0x8000));
    segmentOutside += record(0x00, 0x0000, QByteArray(1, '\x33'));
    segmentOutside += record(0x01, 0);
    if (!expectFailure(segmentOutside, QStringLiteral("超出允许的固件应用区"))) {
        return 5;
    }

    QByteArray badChecksum;
    badChecksum += record(0x04, 0, u16be(0x0800));
    QByteArray damaged = record(0x00, 0, QByteArray(1, '\x01'));
    damaged[damaged.size() - 2] = damaged[damaged.size() - 2] == '0' ? '1' : '0';
    badChecksum += damaged;
    badChecksum += record(0x01, 0);
    if (!expectFailure(badChecksum, QStringLiteral("校验和错误"))) {
        return 6;
    }

    if (!expectFailure(QByteArray(":0200000408F2\n") + record(0x01, 0),
                       QStringLiteral("字节数声明"))) {
        return 7;
    }

    QByteArray missingEof;
    missingEof += record(0x04, 0, u16be(0x0800));
    missingEof += record(0x00, 0, QByteArray(1, '\x01'));
    if (!expectFailure(missingEof, QStringLiteral("缺少EOF"))) {
        return 8;
    }

    QByteArray afterEof = valid + record(0x00, 0, QByteArray(1, '\x01'));
    if (!expectFailure(afterEof, QStringLiteral("EOF记录后"))) {
        return 9;
    }

    QByteArray unsupported;
    unsupported += record(0x06, 0);
    unsupported += record(0x01, 0);
    if (!expectFailure(unsupported, QStringLiteral("不支持的记录类型"))) {
        return 10;
    }

    if (!expectFailure(record(0x04, 0, u16be(0x0800)) + record(0x01, 0),
                       QStringLiteral("不包含可烧录的data记录"))) {
        return 11;
    }

    qInfo() << "intel HEX validator smoke ok";
    return 0;
}
