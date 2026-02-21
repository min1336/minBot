/**
 * minBot Firmware - Main Entry Point
 *
 * FreeRTOS dual-core task architecture:
 *   Core 0: Wi-Fi + WebSocket + BLE (network I/O)
 *   Core 1: Audio pipeline + Display + Sensors (real-time)
 *
 * State machine (XiaoZhi-style):
 *   STARTING → WIFI_CONNECTING → IDLE → LISTENING → THINKING → SPEAKING
 *                                  ↕                              ↓
 *                               SLEEPING                    (back to IDLE)
 */

#include <cstring>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "config.h"
#include "audio/mic_driver.h"
#include "audio/speaker_driver.h"
#include "audio/wake_word.h"
#include "display/face_engine.h"
#include "display/emotions.h"
#include "sensors/gyro.h"
#include "network/wifi_manager.h"
#include "network/ws_client.h"
#include "network/ble_service.h"
#include "network/ota_manager.h"
#include "power/battery.h"

static const char* TAG = "minbot";

// ---------------------------------------------------------------------------
// Application State Machine
// ---------------------------------------------------------------------------

enum class AppState : uint8_t {
    STARTING,
    WIFI_CONNECTING,
    IDLE,
    LISTENING,
    THINKING,
    SPEAKING,
    SLEEPING,
};

static AppState s_state = AppState::STARTING;
static EventGroupHandle_t s_events = nullptr;
static uint32_t s_idle_timer_ms = 0;

// Event bits
#define EVT_WAKE_WORD     BIT0
#define EVT_WIFI_CONNECTED BIT1
#define EVT_WIFI_LOST     BIT2
#define EVT_WS_AUDIO_IN   BIT3
#define EVT_WS_JSON_IN    BIT4
#define EVT_BARGE_IN      BIT5

// Shared buffers (PSRAM)
static int16_t* s_mic_buf = nullptr;
static uint8_t* s_ws_rx_buf = nullptr;
static size_t s_ws_rx_len = 0;
static char* s_json_buf = nullptr;
static size_t s_json_len = 0;

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

static void set_state(AppState new_state) {
    if (s_state == new_state) return;

    ESP_LOGI(TAG, "State: %d → %d", (int)s_state, (int)new_state);
    s_state = new_state;
    s_idle_timer_ms = 0;

    using namespace minbot::display;
    switch (new_state) {
        case AppState::STARTING:     emotion_set(Emotion::IDLE); break;
        case AppState::WIFI_CONNECTING: emotion_set(Emotion::THINKING); break;
        case AppState::IDLE:         emotion_set(Emotion::IDLE); break;
        case AppState::LISTENING:    emotion_set(Emotion::LISTENING); break;
        case AppState::THINKING:     emotion_set(Emotion::THINKING); break;
        case AppState::SPEAKING:     emotion_set(Emotion::SPEAKING); break;
        case AppState::SLEEPING:     emotion_set(Emotion::SLEEPING); break;
    }
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

static void on_wake_word() {
    ESP_LOGI(TAG, "Wake word detected!");
    xEventGroupSetBits(s_events, EVT_WAKE_WORD);
}

static void on_wifi_state(bool connected) {
    if (connected) {
        xEventGroupSetBits(s_events, EVT_WIFI_CONNECTED);
    } else {
        xEventGroupSetBits(s_events, EVT_WIFI_LOST);
    }
}

static void on_ws_audio(const uint8_t* data, size_t len) {
    if (len > 0 && s_ws_rx_buf != nullptr) {
        size_t copy_len = len < (DMA_BUF_LEN * DMA_BUF_COUNT * 2) ? len : (DMA_BUF_LEN * DMA_BUF_COUNT * 2);
        memcpy(s_ws_rx_buf, data, copy_len);
        s_ws_rx_len = copy_len;
        xEventGroupSetBits(s_events, EVT_WS_AUDIO_IN);
    }
}

static void on_ws_json(const char* json, size_t len) {
    if (len > 0 && s_json_buf != nullptr) {
        size_t copy_len = len < 1023 ? len : 1023;
        memcpy(s_json_buf, json, copy_len);
        s_json_buf[copy_len] = '\0';
        s_json_len = copy_len;
        xEventGroupSetBits(s_events, EVT_WS_JSON_IN);
    }
}

static void on_tilt(float angle) {
    ESP_LOGI(TAG, "Tilt detected: %.1f°", angle);
    if (s_state == AppState::SLEEPING) {
        set_state(AppState::IDLE);
    }
    minbot::display::emotion_set(minbot::display::Emotion::TILTED);
}

// ---------------------------------------------------------------------------
// Audio Task (Core 1) — mic capture + wake word + streaming to server
// ---------------------------------------------------------------------------

static void audio_task(void* arg) {
    ESP_LOGI(TAG, "Audio task started on core %d", xPortGetCoreID());

    const size_t frame_bytes = DMA_BUF_LEN * sizeof(int16_t);

    while (true) {
        size_t bytes_read = 0;
        int err = minbot::audio::mic_read(s_mic_buf, frame_bytes, &bytes_read, 100);
        if (err != 0 || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int samples = bytes_read / sizeof(int16_t);

        switch (s_state) {
            case AppState::IDLE:
            case AppState::SLEEPING:
                // Feed audio to wake word detector
                if (minbot::audio::wake_word_detect(s_mic_buf, samples)) {
                    // Callback handles event
                }
                break;

            case AppState::LISTENING:
                // Stream audio to server via WebSocket
                if (minbot::network::ws_is_connected()) {
                    minbot::network::ws_send_binary(
                        reinterpret_cast<const uint8_t*>(s_mic_buf), bytes_read
                    );
                }
                break;

            case AppState::SPEAKING:
                // During playback, still listen for barge-in via wake word
                if (minbot::audio::wake_word_detect(s_mic_buf, samples)) {
                    ESP_LOGI(TAG, "Barge-in via wake word during playback");
                    xEventGroupSetBits(s_events, EVT_BARGE_IN);
                }
                break;

            default:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---------------------------------------------------------------------------
// Display Task (Core 1) — face rendering at 30fps
// ---------------------------------------------------------------------------

static void display_task(void* arg) {
    ESP_LOGI(TAG, "Display task started on core %d", xPortGetCoreID());

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t frame_period = pdMS_TO_TICKS(33);  // ~30fps

    while (true) {
        minbot::display::emotion_update(33);
        minbot::display::face_update();
        vTaskDelayUntil(&last_wake, frame_period);
    }
}

// ---------------------------------------------------------------------------
// Sensor Task (Core 1) — gyroscope polling
// ---------------------------------------------------------------------------

static void sensor_task(void* arg) {
    ESP_LOGI(TAG, "Sensor task started on core %d", xPortGetCoreID());

    while (true) {
        auto data = minbot::sensors::gyro_read();
        (void)data;  // Tilt callback handles events

        // Battery check
        int batt = minbot::power::battery_get_percent();
        if (batt <= CRIT_BATT_THRESHOLD) {
            ESP_LOGW(TAG, "Critical battery (%d%%), entering deep sleep", batt);
            minbot::power::battery_enter_deep_sleep();
        } else if (batt <= LOW_BATT_THRESHOLD) {
            ESP_LOGW(TAG, "Low battery (%d%%)", batt);
        }

        // Update BLE battery level
        if (minbot::network::ble_is_connected()) {
            minbot::network::ble_send_battery_level(static_cast<uint8_t>(batt));
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz
    }
}

// ---------------------------------------------------------------------------
// Network Task (Core 0) — event processing + state machine
// ---------------------------------------------------------------------------

static void network_task(void* arg) {
    ESP_LOGI(TAG, "Network task started on core %d", xPortGetCoreID());

    while (true) {
        EventBits_t bits = xEventGroupWaitBits(
            s_events,
            EVT_WAKE_WORD | EVT_WIFI_CONNECTED | EVT_WIFI_LOST |
            EVT_WS_AUDIO_IN | EVT_WS_JSON_IN | EVT_BARGE_IN,
            pdTRUE,   // clear on exit
            pdFALSE,  // wait for any bit
            pdMS_TO_TICKS(100)
        );

        // --- Wi-Fi events ---
        if (bits & EVT_WIFI_CONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi connected, connecting WebSocket...");
            minbot::network::ws_connect();
            set_state(AppState::IDLE);
        }

        if (bits & EVT_WIFI_LOST) {
            ESP_LOGW(TAG, "Wi-Fi lost, attempting reconnect...");
            set_state(AppState::WIFI_CONNECTING);
        }

        // --- Wake word ---
        if (bits & EVT_WAKE_WORD) {
            if (s_state == AppState::IDLE || s_state == AppState::SLEEPING) {
                set_state(AppState::LISTENING);
            }
        }

        // --- Barge-in ---
        if (bits & EVT_BARGE_IN) {
            if (s_state == AppState::SPEAKING || s_state == AppState::THINKING) {
                ESP_LOGI(TAG, "Barge-in: stopping playback, restarting listen");
                minbot::audio::spk_stop_and_clear();
                // Notify server
                minbot::network::ws_send_text("{\"type\":\"barge_in\"}");
                set_state(AppState::LISTENING);
            }
        }

        // --- Incoming audio from server (TTS response) ---
        if (bits & EVT_WS_AUDIO_IN) {
            if (s_state == AppState::THINKING || s_state == AppState::SPEAKING) {
                set_state(AppState::SPEAKING);
                size_t written = 0;
                minbot::audio::spk_write(
                    reinterpret_cast<const int16_t*>(s_ws_rx_buf),
                    s_ws_rx_len, &written, 100
                );

                // Calculate volume for lip sync
                int32_t sum = 0;
                int sample_count = s_ws_rx_len / 2;
                const int16_t* samples = reinterpret_cast<const int16_t*>(s_ws_rx_buf);
                for (int i = 0; i < sample_count; i++) {
                    int32_t s = samples[i] < 0 ? -samples[i] : samples[i];
                    sum += s;
                }
                uint8_t level = sample_count > 0
                    ? static_cast<uint8_t>((sum / sample_count) >> 7)
                    : 0;
                minbot::display::emotion_set_audio_level(level);
            }
        }

        // --- Incoming JSON from server ---
        if (bits & EVT_WS_JSON_IN) {
            // Simple JSON parsing for control messages
            if (strstr(s_json_buf, "\"cancel_playback\"")) {
                ESP_LOGI(TAG, "Server requested cancel_playback");
                minbot::audio::spk_stop_and_clear();
                set_state(AppState::IDLE);
            }
            else if (strstr(s_json_buf, "\"emotion\"")) {
                // Parse emotion from: {"type":"emotion","emotion":"happy"}
                const char* e = strstr(s_json_buf, "\"emotion\":\"");
                if (e) {
                    e += 11;  // skip past "emotion":"
                    using namespace minbot::display;
                    if (strncmp(e, "happy", 5) == 0) emotion_set(Emotion::HAPPY);
                    else if (strncmp(e, "sad", 3) == 0) emotion_set(Emotion::SAD);
                    else if (strncmp(e, "surprised", 9) == 0) emotion_set(Emotion::SURPRISED);
                    else if (strncmp(e, "thinking", 8) == 0) emotion_set(Emotion::THINKING);
                    else if (strncmp(e, "speaking", 8) == 0) emotion_set(Emotion::SPEAKING);
                    else if (strncmp(e, "idle", 4) == 0) emotion_set(Emotion::IDLE);
                }
            }
            else if (strstr(s_json_buf, "\"status\"")) {
                // Pipeline complete → back to idle
                if (strstr(s_json_buf, "\"pipeline_done\"")) {
                    set_state(AppState::IDLE);
                }
            }
        }

        // --- Idle timeout → sleep ---
        if (bits == 0 && s_state == AppState::IDLE) {
            s_idle_timer_ms += 100;
            if (s_idle_timer_ms >= SLEEP_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Idle timeout, entering sleep mode");
                set_state(AppState::SLEEPING);
                minbot::display::face_set_brightness(30);  // dim display
            }
        }

        // --- Listening timeout (no speech detected for 10s) ---
        if (bits == 0 && s_state == AppState::LISTENING) {
            s_idle_timer_ms += 100;
            if (s_idle_timer_ms >= 10000) {
                ESP_LOGI(TAG, "Listening timeout, back to idle");
                set_state(AppState::IDLE);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// app_main — Entry Point
// ---------------------------------------------------------------------------

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== minBot Starting ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // NVS init (required for Wi-Fi + BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Event group for inter-task communication
    s_events = xEventGroupCreate();

    // Allocate PSRAM buffers
    s_mic_buf = static_cast<int16_t*>(
        heap_caps_malloc(DMA_BUF_LEN * sizeof(int16_t), MALLOC_CAP_SPIRAM)
    );
    s_ws_rx_buf = static_cast<uint8_t*>(
        heap_caps_malloc(DMA_BUF_LEN * DMA_BUF_COUNT * 2, MALLOC_CAP_SPIRAM)
    );
    s_json_buf = static_cast<char*>(
        heap_caps_malloc(1024, MALLOC_CAP_SPIRAM)
    );

    if (!s_mic_buf || !s_ws_rx_buf || !s_json_buf) {
        ESP_LOGE(TAG, "PSRAM allocation failed!");
        esp_restart();
    }

    // --- Initialize hardware ---
    ESP_LOGI(TAG, "Initializing hardware...");

    set_state(AppState::STARTING);

    // Display (Core 1)
    minbot::display::face_init();
    minbot::display::emotion_init();

    // Audio (Core 1)
    minbot::audio::mic_init();
    minbot::audio::spk_init();
    minbot::audio::wake_word_init();
    minbot::audio::wake_word_on_detect(on_wake_word);

    // Sensors (Core 1)
    minbot::sensors::gyro_init();
    minbot::sensors::gyro_on_tilt(on_tilt, 30.0f);

    // Battery
    minbot::power::battery_init();
    ESP_LOGI(TAG, "Battery: %d%% (%dmV)",
        minbot::power::battery_get_percent(),
        minbot::power::battery_get_voltage_mv()
    );

    // BLE (start immediately for provisioning)
    minbot::network::ble_init();
    minbot::network::ble_start_advertising();

    // Wi-Fi
    minbot::network::wifi_init();
    minbot::network::wifi_on_state_change(on_wifi_state);
    set_state(AppState::WIFI_CONNECTING);

    // WebSocket callbacks
    minbot::network::ws_init(WS_SERVER_URI);
    minbot::network::ws_on_binary(on_ws_audio);
    minbot::network::ws_on_text(on_ws_json);

    // --- Create FreeRTOS tasks ---
    ESP_LOGI(TAG, "Creating tasks...");

    // Core 1: Real-time tasks
    xTaskCreatePinnedToCore(audio_task, "audio", STACK_AUDIO,
        nullptr, TASK_PRIO_AUDIO, nullptr, 1);

    xTaskCreatePinnedToCore(display_task, "display", STACK_DISPLAY,
        nullptr, TASK_PRIO_DISPLAY, nullptr, 1);

    xTaskCreatePinnedToCore(sensor_task, "sensor", STACK_SENSOR,
        nullptr, TASK_PRIO_SENSOR, nullptr, 1);

    // Core 0: Network task
    xTaskCreatePinnedToCore(network_task, "network", STACK_NETWORK,
        nullptr, TASK_PRIO_NETWORK, nullptr, 0);

    ESP_LOGI(TAG, "=== minBot Ready ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
}
