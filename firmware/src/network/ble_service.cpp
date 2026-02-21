#include "ble_service.h"
#include "config.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <cstring>

static const char* TAG = "ble_svc";

// Standard BLE Battery Service UUID
#define BATTERY_SERVICE_UUID        0x180F
#define BATTERY_LEVEL_CHAR_UUID     0x2A19

// Custom minBot service UUID for WiFi provisioning
// {12345678-1234-1234-1234-123456789ABC}
static uint8_t MINBOT_SERVICE_UUID[16] = {
    0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};
static uint8_t WIFI_SSID_CHAR_UUID[16] = {
    0xBD, 0x9A, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};
static uint8_t WIFI_PASS_CHAR_UUID[16] = {
    0xBE, 0x9A, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};
static uint8_t STATUS_CHAR_UUID[16] = {
    0xBF, 0x9A, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12,
    0x34, 0x12, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

#define GATTS_APP_ID        0
#define GATTS_NUM_HANDLES   10

namespace minbot::network {

static bool s_initialized = false;
static bool s_ble_connected = false;
static uint16_t s_conn_id = 0;
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;

// Attribute handles
static uint16_t s_battery_handle = 0;
static uint16_t s_status_handle = 0;

static uint8_t s_battery_level = 100;
static uint8_t s_device_status = 0x01;  // 0x01 = idle

static uint8_t s_adv_raw_data[] = {
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    0x08, ESP_BLE_AD_TYPE_NAME_CMPL, 'm', 'i', 'n', 'B', 'o', 't', 0
};

static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr          = {0},
    .peer_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t* param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&s_adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Advertising start failed");
            } else {
                ESP_LOGI(TAG, "Advertising started");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            ESP_LOGI(TAG, "Advertising stopped");
            break;
        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            s_gatts_if = gatts_if;
            esp_ble_gap_set_device_name("minBot");
            esp_ble_gap_config_adv_data_raw(s_adv_raw_data, sizeof(s_adv_raw_data));

            // Create battery service (16-bit UUID)
            {
                esp_gatt_srvc_id_t battery_srvc = {};
                battery_srvc.is_primary = true;
                battery_srvc.id.inst_id = 0;
                battery_srvc.id.uuid.len = ESP_UUID_LEN_16;
                battery_srvc.id.uuid.uuid.uuid16 = BATTERY_SERVICE_UUID;
                esp_ble_gatts_create_service(gatts_if, &battery_srvc, GATTS_NUM_HANDLES);
            }
            break;

        case ESP_GATTS_CREATE_EVT:
            esp_ble_gatts_start_service(param->create.service_handle);

            // Add battery level characteristic
            {
                esp_bt_uuid_t char_uuid = {};
                char_uuid.len = ESP_UUID_LEN_16;
                char_uuid.uuid.uuid16 = BATTERY_LEVEL_CHAR_UUID;
                esp_ble_gatts_add_char(param->create.service_handle, &char_uuid,
                                       ESP_GATT_PERM_READ,
                                       ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                       nullptr, nullptr);
            }
            break;

        case ESP_GATTS_ADD_CHAR_EVT:
            if (param->add_char.char_uuid.uuid.uuid16 == BATTERY_LEVEL_CHAR_UUID) {
                s_battery_handle = param->add_char.attr_handle;
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            s_ble_connected = true;
            s_conn_id = param->connect.conn_id;
            ESP_LOGI(TAG, "BLE client connected, conn_id=%d", s_conn_id);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            s_ble_connected = false;
            ESP_LOGI(TAG, "BLE client disconnected");
            esp_ble_gap_start_advertising(&s_adv_params);
            break;

        case ESP_GATTS_WRITE_EVT:
            // Handle WiFi provisioning writes (SSID / password)
            ESP_LOGI(TAG, "GATT write handle=%d len=%d",
                     param->write.handle, param->write.len);
            break;

        case ESP_GATTS_READ_EVT:
            if (param->read.handle == s_battery_handle) {
                esp_gatt_rsp_t rsp = {};
                rsp.attr_value.len = 1;
                rsp.attr_value.value[0] = s_battery_level;
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                            param->read.trans_id, ESP_GATT_OK, &rsp);
            }
            break;

        default:
            break;
    }
}

int ble_init() {
    if (s_initialized) return 0;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "bt_controller_init failed");
        return -1;
    }
    if (esp_bt_controller_enable(ESP_BT_MODE_BLE) != ESP_OK) {
        ESP_LOGE(TAG, "bt_controller_enable failed");
        return -1;
    }
    if (esp_bluedroid_init() != ESP_OK || esp_bluedroid_enable() != ESP_OK) {
        ESP_LOGE(TAG, "bluedroid init/enable failed");
        return -1;
    }

    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(GATTS_APP_ID);

    s_initialized = true;
    ESP_LOGI(TAG, "BLE service initialized");
    return 0;
}

void ble_start_advertising() {
    if (!s_initialized) return;
    esp_ble_gap_config_adv_data_raw(s_adv_raw_data, sizeof(s_adv_raw_data));
}

void ble_stop_advertising() {
    if (!s_initialized) return;
    esp_ble_gap_stop_advertising();
}

bool ble_is_connected() {
    return s_ble_connected;
}

int ble_send_battery_level(uint8_t percent) {
    s_battery_level = percent;
    if (!s_ble_connected || s_battery_handle == 0) return -1;

    esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id, s_battery_handle,
                                 sizeof(percent), &percent, false);
    return 0;
}

void ble_deinit() {
    if (!s_initialized) return;
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    s_initialized = false;
    s_ble_connected = false;
    ESP_LOGI(TAG, "BLE service deinitialized");
}

} // namespace minbot::network
