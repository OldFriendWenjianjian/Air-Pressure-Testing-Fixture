#include <stdint.h>
#include <stdio.h>

#include "app_pcba_rx_stream.h"

static uint8_t feed(AppPcbaRxStream *stream,
                    const uint8_t *bytes,
                    size_t len,
                    PcbaFrame *candidate)
{
    uint8_t emitted = 0u;

    for (size_t i = 0u; i < len; ++i) {
        if (AppPcbaRxStream_Push(stream, bytes[i], candidate) != 0u) {
            emitted = 1u;
        }
    }
    return emitted;
}

int main(void)
{
    AppPcbaRxStream stream;
    PcbaFrame candidate = {0};
    uint8_t frame[PCBA_FRAME_MAX_SIZE];
    const uint8_t noise[] = {0x31u, 0x55u, 0x13u, 0x55u};
    const uint8_t partial[] = {0x55u, 0xAAu, 0x7Fu, 0x00u, 0x01u};
    const uint8_t legacy_ack[] = {0x55u, 0xAAu, 0x7Fu, 0x00u,
                                  0x00u, 0x00u, 0x60u, 0x0Au};
    size_t len;

    AppPcbaRxStream_Init(&stream);
    (void)feed(&stream, noise, sizeof(noise), &candidate);

    len = PcbaProtocol_BuildPressure(PCBA_CMD_SYNC_PRESSURE_CAL,
                                     0u,
                                     527u,
                                     frame,
                                     sizeof(frame));
    if (len == 0u || feed(&stream, frame, len, &candidate) == 0u ||
        AppPcbaRxFrame_IsExpected(&candidate, PCBA_CMD_ACK, 1u) != 0u) {
        fputs("request echo was accepted as ACK\n", stderr);
        return 1;
    }

    len = PcbaProtocol_Build(PCBA_CMD_ACK,
                             0u,
                             (const uint8_t[]){2u},
                             1u,
                             frame,
                             sizeof(frame));
    if (len == 0u || feed(&stream, frame, len, &candidate) == 0u ||
        AppPcbaRxFrame_IsExpected(&candidate, PCBA_CMD_ACK, 1u) != 0u) {
        fputs("ACK with invalid data was accepted\n", stderr);
        return 9;
    }

    len = PcbaProtocol_Build(PCBA_CMD_ACK, 0u, 0, 0u, frame, sizeof(frame));
    if (len == 0u) {
        fputs("failed to build bad ACK fixture\n", stderr);
        return 2;
    }
    frame[len - 1u] ^= 0x01u;
    if (feed(&stream, frame, len, &candidate) == 0u ||
        AppPcbaRxFrame_IsExpected(&candidate, PCBA_CMD_ACK, 1u) != 0u) {
        fputs("bad CRC ACK was accepted\n", stderr);
        return 3;
    }

    len = PcbaProtocol_Build(PCBA_CMD_ACK,
                             0u,
                             (const uint8_t[]){PCBA_ACK_YES},
                             1u,
                             frame,
                             sizeof(frame));
    if (len == 0u || feed(&stream, frame, len, &candidate) == 0u ||
        AppPcbaRxFrame_IsExpected(&candidate, PCBA_CMD_ACK, 1u) == 0u) {
        fputs("valid ACK after noise/echo/bad CRC was rejected\n", stderr);
        return 4;
    }

    AppPcbaRxStream_Init(&stream);
    if (feed(&stream, legacy_ack, sizeof(legacy_ack), &candidate) == 0u ||
        AppPcbaRxFrame_IsExpected(&candidate, PCBA_CMD_ACK, 1u) == 0u) {
        fputs("legacy fixture ACK was rejected by stream parser\n", stderr);
        return 5;
    }

    AppPcbaRxStream_Init(&stream);
    (void)feed(&stream, partial, sizeof(partial), &candidate);
    AppPcbaRxStream_CopyDiagnostic(&stream, &candidate);
    if (candidate.raw_len != sizeof(partial) || candidate.raw[0] != 0x55u ||
        candidate.raw[4] != 0x01u) {
        fputs("partial frame was not retained for timeout diagnostics\n", stderr);
        return 6;
    }

    len = PcbaProtocol_Build(PCBA_CMD_ACK,
                             0u,
                             (const uint8_t[]){PCBA_ACK_YES},
                             1u,
                             frame,
                             sizeof(frame));
    AppPcbaRxStream_Init(&stream);
    (void)feed(&stream, partial, sizeof(partial), &candidate);
    if (len == 0u || feed(&stream, frame, len, &candidate) == 0u ||
        AppPcbaRxFrame_IsExpected(&candidate, PCBA_CMD_ACK, 1u) == 0u) {
        fputs("valid ACK after a partial frame was rejected\n", stderr);
        return 8;
    }

    len = PcbaProtocol_BuildPressure(PCBA_CMD_PRESSURE_TEST,
                                     0u,
                                     100000u,
                                     frame,
                                     sizeof(frame));
    AppPcbaRxStream_Init(&stream);
    if (len == 0u || feed(&stream, frame, len, &candidate) == 0u ||
        AppPcbaRxFrame_IsExpected(&candidate, PCBA_CMD_PRESSURE_TEST, 4u) == 0u) {
        fputs("valid pressure response was rejected\n", stderr);
        return 7;
    }

    puts("PCBA RX stream smoke passed");
    return 0;
}
