#include "IntelHexValidator.h"

#include <QFile>
#include <QStringList>

#include <limits>

namespace fixture {

namespace {

int hexNibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

QString addressText(quint64 address)
{
    return QStringLiteral("0x%1").arg(address, 8, 16, QLatin1Char('0')).toUpper();
}

IntelHexValidationResult failure(int lineNumber, const QString &reason)
{
    IntelHexValidationResult result;
    result.error = lineNumber > 0
        ? QStringLiteral("HEX第%1行：%2").arg(lineNumber).arg(reason)
        : reason;
    return result;
}

bool decodeRecord(const QByteArray &line, QByteArray &bytes, QString &error)
{
    if (!line.startsWith(':')) {
        error = QStringLiteral("记录必须以冒号开头");
        return false;
    }
    const QByteArray encoded = line.mid(1);
    if (encoded.size() < 10 || (encoded.size() & 1) != 0) {
        error = QStringLiteral("记录十六进制长度无效");
        return false;
    }

    bytes.clear();
    bytes.reserve(encoded.size() / 2);
    for (qsizetype index = 0; index < encoded.size(); index += 2) {
        const int high = hexNibble(encoded[index]);
        const int low = hexNibble(encoded[index + 1]);
        if (high < 0 || low < 0) {
            error = QStringLiteral("包含非十六进制字符");
            return false;
        }
        bytes.append(static_cast<char>((high << 4) | low));
    }

    const int declaredLength = static_cast<uint8_t>(bytes[0]);
    if (bytes.size() != declaredLength + 5) {
        error = QStringLiteral("字节数声明为%1，但记录实际包含%2个数据字节")
                    .arg(declaredLength)
                    .arg(bytes.size() - 5);
        return false;
    }

    uint8_t sum = 0;
    for (char value : bytes) {
        sum = static_cast<uint8_t>(sum + static_cast<uint8_t>(value));
    }
    if (sum != 0u) {
        error = QStringLiteral("校验和错误");
        return false;
    }
    return true;
}

} // namespace

IntelHexValidationResult validateApplicationIntelHex(const QByteArray &contents)
{
    const QList<QByteArray> lines = contents.split('\n');
    quint64 baseAddress = 0;
    bool sawEof = false;
    bool sawData = false;
    quint64 minimumAddress = std::numeric_limits<quint64>::max();
    quint64 maximumAddress = 0;
    IntelHexValidationResult result;

    for (qsizetype lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const int lineNumber = static_cast<int>(lineIndex + 1);
        const QByteArray line = lines[lineIndex].trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (sawEof) {
            return failure(lineNumber, QStringLiteral("EOF记录后仍有其他记录"));
        }

        QByteArray record;
        QString decodeError;
        if (!decodeRecord(line, record, decodeError)) {
            return failure(lineNumber, decodeError);
        }

        const uint8_t byteCount = static_cast<uint8_t>(record[0]);
        const uint16_t address = static_cast<uint16_t>(
            (static_cast<uint16_t>(static_cast<uint8_t>(record[1])) << 8) |
            static_cast<uint8_t>(record[2]));
        const uint8_t type = static_cast<uint8_t>(record[3]);

        switch (type) {
        case 0x00: {
            const quint64 start = baseAddress + address;
            const quint64 end = start + byteCount;
            if (end < start || end > static_cast<quint64>(std::numeric_limits<quint32>::max()) + 1u) {
                return failure(lineNumber, QStringLiteral("data记录绝对地址溢出"));
            }
            if (byteCount > 0u) {
                const bool overlapsCalibration = start < kSensorCalibrationFlashEndExclusive &&
                                                 end > kSensorCalibrationFlashStart;
                if (overlapsCalibration) {
                    return failure(
                        lineNumber,
                        QStringLiteral("data范围%1..%2覆盖传感器标定保留区%3..%4")
                            .arg(addressText(start))
                            .arg(addressText(end - 1u))
                            .arg(addressText(kSensorCalibrationFlashStart))
                            .arg(addressText(kSensorCalibrationFlashEndExclusive - 1u)));
                }
                if (start < kFirmwareApplicationStart || end > kFirmwareApplicationEndExclusive) {
                    return failure(
                        lineNumber,
                        QStringLiteral("data范围%1..%2超出允许的固件应用区%3..%4")
                            .arg(addressText(start))
                            .arg(addressText(end - 1u))
                            .arg(addressText(kFirmwareApplicationStart))
                            .arg(addressText(kFirmwareApplicationEndExclusive - 1u)));
                }
                sawData = true;
                minimumAddress = qMin(minimumAddress, start);
                maximumAddress = qMax(maximumAddress, end - 1u);
                result.dataBytes += byteCount;
                ++result.dataRecords;
            }
            break;
        }
        case 0x01:
            if (byteCount != 0u || address != 0u) {
                return failure(lineNumber, QStringLiteral("EOF记录必须为长度0、地址0000"));
            }
            sawEof = true;
            break;
        case 0x02:
            if (byteCount != 2u || address != 0u) {
                return failure(lineNumber, QStringLiteral("扩展段地址记录必须为长度2、地址0000"));
            }
            baseAddress = static_cast<quint64>(
                (static_cast<uint16_t>(static_cast<uint8_t>(record[4])) << 8) |
                static_cast<uint8_t>(record[5])) << 4;
            break;
        case 0x03:
            if (byteCount != 4u || address != 0u) {
                return failure(lineNumber, QStringLiteral("起始段地址记录必须为长度4、地址0000"));
            }
            break;
        case 0x04:
            if (byteCount != 2u || address != 0u) {
                return failure(lineNumber, QStringLiteral("扩展线性地址记录必须为长度2、地址0000"));
            }
            baseAddress = static_cast<quint64>(
                (static_cast<uint16_t>(static_cast<uint8_t>(record[4])) << 8) |
                static_cast<uint8_t>(record[5])) << 16;
            break;
        case 0x05:
            if (byteCount != 4u || address != 0u) {
                return failure(lineNumber, QStringLiteral("起始线性地址记录必须为长度4、地址0000"));
            }
            break;
        default:
            return failure(lineNumber, QStringLiteral("不支持的记录类型0x%1")
                                           .arg(type, 2, 16, QLatin1Char('0'))
                                           .toUpper());
        }
    }

    if (!sawEof) {
        return failure(0, QStringLiteral("HEX缺少EOF记录"));
    }
    if (!sawData) {
        return failure(0, QStringLiteral("HEX不包含可烧录的data记录"));
    }

    result.ok = true;
    result.minimumAddress = static_cast<quint32>(minimumAddress);
    result.maximumAddress = static_cast<quint32>(maximumAddress);
    return result;
}

IntelHexValidationResult validateApplicationIntelHexFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(0, QStringLiteral("无法读取HEX文件：%1").arg(file.errorString()));
    }
    return validateApplicationIntelHex(file.readAll());
}

} // namespace fixture
