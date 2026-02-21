/**
 * speaker_driver.cpp - MAX98357A I2S speaker driver for minBot
 *
 * Uses ESP-IDF new I2S standard API (driver/i2s_std.h).
 * I2S_NUM_1, BCK=GPIO26, WS=GPIO25, DOUT=GPIO22
 * 16kHz, 16-bit mono, DMA buf_len=512, buf_count=4
 * spk_stop_and_clear() resets the I2S DMA for immediate barge-in.
 * Pinned to Core 1 via calling task or internal configuration.
 */

#include "speaker_driver.h"
#include "../config.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "spk_driver";

// --- Pin definitions (fallback if not defined in config.h) ---
#ifndef SPK_BCK_GPIO
#define SPK_BCK_GPIO   GPIO_NUM_26
#endif
#ifndef SPK_WS_GPIO
#define SPK_WS_GPIO    GPIO_NUM_25
#endif
#ifndef SPK_DOUT_GPIO
#define SPK_DOUT_GPIO  GPIO_NUM_22
#endif

// --- I2S config constants ---
static constexpr uint32_t SAMPLE_RATE   = 16000;
static constexpr uint32_t DMA_BUF_LEN   = 512;   // samples per DMA buffer
static constexpr uint32_t DMA_BUF_COUNT = 4;

static i2s_chan_handle_t s_tx_chan = nullptr;

// Store config for re-init during stop_and_clear
static i2s_std_config_t s_std_cfg;

namespace minbot::audio {

int spk_init() {
    if (s_tx_chan != nullptr) {
        ESP_LOGW(TAG, "speaker already initialised");
        return ESP_OK;
    }

    // --- Channel configuration ---
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_LEN;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // --- Standard mode (I2S Philips) configuration ---
    s_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = static_cast<gpio_num_t>(SPK_BCK_GPIO),
            .ws   = static_cast<gpio_num_t>(SPK_WS_GPIO),
            .dout = static_cast<gpio_num_t>(SPK_DOUT_GPIO),
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_chan, &s_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = nullptr;
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = nullptr;
        return ret;
    }

    ESP_LOGI(TAG, "speaker initialised (BCK=%d WS=%d DOUT=%d %uHz)",
             SPK_BCK_GPIO, SPK_WS_GPIO, SPK_DOUT_GPIO, SAMPLE_RATE);
    return ESP_OK;
}

int spk_write(const int16_t* buffer, size_t bytes, size_t* bytes_written, uint32_t timeout_ms) {
    if (s_tx_chan == nullptr) {
        ESP_LOGE(TAG, "speaker not initialised");
        return ESP_ERR_INVALID_STATE;
    }
    if (buffer == nullptr || bytes_written == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY)
                     ? portMAX_DELAY
                     : pdMS_TO_TICKS(timeout_ms);

    esp_err_t ret = i2s_channel_write(s_tx_chan, buffer, bytes, bytes_written, ticks);
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "i2s_channel_write error: %s", esp_err_to_name(ret));
    }
    return ret;
}

void spk_stop_and_clear() {
    if (s_tx_chan == nullptr) {
        return;
    }

    // Disable, delete, and re-create the channel to flush all DMA buffers.
    // This is the most reliable way to achieve immediate barge-in silence on ESP-IDF.
    i2s_channel_disable(s_tx_chan);
    i2s_del_channel(s_tx_chan);
    s_tx_chan = nullptr;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_LEN;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spk_stop_and_clear: i2s_new_channel failed: %s", esp_err_to_name(ret));
        s_tx_chan = nullptr;
        return;
    }

    ret = i2s_channel_init_std_mode(s_tx_chan, &s_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spk_stop_and_clear: init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = nullptr;
        return;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spk_stop_and_clear: enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = nullptr;
        return;
    }

    ESP_LOGD(TAG, "speaker DMA cleared (barge-in)");
}

void spk_deinit() {
    if (s_tx_chan == nullptr) {
        return;
    }
    i2s_channel_disable(s_tx_chan);
    i2s_del_channel(s_tx_chan);
    s_tx_chan = nullptr;
    ESP_LOGI(TAG, "speaker deinitialised");
}

} // namespace minbot::audio
