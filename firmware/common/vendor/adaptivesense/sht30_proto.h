/*
 * sht30_proto.h — SHT30 / SHT3x protocol, independent of any I2C implementation.
 *
 * Pure C99, no ESP-IDF dependency, so the protocol can be unit-tested on a
 * workstation with a fake transport. `sensor_sht30.c` supplies a transport backed
 * by the real bus and does nothing else.
 *
 * The command constant and the two conversion formulas below are not taken on
 * trust from the datasheet: they were confirmed on the physical device before
 * this driver was written, by issuing the command over I2C and checking that both
 * CRC fields validate and that the decoded values are physically plausible and
 * agree with a second, independent command:
 *
 *   cmd 0x2400 -> 6A 12 1B 90 8F 98 | CRC ok | T=27.51 C  RH=56.47 %
 *   cmd 0x2C06 -> 6A 12 1B 90 70 34 | CRC ok | T=27.51 C  RH=56.42 %
 *
 * A wrong command byte or a wrong formula cannot survive that test.
 */
#ifndef ADAPTIVESENSE_SHT30_PROTO_H
#define ADAPTIVESENSE_SHT30_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single shot, high repeatability, clock stretching DISABLED. The caller issues
 * the command, waits for the conversion, then reads once. Clock stretching is
 * avoided on purpose: not every I2C controller handles it. */
#define SHT30_CMD_MEASURE_HIGH_NOSTRETCH 0x2400u

/* Read the status word (3 bytes, CRC-checked). Used to confirm that the device at
 * the address really speaks the SHT3x command set before trusting it. */
#define SHT30_CMD_READ_STATUS 0xF32Du

/* Soft reset (the only command sent without a following read). */
#define SHT30_CMD_SOFT_RESET 0x30A2u

/* A measurement response is two 16-bit values, each followed by its CRC. */
#define SHT30_FRAME_LEN 6
#define SHT30_STATUS_LEN 3

/* Conversion time for high repeatability, milliseconds, plus margin. The
 * datasheet maximum is 15 ms; the caller waits this long between the command and
 * the read. Exposed so a caller can document its timing budget. */
#define SHT30_MEASURE_WAIT_MS 20u

/*
 * Transport supplied by the caller: three operations rather than one, because the
 * measurement genuinely needs write -> wait -> read, and a fake transport must be
 * able to fail each step independently.
 */
typedef struct {
    int (*write)(void *ctx, const uint8_t *cmd, size_t len);
    int (*read)(void *ctx, uint8_t *out, size_t len);
    void (*delay_ms)(void *ctx, unsigned ms);
    void *ctx;
} sht30_io_t;

/*
 * CRC-8 as specified by Sensirion: polynomial x^8 + x^5 + x^4 + 1 (0x31),
 * initial value 0xFF, no reflection, no final XOR.
 * Verified against frames captured from the physical device.
 */
uint8_t sht30_crc8(const uint8_t *data, size_t len);

/* True when `data` is followed by a correct CRC byte. */
bool sht30_crc_ok(const uint8_t *data, size_t len);

/*
 * Decode a 6-byte measurement frame.
 *
 * Returns false, writing nothing, if either CRC is wrong: a measurement that
 * fails its checksum is not a measurement, and the caller must treat the sample
 * as absent rather than use the value. This is the property the host tests pin.
 */
bool sht30_decode_measurement(const uint8_t frame[SHT30_FRAME_LEN],
                              float *temperature_c, float *humidity_pct);

/*
 * Confirm the device speaks the SHT3x command set: send the status command and
 * check the 3-byte status word (and its CRC) comes back. Returns 0 on success.
 */
int sht30_proto_identify(const sht30_io_t *io);

/*
 * Take one measurement: write the measurement command, wait SHT30_MEASURE_WAIT_MS,
 * read the frame, verify both CRCs, decode.
 *
 * Returns 0 on success and only then writes the outputs; -1 on any failure
 * (transport error, short read, bad CRC), leaving the outputs untouched. One
 * command yields exactly one readable frame — a second read without a new command
 * fails, which was also observed on the device.
 */
int sht30_proto_measure(const sht30_io_t *io, float *temperature_c,
                        float *humidity_pct);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTIVESENSE_SHT30_PROTO_H */
