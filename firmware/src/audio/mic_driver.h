#pragma once
#include <cstdint>
#include <cstddef>

namespace minbot::audio {

/// Initialize INMP441 I2S microphone
/// Returns ESP_OK on success
int mic_init();

/// Read PCM samples from microphone
/// @param buffer Output buffer for 16-bit PCM samples
/// @param max_bytes Buffer size in bytes
/// @param bytes_read Actual bytes read (output)
/// @param timeout_ms Max wait time
/// @return ESP_OK on success
int mic_read(int16_t* buffer, size_t max_bytes, size_t* bytes_read, uint32_t timeout_ms = 100);

/// Deinitialize microphone
void mic_deinit();

} // namespace minbot::audio
