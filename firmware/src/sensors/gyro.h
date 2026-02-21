#pragma once
#include <cstdint>
#include <functional>

namespace minbot::sensors {

struct GyroData {
    float accel_x, accel_y, accel_z;  // g
    float gyro_x, gyro_y, gyro_z;    // deg/s
    float tilt_angle;                  // degrees from vertical
    float temperature;                 // Celsius
};

using TiltCallback = std::function<void(float angle)>;

int gyro_init();
GyroData gyro_read();
float gyro_get_tilt();
void gyro_on_tilt(TiltCallback cb, float threshold_degrees = 30.0f);
void gyro_deinit();

} // namespace minbot::sensors
