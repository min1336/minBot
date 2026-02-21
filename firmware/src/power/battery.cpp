#include "battery.h"
#include "config.h"

#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "battery";

// Battery voltage thresholds (in mV)
#define BATTERY_FULL_MV     4200
#define BATTERY_EMPTY_MV    3300
#define BATTERY_CRITICAL_MV 3400  // ~5%

// ADC calibration
#define ADC_ATTEN           ADC_ATTEN_DB_11   // 0-3.9V range
#define ADC_UNIT            ADC_UNIT_1
#define ADC_SAMPLES         16                 // Average over 16 samples

// Voltage divider ratio: resistors R1/R2 step down 4.2V to ADC range
// e.g., 100k + 100k gives ratio 0.5 -> multiply reading by 2.0
#define VDIV_RATIO          2.0f

namespace minbot::power {

static bool s_initialized = false;
static esp_adc_cal_characteristics_t s_adc_chars = {};

int battery_init() {
    if (s_initialized) return 0;

    // Configure ADC channel for battery voltage on BATTERY_ADC_PIN (GPIO34)
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN);

    // Characterize ADC using eFuse or vref for better accuracy
    esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
        ADC_UNIT, ADC_ATTEN, ADC_WIDTH_BIT_12, 1100, &s_adc_chars);

    if (cal_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "ADC calibrated via eFuse Vref");
    } else if (cal_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "ADC calibrated via eFuse two-point");
    } else {
        ESP_LOGW(TAG, "ADC using default calibration (less accurate)");
    }

    // Configure charging detection pin (input with pull-down)
    gpio_config_t io_cfg = {};
    io_cfg.pin_bit_mask = (1ULL << CHARGE_DETECT_PIN);
    io_cfg.mode = GPIO_MODE_INPUT;
    io_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    io_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_cfg);

    s_initialized = true;
    ESP_LOGI(TAG, "Battery module initialized");
    return 0;
}

int battery_get_voltage_mv() {
    if (!s_initialized) return -1;

    // Average multiple ADC samples to reduce noise
    uint32_t adc_sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        adc_sum += adc1_get_raw(BATTERY_ADC_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    uint32_t adc_raw = adc_sum / ADC_SAMPLES;

    // Convert raw ADC to millivolts using calibration
    uint32_t voltage_adc_mv = esp_adc_cal_raw_to_voltage(adc_raw, &s_adc_chars);

    // Apply voltage divider correction
    int voltage_mv = (int)((float)voltage_adc_mv * VDIV_RATIO);

    ESP_LOGD(TAG, "ADC raw=%lu  adc_mv=%lu  battery_mv=%d",
             (unsigned long)adc_raw, (unsigned long)voltage_adc_mv, voltage_mv);
    return voltage_mv;
}

int battery_get_percent() {
    int voltage_mv = battery_get_voltage_mv();
    if (voltage_mv < 0) return -1;

    if (voltage_mv >= BATTERY_FULL_MV) return 100;
    if (voltage_mv <= BATTERY_EMPTY_MV) return 0;

    int percent = ((voltage_mv - BATTERY_EMPTY_MV) * 100) /
                  (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    return percent;
}

bool battery_is_charging() {
    // TP4056 CHRG pin: LOW = charging, HIGH or floating = not charging
    // CHARGE_DETECT_PIN is connected to TP4056 CHRG output
    return gpio_get_level((gpio_num_t)CHARGE_DETECT_PIN) == 0;
}

void battery_enter_light_sleep() {
    ESP_LOGI(TAG, "Entering light sleep (modem sleep)");

    // Keep CPU running but disable modem to save power
    // Wake on timer (5 minutes) or GPIO interrupt
    esp_sleep_enable_timer_wakeup(5ULL * 60 * 1000 * 1000);  // 5 min in us
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();

    ESP_LOGI(TAG, "Woke from light sleep");
}

void battery_enter_deep_sleep() {
    int percent = battery_get_percent();
    ESP_LOGW(TAG, "Battery critical (%d%%). Entering deep sleep.", percent);

    // Wake only on USB charge detection (EXT0 wakeup)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)CHARGE_DETECT_PIN, 0);  // wake on LOW (charging)

    vTaskDelay(pdMS_TO_TICKS(100));  // Allow log flush
    esp_deep_sleep_start();
    // Does not return
}

} // namespace minbot::power
