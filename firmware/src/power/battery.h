#pragma once
#include <cstdint>

namespace minbot::power {

int battery_init();
int battery_get_percent();
int battery_get_voltage_mv();
bool battery_is_charging();
void battery_enter_light_sleep();
void battery_enter_deep_sleep();

} // namespace minbot::power
