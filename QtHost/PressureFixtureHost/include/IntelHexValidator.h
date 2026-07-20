#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace fixture {

constexpr quint32 kFirmwareApplicationStart = 0x08000000u;
constexpr quint32 kFirmwareApplicationEndExclusive = 0x0807F000u;
constexpr quint32 kSensorCalibrationFlashStart = 0x0807F000u;
constexpr quint32 kSensorCalibrationFlashEndExclusive = 0x08080000u;

struct IntelHexValidationResult {
    bool ok = false;
    QString error;
    quint32 minimumAddress = 0;
    quint32 maximumAddress = 0;
    qsizetype dataBytes = 0;
    int dataRecords = 0;
};

IntelHexValidationResult validateApplicationIntelHex(const QByteArray &contents);
IntelHexValidationResult validateApplicationIntelHexFile(const QString &path);

} // namespace fixture
