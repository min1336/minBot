#include "ota_manager.h"
#include "config.h"

#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

static const char* TAG = "ota_mgr";

namespace minbot::network {

static int s_last_http_status = 0;

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        // Check for version header if server provides it
        if (strcasecmp(evt->header_key, "X-Firmware-Version") == 0) {
            ESP_LOGI(TAG, "Server firmware version: %s", evt->header_value);
        }
    }
    return ESP_OK;
}

int ota_check_update(const char* url) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.skip_cert_common_name_check = true;  // adjust for production

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return -1;
    }

    // HEAD request to check if update is available
    esp_http_client_set_method(client, HTTP_METHOD_HEAD);
    esp_err_t err = esp_http_client_perform(client);
    s_last_http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP check failed: %s", esp_err_to_name(err));
        return -1;
    }

    if (s_last_http_status == 200) {
        ESP_LOGI(TAG, "Update available at: %s", url);
        return 1;  // Update available
    } else if (s_last_http_status == 304) {
        ESP_LOGI(TAG, "Firmware is up to date");
        return 0;  // No update
    }

    ESP_LOGW(TAG, "Unexpected HTTP status: %d", s_last_http_status);
    return -1;
}

int ota_start_update(const char* url, OtaProgressCallback on_progress) {
    ESP_LOGI(TAG, "Starting OTA update from: %s", url);

    esp_http_client_config_t http_config = {};
    http_config.url = url;
    http_config.skip_cert_common_name_check = true;
    http_config.keep_alive_enable = true;
    http_config.buffer_size = 4096;
    http_config.buffer_size_tx = 2048;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &http_config;

    esp_https_ota_handle_t ota_handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        return -1;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(ota_handle, &app_desc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "New firmware version: %s", app_desc.version);
    }

    int image_len = esp_https_ota_get_image_size(ota_handle);
    int downloaded = 0;

    while (true) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        downloaded = esp_https_ota_get_image_len_read(ota_handle);
        if (on_progress && image_len > 0) {
            int percent = (downloaded * 100) / image_len;
            on_progress(percent);
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        return -1;
    }

    bool image_valid = esp_https_ota_is_complete_data_received(ota_handle);
    esp_err_t finish_err = esp_https_ota_finish(ota_handle);

    if (!image_valid || finish_err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed or image incomplete");
        return -1;
    }

    if (on_progress) on_progress(100);
    ESP_LOGI(TAG, "OTA update complete. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return 0;  // unreachable after restart
}

void ota_rollback() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Rolling back to previous firmware");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        } else {
            ESP_LOGI(TAG, "Marking current firmware as valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    } else {
        ESP_LOGW(TAG, "Could not get OTA partition state");
    }
}

} // namespace minbot::network
