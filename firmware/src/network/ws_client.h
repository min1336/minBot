#pragma once
#include <cstdint>
#include <functional>

namespace minbot::network {

using AudioCallback = std::function<void(const uint8_t* data, size_t len)>;
using JsonCallback = std::function<void(const char* json, size_t len)>;

int ws_init(const char* uri);
int ws_connect();
bool ws_is_connected();
int ws_send_binary(const uint8_t* data, size_t len);
int ws_send_text(const char* text);
void ws_on_binary(AudioCallback cb);
void ws_on_text(JsonCallback cb);
void ws_disconnect();

} // namespace minbot::network
