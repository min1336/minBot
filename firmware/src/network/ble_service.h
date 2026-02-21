#pragma once

namespace minbot::network {

int ble_init();
void ble_start_advertising();
void ble_stop_advertising();
bool ble_is_connected();
int ble_send_battery_level(uint8_t percent);
void ble_deinit();

} // namespace minbot::network
