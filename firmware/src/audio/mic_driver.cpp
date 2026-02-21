/**
 * mic_driver.cpp - INMP441 I2S microphone driver for minBot
 *
 * Uses ESP-IDF new I2S standard API (driver/i2s_std.h).
 * I2S_NUM_0, BCK=GPIO14, WS=GPIO15, DIN=GPIO32
 * 16kHz, 16-bit mono, DMA buf_len=512, buf_count=4
 * Pinned to Core 1 via calling task or internal configuration.
 */

#include "mic_driver.h"
#include "../config.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "mic_driver";

// --- Pin definitions (fallback if not defined in config.h) ---
#ifndef MIC_BCK_GPIO
#define MIC_BCK_GPIO   GPIO_NUM_14
#endif
#ifndef MIC_WS_GPIO
#define MIC_WS_GPIO    GPIO_NUM_15
#endif
#ifndef MIC_DIN_GPIO
#define MIC_DIN_GPIO   GPIO_NUM_32
#endif

// --- I2S config constants ---
static constexpr uint32_t SAMPLE_RATE   = 16000;
static constexpr uint32_t DMA_BUF_LEN   = 512;   // samples per DMA buffer
static constexpr uint32_t DMA_BUF_COUNT = 4;

static i2s_chan_handle_t s_rx_chan = nullptr;

namespace minbot::audio {

int mic_init() {
    if (s_rx_chan != nullptr) {
        ESP_LOGW(TAG, "mic already initialised");
        return ESP_OK;
    }

    // --- Channel configuration ---
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_LEN;

    esp_err_t ret = i2s_new_channel(&chan_cfg, nullptr, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // --- Standard mode (I2S Philips) configuration ---
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(MIC_BCK_GPIO),
            .ws   = static_cast<gpio_num_t>(MIC_WS_GPIO),
            .dout = I2S_GPIO_UNUSED,
            .din  = static_cast<gpio_num_t>(MIC_DIN_GPIO),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // INMP441 outputs on the left channel; select left slot
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ret = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = nullptr;
        return ret;
    }

    ret = i2s_channel_enable(s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_chan);
        s_rx_chan = nullptr;
        return ret;
    }

    ESP_LOGI(TAG, "mic initialised (BCK=%d WS=%d DIN=%d %uHz)",
             MIC_BCK_GPIO, MIC_WS_GPIO, MIC_DIN_GPIO, SAMPLE_RATE);
    return ESP_OK;
}

int mic_read(int16_t* buffer, size_t max_bytes, size_t* bytes_read, uint32_t timeout_ms) {
    if (s_rx_chan == nullptr) {
        ESP_LOGE(TAG, "mic not initialised");
        return ESP_ERR_INVALID_STATE;
    }
    if (buffer == nullptr || bytes_read == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    esp_err_t ret = i2s_channel_read(s_rx_chan, buffer, max_bytes, bytes_read, ticks);
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "i2s_channel_read error: %s", esp_err_to_name(ret));
    }
    return ret;
}

void mic_deinit() {
    if (s_rx_chan == nullptr) {
        return;
    }
    i2s_channel_disable(s_rx_chan);
    i2s_del_channel(s_rx_chan);
    s_rx_chan = nullptr;
    ESP_LOGI(TAG, "mic deinitialised");
}

} // namespace minbot::audio
