/*
 * soil_moisture.c — analog soil-moisture acquisition on the ADC.
 *
 * See soil_moisture.h for the contract and soil_moisture_math.h for the maths.
 * The ESP-IDF pieces here are the current recommended ones (esp_adc oneshot +
 * adc_cali), and the GPIO -> unit/channel mapping is asked of the driver with
 * adc_oneshot_io_to_channel() rather than hardcoded, so changing the pin in
 * configuration is enough.
 */
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include "config_include.h"
#include "soil_moisture.h"
#include "soil_moisture_math.h"

static const char *TAG = "soil";

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static adc_channel_t s_channel = 0;
static bool s_ready = false;
static bool s_cali_ok = false;

/* Warn once per contiguous saturated stretch, not on every read: a probe sitting
 * at an endpoint is a legitimate state, and spamming the log helps nobody. */
static bool s_saturation_warned = false;

static const char *atten_name(int atten)
{
    switch (atten) {
    case 0: return "0 dB  (up to ~1.1 V)";
    case 1: return "2.5 dB (up to ~1.5 V)";
    case 2: return "6 dB  (up to ~2.2 V)";
    case 3: return "12 dB (up to ~3.1 V)";
    default: return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* init                                                                */
/* ------------------------------------------------------------------ */
int soil_moisture_init(void)
{
#if !CONFIG_AS_USE_SOIL_SENSOR
    ESP_LOGW(TAG, "soil telemetry disabled by configuration (CONFIG_AS_USE_SOIL_SENSOR=0)");
    return -1;
#else
    /* Which unit and channel does this GPIO belong to? Asked, not hardcoded:
     * the answer is a property of the chip, and the pin is a config value. */
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = 0;
    const esp_err_t map_err =
        adc_oneshot_io_to_channel(CONFIG_AS_SOIL_ADC_GPIO, &unit, &channel);
    if (map_err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d is not an ADC pad (%s); soil telemetry unavailable",
                 (unsigned)CONFIG_AS_SOIL_ADC_GPIO, esp_err_to_name(map_err));
        return -1;
    }

    if (unit != ADC_UNIT_1) {
        /*
         * ADC2 shares its controller with the Wi-Fi driver and cannot be used
         * while Wi-Fi is active, which on this node is always. Fail loudly here
         * rather than producing readings that only work with the radio off.
         */
        ESP_LOGE(TAG, "GPIO %d is on ADC2, which is unavailable while Wi-Fi is "
                      "active; pick an ADC1 pin (GPIO1-10)",
                 (unsigned)CONFIG_AS_SOIL_ADC_GPIO);
        return -1;
    }
    s_channel = channel;

    const adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&init_cfg, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed; soil telemetry unavailable");
        return -1;
    }

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = (adc_atten_t)CONFIG_AS_SOIL_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT, /* 12 bit on the ESP32-S3 */
    };
    if (adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed; soil telemetry unavailable");
        return -1;
    }

    /*
     * Calibration. Curve fitting is the scheme the ESP32-S3 supports
     * (ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED); the older line-fitting scheme
     * exists on other chips and is guarded by its own capability macro, so a
     * port to one of those keeps its fallback without breaking this build.
     * Without either scheme, raw counts are still reported but millivolts are
     * not (voltage_valid = false), and the log says so instead of implying a
     * calibrated voltage.
     */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t curve_cfg = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = (adc_atten_t)CONFIG_AS_SOIL_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&curve_cfg, &s_cali) == ESP_OK) {
        s_cali_ok = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_efuse_val_t line_val = 0;
    if (adc_cali_scheme_line_fitting_check_efuse(&line_val) == ESP_OK) {
        const adc_cali_line_fitting_config_t line_cfg = {
            .unit_id = unit,
            .chan = s_channel,
            .atten = (adc_atten_t)CONFIG_AS_SOIL_ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_line_fitting(&line_cfg, &s_cali) == ESP_OK) {
            s_cali_ok = true;
        }
    }
#endif
    if (!s_cali_ok) {
        ESP_LOGW(TAG, "no ADC calibration scheme available: raw counts will be "
                      "reported without calibrated millivolts");
    }

    s_ready = true;
    ESP_LOGI(TAG, "soil moisture ADC ready: GPIO%d -> ADC%d ch%d, atten %s, "
                  "%u samples/read, calibration=%s",
             (unsigned)CONFIG_AS_SOIL_ADC_GPIO, (int)unit + 1, (int)channel,
             atten_name(CONFIG_AS_SOIL_ADC_ATTEN),
             (unsigned)CONFIG_AS_SOIL_SAMPLE_COUNT,
             s_cali_ok ? "curve/line fitting (eFuse)" : "NONE (raw only)");
    return 0;
#endif /* CONFIG_AS_USE_SOIL_SENSOR */
}

/* ------------------------------------------------------------------ */
/* read                                                                */
/* ------------------------------------------------------------------ */
bool soil_moisture_calibrated(void)
{
    /*
     * Zero is reserved as the unset sentinel for each endpoint. With only one
     * endpoint configured, raw 0 must not be interpreted as fully dry or wet.
     * Until BOTH endpoints are measured, the honest output is no index at all.
     */
    if (CONFIG_AS_SOIL_DRY_RAW <= 0 || CONFIG_AS_SOIL_WET_RAW <= 0) {
        return false;
    }
    soil_moisture_calib_t calib = {
        .dry_raw = CONFIG_AS_SOIL_DRY_RAW,
        .wet_raw = CONFIG_AS_SOIL_WET_RAW,
    };
    return soil_moisture_calib_valid(&calib, SOIL_ADC_MAX_RAW);
}

int soil_moisture_read(soil_moisture_reading_t *out)
{
    if (out == NULL || !s_ready) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    int samples[CONFIG_AS_SOIL_SAMPLE_COUNT];
    int taken = 0;
    for (int i = 0; i < CONFIG_AS_SOIL_SAMPLE_COUNT; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, s_channel, &raw) == ESP_OK) {
            samples[taken++] = raw;
        }
    }
    out->samples_taken = taken;
    if (taken == 0) {
        /* Not one usable read: a real failure, and never a 0 % reading. */
        ESP_LOGW(TAG, "no usable ADC read in %u attempts",
                 (unsigned)CONFIG_AS_SOIL_SAMPLE_COUNT);
        return -1;
    }

    soil_batch_stats_t st;
    soil_batch_stats_compute(samples, taken, &st);
    out->min = st.min;
    out->max = st.max;
    out->mean = st.mean;
    out->std = st.std;

    /*
     * Filter: the median for batches big enough to have one, else the mean.
     * The mean and median are both carried in the log, so the data can show
     * whether the choice matters — if the two agree within the noise, a plain
     * mean was enough and there is no need to pretend otherwise.
     */
    out->raw = (taken >= 5) ? st.median : (int)(st.mean + 0.5);

    /* A filtered value at either ADC limit may be clipped. Keep the raw data,
     * but do not turn that limit into a moisture index. */
    out->saturated = out->raw == 0 || out->raw == SOIL_ADC_MAX_RAW;
    if (out->saturated) {
        if (!s_saturation_warned) {
            ESP_LOGW(TAG, "filtered raw at ADC limit %d: possible saturation "
                          "or wiring issue (not interpreted as moisture)",
                     out->raw);
            s_saturation_warned = true;
        }
    } else {
        s_saturation_warned = false;
    }

    if (s_cali_ok) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, out->raw, &mv) == ESP_OK) {
            out->millivolts = mv;
            out->voltage_valid = true;
        }
    }

    if (!out->saturated && soil_moisture_calibrated()) {
        soil_moisture_calib_t calib = {
            .dry_raw = CONFIG_AS_SOIL_DRY_RAW,
            .wet_raw = CONFIG_AS_SOIL_WET_RAW,
        };
        if (soil_moisture_normalize(&calib, out->raw,
                                    &out->normalized, &out->unclamped) == 0) {
            out->normalized_valid = true;
        }
    }

    return 0;
}
