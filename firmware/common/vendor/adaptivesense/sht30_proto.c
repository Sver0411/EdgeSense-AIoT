/*
 * sht30_proto.c — see sht30_proto.h.
 *
 * Pure C99, no ESP-IDF dependency, host-testable.
 */

#include "sht30_proto.h"

/* Sensirion CRC-8: poly 0x31, init 0xFF, MSB-first, no reflection, no final XOR.
 * Validated against status, serial-number and measurement frames read from the
 * physical device. */
uint8_t sht30_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    if (data == NULL) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool sht30_crc_ok(const uint8_t *data, size_t len)
{
    if (data == NULL || len < 1) {
        return false;
    }
    return sht30_crc8(data, len - 1) == data[len - 1];
}

bool sht30_decode_measurement(const uint8_t frame[SHT30_FRAME_LEN],
                              float *temperature_c, float *humidity_pct)
{
    if (frame == NULL) {
        return false;
    }

    /* Both halves carry their own CRC. A failure here means the frame is not
     * trustworthy, so nothing is decoded and nothing is written. */
    if (!sht30_crc_ok(&frame[0], 3) || !sht30_crc_ok(&frame[3], 3)) {
        return false;
    }

    const uint16_t raw_t = (uint16_t)(((uint16_t)frame[0] << 8) | frame[1]);
    const uint16_t raw_rh = (uint16_t)(((uint16_t)frame[3] << 8) | frame[4]);

    /* Sensirion SHT3x conversion. */
    const float t = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    const float rh = 100.0f * ((float)raw_rh / 65535.0f);

    if (temperature_c != NULL) {
        *temperature_c = t;
    }
    if (humidity_pct != NULL) {
        *humidity_pct = rh;
    }
    return true;
}

static int write_u16(const sht30_io_t *io, uint16_t command)
{
    const uint8_t cmd[2] = { (uint8_t)(command >> 8), (uint8_t)(command & 0xFF) };
    return io->write(io->ctx, cmd, sizeof(cmd));
}

int sht30_proto_identify(const sht30_io_t *io)
{
    if (io == NULL || io->write == NULL || io->read == NULL) {
        return -1;
    }
    if (write_u16(io, SHT30_CMD_READ_STATUS) != 0) {
        return -1;
    }

    uint8_t status[SHT30_STATUS_LEN] = { 0 };
    if (io->read(io->ctx, status, sizeof(status)) != 0) {
        return -1;
    }
    /* A device that answers with a valid checksum on this command is SHT3x
     * family; anything else at this address must not be treated as one. */
    return sht30_crc_ok(status, sizeof(status)) ? 0 : -1;
}

int sht30_proto_measure(const sht30_io_t *io, float *temperature_c,
                        float *humidity_pct)
{
    if (io == NULL || io->write == NULL || io->read == NULL) {
        return -1;
    }

    if (write_u16(io, SHT30_CMD_MEASURE_HIGH_NOSTRETCH) != 0) {
        return -1;
    }

    if (io->delay_ms != NULL) {
        io->delay_ms(io->ctx, SHT30_MEASURE_WAIT_MS);
    }

    uint8_t frame[SHT30_FRAME_LEN] = { 0 };
    if (io->read(io->ctx, frame, sizeof(frame)) != 0) {
        return -1;
    }

    float t = 0.0f;
    float rh = 0.0f;
    if (!sht30_decode_measurement(frame, &t, &rh)) {
        return -1; /* bad CRC: the sample does not exist */
    }

    if (temperature_c != NULL) {
        *temperature_c = t;
    }
    if (humidity_pct != NULL) {
        *humidity_pct = rh;
    }
    return 0;
}
