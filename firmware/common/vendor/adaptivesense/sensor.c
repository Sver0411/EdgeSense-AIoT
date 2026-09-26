/*
 * sensor.c — the sensor API: contract, backend selection, mock override.
 *
 * AdaptiveSense is sensor-agnostic. This file knows nothing about any particular
 * chip; it owns
 *
 *   * the public read contract (nothing is read before a successful init),
 *   * the choice of backend (CONFIG_AS_SENSOR_BACKEND), and
 *   * the mock override used when no hardware is attached.
 *
 * and delegates the actual work to a `sensor_backend_t`. Two backends ship:
 * BME280 (register-level, with pressure) and SHT30/SHT3x (command-based,
 * temperature and humidity). See sensor_backend.h.
 *
 * Backends report what they cannot measure by leaving the channel invalid, which
 * is exactly how the change detector already decides which channels to score — so
 * swapping the sensor changes which numbers arrive, and never how they are used.
 *
 * Initialisation contract
 * -----------------------
 * `sensor_read()` refuses to do anything until `sensor_init()` has reported
 * success. On a failure after init, the return value is -1 and no channel is
 * marked valid, so a caller that ignores the return value still cannot mistake
 * the buffer for a measurement. This is what stops a missing or mis-wired sensor
 * from feeding zeros into the change detector's EMA baseline.
 *
 * The decision of *when* to retry bring-up lives in sensor_supervisor.c
 * (pure C, host-tested); this file only enforces the contract.
 *
 * Host build: with CONFIG_AS_USE_MOCK_SENSOR=1 the ESP-IDF paths are compiled out
 * entirely, so this file builds against the test-only `esp_log.h` shim in
 * tests/c_host/shims and its contract can be tested on a workstation
 * (tests/test_sensor_contract.py).
 */
#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "config_include.h"
#include "sensor.h"
#include "sensor_backend.h"
/* Header-only in every build: the result codes are plain macros and the mock
 * build needs them. The implementation is only linked (and called) in a device
 * build — see apply_optional_light(). */
#include "sensor_bh1750.h"

#if !CONFIG_AS_USE_MOCK_SENSOR
#include "sensor_bus.h"
#endif

static const char *TAG = "sensor";

#if CONFIG_AS_USE_MOCK_SENSOR
/* How the optional light channel should behave in the mock build, so the merge
 * rules can be tested on a host: present, failing, or absent. */
typedef enum {
    MOCK_LIGHT_OK = 0,
    MOCK_LIGHT_FAIL,
    MOCK_LIGHT_ABSENT
} mock_light_mode_t;

static mock_light_mode_t s_mock_light = MOCK_LIGHT_OK;
static bool s_mock_read_fails = false;
#endif

/* Set to true only by a sensor_init() that actually succeeded. Invariant:
 * `s_sensor_initialized == (s_active != NULL)` in non-mock builds. */
static bool s_sensor_initialized = false;

#if CONFIG_AS_USE_MOCK_SENSOR
/* Test seam, present only in the mock configuration: make the next init fail so
 * the contract's failure path can be exercised without an ESP32 attached. */
static bool s_mock_init_fails = false;

/*
 * Stand-ins for the state a real build keeps in `s_active`. A device build's
 * invariant is `s_sensor_initialized == (s_active != NULL)`; `s_mock_backend_live`
 * is the second half of that pair here, so a host test can assert the invariant
 * instead of taking it on trust — in particular that a *failed* re-init leaves no
 * handle behind for a later read to use.
 */
static bool s_mock_backend_live = false;
static unsigned s_mock_init_calls = 0;
static unsigned s_mock_init_failures = 0;
static unsigned s_mock_read_calls = 0;

void sensor_mock_set_init_failure(bool should_fail)
{
    s_mock_init_fails = should_fail;
}

void sensor_mock_stats(sensor_mock_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->init_calls = s_mock_init_calls;
    out->init_failures = s_mock_init_failures;
    out->read_calls = s_mock_read_calls;
    out->backend_live = s_mock_backend_live;
}

void sensor_mock_set_read_failure(bool should_fail)
{
    s_mock_read_fails = should_fail;
}

/*
 * Mock-only seam for the optional light channel. `mode` is one of 0 = present,
 * 1 = attempted and failed, 2 = absent (no attempt made). All three leave the
 * primary channels untouched, which is the behaviour the merge rules are about.
 */
void sensor_mock_set_bh1750(int mode)
{
    s_mock_light = (mode == 1) ? MOCK_LIGHT_FAIL
                 : (mode == 2) ? MOCK_LIGHT_ABSENT
                 : MOCK_LIGHT_OK;
}
#endif

#if !CONFIG_AS_USE_MOCK_SENSOR
/* The backend that is currently brought up, or NULL. */
static const sensor_backend_t *s_active = NULL;

/*
 * Which backend this build uses. Both are always compiled (so neither rots), and
 * the choice is a configuration value rather than a code change — moving between
 * a BME280 and an SHT30 is one line in config.h.
 */
static const sensor_backend_t *selected_backend(void)
{
#if CONFIG_AS_SENSOR_BACKEND == 1
    return &sensor_backend_bme280;
#elif CONFIG_AS_SENSOR_BACKEND == 2
    return &sensor_backend_sht30;
#else
#error "CONFIG_AS_SENSOR_BACKEND must be 1 (BME280) or 2 (SHT30)"
#endif
}
#endif /* !CONFIG_AS_USE_MOCK_SENSOR */

/* ------------------------------------------------------------------ */
/* mock sensor: reproducible synthetic sequence (documented in README) */
/* ------------------------------------------------------------------ */
#if CONFIG_AS_USE_MOCK_SENSOR
static void mock_fill(sensor_read_t *out)
{
    static uint32_t step = 0;
    const float t = 24.0f + 0.08f * sinf((float)step * 0.1f);
    const float h = 45.0f + 0.4f * sinf((float)step * 0.07f);
    for (int i = 0; i < SEN_CH_COUNT; i++) {
        out->valid[i] = true;
    }
    out->value[SEN_CH_TEMPERATURE] = t;
    out->value[SEN_CH_HUMIDITY] = h;
    out->value[SEN_CH_PRESSURE] = 1012.4f;
    out->value[SEN_CH_LIGHT] = 320.0f + 3.0f * sinf((float)step * 0.02f);
    step++;
}
#endif /* CONFIG_AS_USE_MOCK_SENSOR */

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */
int sensor_init(void)
{
#if CONFIG_AS_USE_MOCK_SENSOR
    s_mock_init_calls++;
    if (s_mock_init_fails) {
        /* Mock-only seam so the contract's failure path is testable on a host.
         * Not compiled into a device build; see sensor.h. */
        ESP_LOGW(TAG, "MOCK sensor: init failure requested by the test seam");
        s_sensor_initialized = false;
        s_mock_backend_live = false; /* the equivalent of teardown() in a real build */
        s_mock_init_failures++;
        return -1;
    }
    ESP_LOGW(TAG, "MOCK sensor enabled - results are NOT real measurements");
    s_sensor_initialized = true;
    s_mock_backend_live = true;
    return 0;
#else
    const sensor_backend_t *backend = selected_backend();

    /*
     * A bring-up attempt begins by declaring the sensor unusable, and only the
     * success path at the bottom puts it back. The invariant this maintains is
     *
     *     s_sensor_initialized == (s_active != NULL)
     *
     * which matters because a *re*-init can fail. Before this, that left the
     * previous success's flag set while the backend had already been torn down:
     * `sensor_is_initialized()` returned true with no active backend, and callers
     * that trust it would talk to a driver that no longer exists.
     */
    s_sensor_initialized = false;
    if (s_active != NULL) {
        s_active->teardown();
        s_active = NULL;
    }

    if (backend == NULL) {
        return -1;
    }

    /* The bus is created once and shared with every other device on it; a
     * backend never makes its own. See sensor_bus.h. */
    if (sensor_bus_acquire() == NULL) {
        ESP_LOGE(TAG, "i2c bus unavailable; cannot bring up %s", backend->name);
        return -1;
    }

    if (backend->init() != 0) {
        backend->teardown();
        return -1; /* already not initialized, and no active backend */
    }

    s_active = backend;
    s_sensor_initialized = true;
    ESP_LOGI(TAG, "sensor backend ready: %s", backend->name);

#if CONFIG_AS_USE_BH1750
    /*
     * Arm the optional light channel. It is deliberately not probed here: the
     * primary backend is up, so the node can measure, and an absent light sensor
     * is discovered by the first measurement rather than assumed now. Arming
     * cannot fail, so it cannot change the result of bring-up — the light
     * channel must never be able to prevent the node from starting.
     */
    sensor_bh1750_init();
#endif
    return 0;
#endif /* CONFIG_AS_USE_MOCK_SENSOR */
}

#if CONFIG_AS_USE_MOCK_SENSOR
static int mock_light_read(float *lux)
{
    if (s_mock_light == MOCK_LIGHT_ABSENT) {
        return BH1750_READ_SKIP;
    }
    if (s_mock_light == MOCK_LIGHT_FAIL) {
        return BH1750_READ_FAIL;
    }
    static uint32_t lstep = 0;
    *lux = 320.0f + 3.0f * sinf((float)lstep * 0.02f);
    lstep++;
    return BH1750_READ_OK;
}
#endif /* CONFIG_AS_USE_MOCK_SENSOR */

/*
 * The optional light channel, merged into a measurement the primary backend has
 * already produced.
 *
 * Why this exists as a separate step. The BH1750 is not a third entry in
 * CONFIG_AS_SENSOR_BACKEND: that option selects which chip provides the primary
 * environmental reading, and the light sensor adds one channel alongside it
 * rather than replacing it. So the primary backend is read first, and only then
 * is the light channel filled in.
 *
 * Why a light failure is not a measurement failure. The light sensor is optional.
 * If it fails — or is not fitted, or has just been unplugged — the temperature
 * and humidity the node exists to report are still perfectly good, and tearing
 * the whole measurement down because a decorative channel went quiet would be a
 * bug, not caution. The light channel is simply marked invalid, which is exactly
 * the signal the change detector already uses to skip a channel.
 *
 * This layer still knows nothing about the detector, the scheduler, MQTT or
 * events: it either has a lux value or it does not.
 */
static void apply_optional_light(sensor_read_t *out)
{
    out->value[SEN_CH_LIGHT] = 0.0f;
    out->valid[SEN_CH_LIGHT] = false;

    /* If the policy excludes the channel, do not spend conversion time on it. */
#if !CONFIG_AS_USE_LIGHT
    return;
#else
    float lux = 0.0f;
    int rc;
#if CONFIG_AS_USE_MOCK_SENSOR
    rc = mock_light_read(&lux);
#elif CONFIG_AS_USE_BH1750
    rc = sensor_bh1750_read_lux(&lux);
#else
    rc = BH1750_READ_SKIP;   /* no light driver in this build */
#endif
    if (rc == BH1750_READ_OK) {
        out->value[SEN_CH_LIGHT] = lux;
        out->valid[SEN_CH_LIGHT] = true;
    }
#endif /* CONFIG_AS_USE_LIGHT */
}

int sensor_read(sensor_read_t *out)
{
    if (out == NULL) {
        return -1;
    }
    /* Contract: nothing is read before a successful init. Returning here leaves
     * `out` untouched, so a caller that ignores the return value cannot mistake
     * zeros for a measurement. */
    if (!s_sensor_initialized) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

#if CONFIG_AS_USE_MOCK_SENSOR
    /* Counted here, i.e. only once the contract has allowed the read, so the
     * counter shows whether a rejected call ever reached a backend. */
    s_mock_read_calls++;
    if (s_mock_read_fails) {
        /* The primary measurement failed: no channel is usable, including light.
         * The caller sees -1 and nothing is valid. */
        return -1;
    }
    mock_fill(out);
    apply_optional_light(out);
    return 0;
#else
    if (s_active == NULL || s_active->read == NULL) {
        return -1;
    }
    /* The backend fills in only the channels it can measure; everything else
     * stays zero with valid == false, which is how the detector is told to
     * ignore it. On failure no channel is left valid. */
    if (s_active->read(out) != 0) {
        return -1;
    }
    /* The primary measurement succeeded, so the sample is usable. Whether the
     * optional light channel has a value is a separate question. */
    apply_optional_light(out);
    return 0;
#endif /* CONFIG_AS_USE_MOCK_SENSOR */
}

bool sensor_is_initialized(void)
{
    return s_sensor_initialized;
}

const char *sensor_backend_name(void)
{
#if CONFIG_AS_USE_MOCK_SENSOR
    return "mock (no hardware)";
#else
    return selected_backend()->name;
#endif
}

const char *sensor_channel_name(sen_channel_t c)
{
    static const char *const names[SEN_CH_COUNT] = {
        "temperature", "humidity", "pressure", "light"
    };
    if ((int)c < 0 || (int)c >= (int)SEN_CH_COUNT) {
        return "unknown";
    }
    return names[c];
}
