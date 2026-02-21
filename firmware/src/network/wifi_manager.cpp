#include "wifi_manager.h"
#include "config.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <cstring>

static const char* TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MAX_RETRY           10

namespace minbot::network {

static EventGroupHandle_t s_wifi_event_group = nullptr;
static WifiCallback s_state_cb = nullptr;
static int s_retry_count = 0;
static bool s_initialized = false;
static char s_ssid[33] = {};
static char s_password[65] = {};

// Exponential backoff delays in ms: 1s, 2s, 4s, 8s, 16s, 32s (capped)
static uint32_t backoff_ms(int attempt) {
    uint32_t delay = 1000u << (attempt < 6 ? attempt : 5);
    return delay;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < MAX_RETRY) {
            uint32_t delay = backoff_ms(s_retry_count);
            ESP_LOGW(TAG, "Disconnected. Retry %d/%d in %lums",
                     s_retry_count + 1, MAX_RETRY, (unsigned long)delay);
            vTaskDelay(pdMS_TO_TICKS(delay));
            esp_wifi_connect();
            s_retry_count++;
        } else {
            ESP_LOGE(TAG, "Max retries reached. Giving up.");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        if (s_state_cb) s_state_cb(false);

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_state_cb) s_state_cb(true);
    }
}

int wifi_init() {
    if (s_initialized) return 0;

    // Initialize NVS (required for WiFi)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return -1;
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Load saved credentials from NVS if available
    nvs_handle_t nvs;
    if (nvs_open("wifi_cfg", NVS_READONLY, &nvs) == ESP_OK) {
        size_t ssid_len = sizeof(s_ssid);
        size_t pass_len = sizeof(s_password);
        nvs_get_str(nvs, "ssid", s_ssid, &ssid_len);
        nvs_get_str(nvs, "password", s_password, &pass_len);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Loaded saved SSID: %s", s_ssid);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return 0;
}

int wifi_connect(const char* ssid, const char* password) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "wifi_connect called before wifi_init");
        return -1;
    }

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    strncpy(s_password, password, sizeof(s_password) - 1);

    // Persist credentials to NVS
    nvs_handle_t nvs;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "ssid", s_ssid);
        nvs_set_str(nvs, "password", s_password);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    wifi_config_t wifi_cfg = {};
    strncpy((char*)wifi_cfg.sta.ssid, s_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char*)wifi_cfg.sta.password, s_password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(30000)  // 30s timeout
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", s_ssid);
        return 0;
    }
    ESP_LOGE(TAG, "Failed to connect to SSID: %s", s_ssid);
    return -1;
}

bool wifi_is_connected() {
    if (!s_wifi_event_group) return false;
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

int wifi_get_rssi() {
    wifi_ap_record_t ap_info = {};
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return -127;
    }
    return ap_info.rssi;
}

void wifi_on_state_change(WifiCallback cb) {
    s_state_cb = cb;
}

void wifi_deinit() {
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = nullptr;
    }
    s_initialized = false;
    s_state_cb = nullptr;
    ESP_LOGI(TAG, "WiFi manager deinitialized");
}

} // namespace minbot::network
