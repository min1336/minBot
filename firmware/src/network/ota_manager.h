#pragma once
#include <functional>

namespace minbot::network {

using OtaProgressCallback = std::function<void(int percent)>;

int ota_check_update(const char* url);
int ota_start_update(const char* url, OtaProgressCallback on_progress = nullptr);
void ota_rollback();

} // namespace minbot::network
