#include "ws_client.h"
#include "config.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

static const char* TAG = "ws_client";

#define RECONNECT_DELAY_MS  2000
#define SEND_TIMEOUT_MS     5000

namespace minbot::network {

static esp_websocket_client_handle_t s_client = nullptr;
static AudioCallback s_audio_cb = nullptr;
static JsonCallback s_json_cb = nullptr;
static bool s_connected = false;
static char s_uri[256] = {};

static void websocket_event_handler(void* handler_args, esp_event_base_t base,
                                    int32_t event_id, void* event_data) {
    esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            s_connected = true;
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            s_connected = false;
            // Auto-reconnect after delay
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
            if (s_client) {
                esp_websocket_client_start(s_client);
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x02) {
                // Binary frame: audio data
                if (s_audio_cb && data->data_len > 0) {
                    s_audio_cb((const uint8_t*)data->data_ptr, data->data_len);
                }
            } else if (data->op_code == 0x01) {
                // Text frame: JSON control message
                if (s_json_cb && data->data_len > 0) {
                    s_json_cb(data->data_ptr, data->data_len);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            s_connected = false;
            break;

        default:
            break;
    }
}

int ws_init(const char* uri) {
    if (s_client) {
        ESP_LOGW(TAG, "ws_init called while already initialized");
        return 0;
    }

    strncpy(s_uri, uri, sizeof(s_uri) - 1);

    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = s_uri;
    ws_cfg.reconnect_timeout_ms = RECONNECT_DELAY_MS;
    ws_cfg.network_timeout_ms = SEND_TIMEOUT_MS;

    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        return -1;
    }

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                  websocket_event_handler, nullptr);

    ESP_LOGI(TAG, "WebSocket client initialized: %s", s_uri);
    return 0;
}

int ws_connect() {
    if (!s_client) {
        ESP_LOGE(TAG, "ws_connect called before ws_init");
        return -1;
    }

    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_websocket_client_start failed: %s", esp_err_to_name(err));
        return -1;
    }

    // Wait up to 5s for connection
    for (int i = 0; i < 50; i++) {
        if (s_connected) return 0;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGE(TAG, "WebSocket connection timeout");
    return -1;
}

bool ws_is_connected() {
    return s_connected && s_client && esp_websocket_client_is_connected(s_client);
}

int ws_send_binary(const uint8_t* data, size_t len) {
    if (!ws_is_connected()) {
        ESP_LOGW(TAG, "ws_send_binary: not connected");
        return -1;
    }

    int sent = esp_websocket_client_send_bin(s_client, (const char*)data, len,
                                             pdMS_TO_TICKS(SEND_TIMEOUT_MS));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send binary frame");
        return -1;
    }
    return 0;
}

int ws_send_text(const char* text) {
    if (!ws_is_connected()) {
        ESP_LOGW(TAG, "ws_send_text: not connected");
        return -1;
    }

    int sent = esp_websocket_client_send_text(s_client, text, strlen(text),
                                              pdMS_TO_TICKS(SEND_TIMEOUT_MS));
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send text frame");
        return -1;
    }
    return 0;
}

void ws_on_binary(AudioCallback cb) {
    s_audio_cb = cb;
}

void ws_on_text(JsonCallback cb) {
    s_json_cb = cb;
}

void ws_disconnect() {
    s_connected = false;
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = nullptr;
    }
    s_audio_cb = nullptr;
    s_json_cb = nullptr;
    ESP_LOGI(TAG, "WebSocket disconnected and destroyed");
}

} // namespace minbot::network
