#include "gyro.h"
#include "config.h"
#include <cmath>
#include <cstring>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "gyro";

// MPU6050 register addresses
#define MPU6050_ADDR        0x68
#define REG_PWR_MGMT_1      0x6B
#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_ACCEL_XOUT_H    0x3B
#define REG_TEMP_OUT_H      0x41
#define REG_GYRO_XOUT_H     0x43
#define REG_WHO_AM_I        0x75

// Scale factors
#define ACCEL_SCALE_2G      16384.0f  // LSB/g for ±2g
#define GYRO_SCALE_250      131.0f    // LSB/(deg/s) for ±250 deg/s

#define I2C_TIMEOUT_MS      100
#define MAX_REINIT_ATTEMPTS 3

namespace minbot::sensors {

static bool s_initialized = false;
static TiltCallback s_tilt_cb = nullptr;
static float s_tilt_threshold = 30.0f;
static TaskHandle_t s_tilt_task = nullptr;

static esp_err_t i2c_write_byte(uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_bytes(uint8_t reg, uint8_t* buf, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6050_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static int mpu6050_configure() {
    // Wake up MPU6050 (clear sleep bit)
    if (i2c_write_byte(REG_PWR_MGMT_1, 0x00) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake MPU6050");
        return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Sample rate divider: 1kHz / (1 + 7) = 125Hz
    i2c_write_byte(REG_SMPLRT_DIV, 0x07);
    // DLPF config: 44Hz bandwidth
    i2c_write_byte(REG_CONFIG, 0x03);
    // Gyro range ±250 deg/s
    i2c_write_byte(REG_GYRO_CONFIG, 0x00);
    // Accel range ±2g
    i2c_write_byte(REG_ACCEL_CONFIG, 0x00);

    // Verify WHO_AM_I
    uint8_t who_am_i = 0;
    if (i2c_read_bytes(REG_WHO_AM_I, &who_am_i, 1) != ESP_OK || who_am_i != 0x68) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: 0x%02X", who_am_i);
        return -1;
    }

    ESP_LOGI(TAG, "MPU6050 configured (WHO_AM_I=0x%02X)", who_am_i);
    return 0;
}

static void tilt_monitor_task(void* arg) {
    while (true) {
        if (s_tilt_cb && s_initialized) {
            float angle = gyro_get_tilt();
            if (fabsf(angle) > s_tilt_threshold) {
                s_tilt_cb(angle);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz polling for tilt events
    }
}

int gyro_init() {
    if (s_initialized) {
        return 0;
    }

    // Configure I2C master
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = GYRO_SDA_PIN;   // GPIO21 from config.h
    conf.scl_io_num = GYRO_SCL_PIN;   // GPIO19 from config.h
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;   // 400kHz

    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return -1;
    }

    // Attempt MPU6050 configuration with retries
    int attempt = 0;
    while (attempt < MAX_REINIT_ATTEMPTS) {
        if (mpu6050_configure() == 0) {
            break;
        }
        attempt++;
        ESP_LOGW(TAG, "MPU6050 init attempt %d/%d failed", attempt, MAX_REINIT_ATTEMPTS);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (attempt == MAX_REINIT_ATTEMPTS) {
        ESP_LOGE(TAG, "MPU6050 init failed after %d attempts, disabling gyro", MAX_REINIT_ATTEMPTS);
        i2c_driver_delete(I2C_NUM_0);
        return -1;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Gyro initialized");
    return 0;
}

GyroData gyro_read() {
    GyroData data = {};

    if (!s_initialized) {
        ESP_LOGW(TAG, "gyro_read called before init");
        return data;
    }

    // Read 14 bytes: accel (6) + temp (2) + gyro (6)
    uint8_t raw[14] = {};
    if (i2c_read_bytes(REG_ACCEL_XOUT_H, raw, 14) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MPU6050 data");

        // Attempt reinit
        int attempt = 0;
        while (attempt < MAX_REINIT_ATTEMPTS) {
            if (mpu6050_configure() == 0) break;
            attempt++;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (attempt == MAX_REINIT_ATTEMPTS) {
            ESP_LOGE(TAG, "Reinit failed, disabling gyro");
            s_initialized = false;
        }
        return data;
    }

    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
    int16_t tmp = (int16_t)((raw[6] << 8) | raw[7]);
    int16_t gx = (int16_t)((raw[8] << 8) | raw[9]);
    int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);

    data.accel_x = ax / ACCEL_SCALE_2G;
    data.accel_y = ay / ACCEL_SCALE_2G;
    data.accel_z = az / ACCEL_SCALE_2G;
    data.gyro_x  = gx / GYRO_SCALE_250;
    data.gyro_y  = gy / GYRO_SCALE_250;
    data.gyro_z  = gz / GYRO_SCALE_250;
    data.temperature = (tmp / 340.0f) + 36.53f;

    // Tilt angle from vertical: angle between gravity vector and Z axis
    float magnitude = sqrtf(data.accel_x * data.accel_x +
                            data.accel_y * data.accel_y +
                            data.accel_z * data.accel_z);
    if (magnitude > 0.0f) {
        data.tilt_angle = acosf(fabsf(data.accel_z) / magnitude) * (180.0f / M_PI);
    }

    return data;
}

float gyro_get_tilt() {
    GyroData d = gyro_read();
    return d.tilt_angle;
}

void gyro_on_tilt(TiltCallback cb, float threshold_degrees) {
    s_tilt_cb = cb;
    s_tilt_threshold = threshold_degrees;

    if (s_tilt_task == nullptr) {
        xTaskCreatePinnedToCore(
            tilt_monitor_task,
            "gyro_tilt",
            2048,
            nullptr,
            8,          // Priority 8 (Sensor priority from spec)
            &s_tilt_task,
            1           // Core 1
        );
    }
}

void gyro_deinit() {
    if (s_tilt_task) {
        vTaskDelete(s_tilt_task);
        s_tilt_task = nullptr;
    }
    s_tilt_cb = nullptr;
    if (s_initialized) {
        i2c_driver_delete(I2C_NUM_0);
        s_initialized = false;
    }
    ESP_LOGI(TAG, "Gyro deinitialized");
}

} // namespace minbot::sensors
