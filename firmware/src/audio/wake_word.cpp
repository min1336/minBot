/**
 * wake_word.cpp - ESP-SR WakeNet wake word detection for minBot
 *
 * Uses esp_wn_iface_t from ESP-SR library.
 * Processes 30ms audio frames at 16kHz (480 samples per frame).
 * Triggers a registered callback on detection.
 *
 * NOTE: Custom wake words (e.g. "hey minbot") require training via
 * Espressif's ESP-Skainet toolchain or the EspTouch cloud service.
 * The default model shipped with ESP-SR supports "Hi ESP" and a small
 * set of built-in words. Replace WAKENET_MODEL_NAME and the model
 * coefficient header with your trained artefacts for a custom word.
 *
 * Toolchain reference:
 *   https://github.com/espressif/esp-skainet
 *   https://github.com/espressif/esp-sr/blob/master/docs/wake_word_engine/README.md
 */

#include "wake_word.h"
#include "../config.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"    // provides esp_sr_wn_get() / WAKENET_MODEL_NAME
#include "esp_mn_iface.h"     // multi-net interface (included for completeness)
#include "model_path.h"       // srmodel_spiffs_get_model_path() helper

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "wake_word";

// --- Audio constants ---
// 30ms frame at 16kHz = 480 samples
static constexpr int FRAME_MS      = 30;
static constexpr int SAMPLE_RATE   = 16000;
static constexpr int FRAME_SAMPLES = (SAMPLE_RATE * FRAME_MS) / 1000; // 480

// --- ESP-SR state ---
static const esp_wn_iface_t* s_wn_iface  = nullptr;
static model_iface_data_t*   s_wn_handle = nullptr;
static int                   s_chunk_sz  = 0;

// --- Callback ---
static minbot::audio::WakeWordCallback s_callback = nullptr;

namespace minbot::audio {

int wake_word_init() {
    if (s_wn_handle != nullptr) {
        ESP_LOGW(TAG, "wake word engine already initialised");
        return ESP_OK;
    }

    // Obtain the WakeNet interface for the bundled model.
    // esp_sr_wn_get() selects the model compiled into the firmware via
    // the ESP-SR component's Kconfig (CONFIG_SR_WN_WN9_HILEXIN etc.).
    s_wn_iface = esp_sr_wn_get();
    if (s_wn_iface == nullptr) {
        ESP_LOGE(TAG, "esp_sr_wn_get() returned NULL — is esp-sr in the build?");
        return ESP_ERR_NOT_FOUND;
    }

    // Resolve model path from SPIFFS (or embedded flash).
    // On platforms without SPIFFS, model data is linked into firmware directly.
    char* model_path = srmodel_spiffs_get_model_path(ESP_SR_MODEL_WN9_HILEXIN);
    // model_path may be NULL if the model is linked statically; the iface handles that.

    s_wn_handle = s_wn_iface->create(model_path, DET_MODE_90);
    if (model_path) {
        free(model_path);
    }
    if (s_wn_handle == nullptr) {
        ESP_LOGE(TAG, "WakeNet create() failed — check model and heap");
        return ESP_ERR_NO_MEM;
    }

    // The engine reports the exact chunk size it needs; use it for validation.
    s_chunk_sz = s_wn_iface->get_samp_chunksize(s_wn_handle);
    ESP_LOGI(TAG, "wake word engine ready (chunk=%d samples, frame=%dms)",
             s_chunk_sz, FRAME_MS);

    // Sanity: our FRAME_SAMPLES should match what the model expects.
    if (s_chunk_sz != FRAME_SAMPLES) {
        ESP_LOGW(TAG, "model chunk size %d != expected %d; "
                      "caller must supply exactly %d samples per detect call",
                 s_chunk_sz, FRAME_SAMPLES, s_chunk_sz);
    }

    return ESP_OK;
}

bool wake_word_detect(const int16_t* pcm_data, int samples) {
    if (s_wn_handle == nullptr || s_wn_iface == nullptr) {
        ESP_LOGE(TAG, "wake word engine not initialised");
        return false;
    }
    if (pcm_data == nullptr || samples != s_chunk_sz) {
        ESP_LOGW(TAG, "detect called with %d samples, expected %d", samples, s_chunk_sz);
        return false;
    }

    // run_detect returns the wake word index (>0) on detection, 0 otherwise.
    int result = s_wn_iface->detect(s_wn_handle,
                                    const_cast<int16_t*>(pcm_data));
    if (result > 0) {
        ESP_LOGI(TAG, "wake word detected (index=%d)", result);
        if (s_callback) {
            s_callback();
        }
        return true;
    }
    return false;
}

void wake_word_on_detect(WakeWordCallback cb) {
    s_callback = cb;
}

void wake_word_deinit() {
    if (s_wn_handle != nullptr && s_wn_iface != nullptr) {
        s_wn_iface->destroy(s_wn_handle);
        s_wn_handle = nullptr;
    }
    s_wn_iface  = nullptr;
    s_callback  = nullptr;
    s_chunk_sz  = 0;
    ESP_LOGI(TAG, "wake word engine deinitialised");
}

} // namespace minbot::audio
