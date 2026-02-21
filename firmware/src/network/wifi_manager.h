#pragma once
#include <functional>

namespace minbot::network {

using WifiCallback = std::function<void(bool connected)>;

int wifi_init();
int wifi_connect(const char* ssid, const char* password);
bool wifi_is_connected();
int wifi_get_rssi();
void wifi_on_state_change(WifiCallback cb);
void wifi_deinit();

} // namespace minbot::network
