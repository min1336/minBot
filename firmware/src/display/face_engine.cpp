#include "face_engine.h"
#include "emotions.h"
#include "animator.h"
#include "../config.h"

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lvgl.h>

static const char* TAG = "face_engine";

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr uint32_t FRAME_PERIOD_MS  = 33;   // ~30 fps
static constexpr uint32_t SPI_CLOCK_HZ     = 40000000;
static constexpr uint32_t LVGL_BUF_LINES   = 40;   // lines per partial-flush buf

// LEDC channel for backlight PWM
static constexpr ledc_channel_t BL_CHANNEL  = LEDC_CHANNEL_0;
static constexpr ledc_timer_t   BL_TIMER    = LEDC_TIMER_0;
static constexpr uint32_t       BL_FREQ_HZ  = 5000;
static constexpr ledc_timer_bit_t BL_RES    = LEDC_TIMER_8_BIT;

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static spi_device_handle_t s_spi    = nullptr;
static lv_display_t*       s_disp   = nullptr;
static uint16_t*           s_buf1   = nullptr;
static uint16_t*           s_buf2   = nullptr;
static bool                s_ready  = false;

// ---------------------------------------------------------------------------
// GC9A01 command helpers
// ---------------------------------------------------------------------------
static void gc9a01_cmd(uint8_t cmd) {
    gpio_set_level((gpio_num_t)DISP_PIN_DC, 0);
    spi_transaction_t t = {};
    t.length    = 8;
    t.tx_data[0] = cmd;
    t.flags     = SPI_TRANS_USE_TXDATA;
    spi_device_polling_transmit(s_spi, &t);
}

static void gc9a01_data(const uint8_t* data, size_t len) {
    gpio_set_level((gpio_num_t)DISP_PIN_DC, 1);
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = data;
    spi_device_polling_transmit(s_spi, &t);
}

static void gc9a01_data1(uint8_t d) {
    gc9a01_data(&d, 1);
}

// ---------------------------------------------------------------------------
// GC9A01 initialisation sequence
// ---------------------------------------------------------------------------
static void gc9a01_init_sequence() {
    // Hardware reset
    gpio_set_level((gpio_num_t)DISP_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)DISP_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    gc9a01_cmd(0xEF);
    gc9a01_cmd(0xEB); gc9a01_data1(0x14);

    gc9a01_cmd(0xFE);
    gc9a01_cmd(0xEF);

    gc9a01_cmd(0xEB); gc9a01_data1(0x14);
    gc9a01_cmd(0x84); gc9a01_data1(0x40);
    gc9a01_cmd(0x85); gc9a01_data1(0xFF);
    gc9a01_cmd(0x86); gc9a01_data1(0xFF);
    gc9a01_cmd(0x87); gc9a01_data1(0xFF);
    gc9a01_cmd(0x88); gc9a01_data1(0x0A);
    gc9a01_cmd(0x89); gc9a01_data1(0x21);
    gc9a01_cmd(0x8A); gc9a01_data1(0x00);
    gc9a01_cmd(0x8B); gc9a01_data1(0x80);
    gc9a01_cmd(0x8C); gc9a01_data1(0x01);
    gc9a01_cmd(0x8D); gc9a01_data1(0x01);
    gc9a01_cmd(0x8E); gc9a01_data1(0xFF);
    gc9a01_cmd(0x8F); gc9a01_data1(0xFF);

    gc9a01_cmd(0xB6);
    gc9a01_data1(0x00);
    gc9a01_data1(0x20);

    gc9a01_cmd(0x36); gc9a01_data1(0x48); // MADCTL: RGB, row/col order
    gc9a01_cmd(0x3A); gc9a01_data1(0x05); // Pixel format: 16-bit RGB565

    gc9a01_cmd(0x90);
    gc9a01_data1(0x08); gc9a01_data1(0x08);
    gc9a01_data1(0x08); gc9a01_data1(0x08);

    gc9a01_cmd(0xBD); gc9a01_data1(0x06);
    gc9a01_cmd(0xBC); gc9a01_data1(0x00);

    gc9a01_cmd(0xFF);
    gc9a01_data1(0x60); gc9a01_data1(0x01); gc9a01_data1(0x04);

    gc9a01_cmd(0xC3); gc9a01_data1(0x13); // VREG1A
    gc9a01_cmd(0xC4); gc9a01_data1(0x13); // VREG1B
    gc9a01_cmd(0xC9); gc9a01_data1(0x22); // VREG2A

    gc9a01_cmd(0xBE); gc9a01_data1(0x11);
    gc9a01_cmd(0xE1);
    gc9a01_data1(0x10); gc9a01_data1(0x0E);

    gc9a01_cmd(0xDF);
    gc9a01_data1(0x21); gc9a01_data1(0x0C); gc9a01_data1(0x02);

    // Gamma setting
    gc9a01_cmd(0xF0);
    gc9a01_data1(0x45); gc9a01_data1(0x09); gc9a01_data1(0x08);
    gc9a01_data1(0x08); gc9a01_data1(0x26); gc9a01_data1(0x2A);

    gc9a01_cmd(0xF1);
    gc9a01_data1(0x43); gc9a01_data1(0x70); gc9a01_data1(0x72);
    gc9a01_data1(0x36); gc9a01_data1(0x37); gc9a01_data1(0x6F);

    gc9a01_cmd(0xF2);
    gc9a01_data1(0x45); gc9a01_data1(0x09); gc9a01_data1(0x08);
    gc9a01_data1(0x08); gc9a01_data1(0x26); gc9a01_data1(0x2A);

    gc9a01_cmd(0xF3);
    gc9a01_data1(0x43); gc9a01_data1(0x70); gc9a01_data1(0x72);
    gc9a01_data1(0x36); gc9a01_data1(0x37); gc9a01_data1(0x6F);

    gc9a01_cmd(0xED);
    gc9a01_data1(0x1B); gc9a01_data1(0x0B);

    gc9a01_cmd(0xAE); gc9a01_data1(0x77);
    gc9a01_cmd(0xCD); gc9a01_data1(0x63);

    gc9a01_cmd(0x70);
    gc9a01_data1(0x07); gc9a01_data1(0x07); gc9a01_data1(0x04);
    gc9a01_data1(0x0E); gc9a01_data1(0x0F); gc9a01_data1(0x09);
    gc9a01_data1(0x07); gc9a01_data1(0x08); gc9a01_data1(0x03);

    gc9a01_cmd(0xE8); gc9a01_data1(0x34);

    gc9a01_cmd(0x62);
    gc9a01_data1(0x18); gc9a01_data1(0x0D); gc9a01_data1(0x71);
    gc9a01_data1(0xED); gc9a01_data1(0x70); gc9a01_data1(0x70);
    gc9a01_data1(0x18); gc9a01_data1(0x0F); gc9a01_data1(0x71);
    gc9a01_data1(0xEF); gc9a01_data1(0x70); gc9a01_data1(0x70);

    gc9a01_cmd(0x63);
    gc9a01_data1(0x18); gc9a01_data1(0x11); gc9a01_data1(0x71);
    gc9a01_data1(0xF1); gc9a01_data1(0x70); gc9a01_data1(0x70);
    gc9a01_data1(0x18); gc9a01_data1(0x13); gc9a01_data1(0x71);
    gc9a01_data1(0xF3); gc9a01_data1(0x70); gc9a01_data1(0x70);

    gc9a01_cmd(0x64);
    gc9a01_data1(0x28); gc9a01_data1(0x29); gc9a01_data1(0xF1);
    gc9a01_data1(0x01); gc9a01_data1(0xF1); gc9a01_data1(0x00);
    gc9a01_data1(0x07);

    gc9a01_cmd(0x66);
    gc9a01_data1(0x3C); gc9a01_data1(0x00); gc9a01_data1(0xCD);
    gc9a01_data1(0x67); gc9a01_data1(0x45); gc9a01_data1(0x45);
    gc9a01_data1(0x10); gc9a01_data1(0x00); gc9a01_data1(0x00);
    gc9a01_data1(0x00);

    gc9a01_cmd(0x67);
    gc9a01_data1(0x00); gc9a01_data1(0x3C); gc9a01_data1(0x00);
    gc9a01_data1(0x00); gc9a01_data1(0x00); gc9a01_data1(0x01);
    gc9a01_data1(0x54); gc9a01_data1(0x10); gc9a01_data1(0x32);
    gc9a01_data1(0x98);

    gc9a01_cmd(0x74);
    gc9a01_data1(0x10); gc9a01_data1(0x85); gc9a01_data1(0x80);
    gc9a01_data1(0x00); gc9a01_data1(0x00); gc9a01_data1(0x4E);
    gc9a01_data1(0x00);

    gc9a01_cmd(0x98);
    gc9a01_data1(0x3E); gc9a01_data1(0x07);

    gc9a01_cmd(0x35); // Tearing effect on
    gc9a01_cmd(0x21); // Display inversion on (required for GC9A01 colour accuracy)

    gc9a01_cmd(0x11); // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    gc9a01_cmd(0x29); // Display on
    vTaskDelay(pdMS_TO_TICKS(20));
}

// ---------------------------------------------------------------------------
// LVGL flush callback — DMA transfer to GC9A01
// ---------------------------------------------------------------------------
static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_map) {
    // Set column address
    gc9a01_cmd(0x2A);
    uint8_t col[4] = {
        (uint8_t)(area->x1 >> 8), (uint8_t)(area->x1 & 0xFF),
        (uint8_t)(area->x2 >> 8), (uint8_t)(area->x2 & 0xFF)
    };
    gc9a01_data(col, 4);

    // Set row address
    gc9a01_cmd(0x2B);
    uint8_t row[4] = {
        (uint8_t)(area->y1 >> 8), (uint8_t)(area->y1 & 0xFF),
        (uint8_t)(area->y2 >> 8), (uint8_t)(area->y2 & 0xFF)
    };
    gc9a01_data(row, 4);

    // Write pixels
    gc9a01_cmd(0x2C);
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    gc9a01_data(color_map, pixel_count * 2);  // 2 bytes per RGB565 pixel

    lv_display_flush_ready(disp);
}

// ---------------------------------------------------------------------------
// LVGL tick callback (returns ms since boot for LVGL internal timers)
// ---------------------------------------------------------------------------
static uint32_t lvgl_tick_cb() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
namespace minbot::display {

int face_init() {
    ESP_LOGI(TAG, "Initialising GC9A01 display");

    // --- GPIO config ---
    gpio_config_t io = {};
    io.mode         = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << DISP_PIN_DC)  |
                      (1ULL << DISP_PIN_RST) |
                      (1ULL << DISP_PIN_BL);
    gpio_config(&io);

    gpio_set_level((gpio_num_t)DISP_PIN_BL, 0); // backlight off until ready

    // --- SPI bus ---
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num   = DISP_PIN_MOSI;
    buscfg.miso_io_num   = -1;
    buscfg.sclk_io_num   = DISP_PIN_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = DISP_WIDTH * LVGL_BUF_LINES * 2 + 8;

    esp_err_t ret = spi_bus_initialize((spi_host_device_t)DISP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // --- SPI device ---
    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = SPI_CLOCK_HZ;
    devcfg.mode           = 0;
    devcfg.spics_io_num   = DISP_PIN_CS;
    devcfg.queue_size     = 7;

    ret = spi_bus_add_device((spi_host_device_t)DISP_SPI_HOST, &devcfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // --- Panel init sequence ---
    gc9a01_init_sequence();

    // --- LVGL init ---
    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    // Allocate double-buffer in PSRAM (LVGL_BUF_LINES scanlines each)
    size_t buf_bytes = (size_t)DISP_WIDTH * LVGL_BUF_LINES * sizeof(uint16_t);
    s_buf1 = (uint16_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = (uint16_t*)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "PSRAM buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    s_disp = lv_display_create(DISP_WIDTH, DISP_HEIGHT);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2,
                           buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // --- Emotion + animator subsystems ---
    animator_init();
    emotion_init();

    // --- Backlight on at full ---
    face_set_brightness(255);

    s_ready = true;
    ESP_LOGI(TAG, "Display ready");
    return ESP_OK;
}

void face_update() {
    if (!s_ready) return;

    static uint32_t last_ms = 0;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t dt = now_ms - last_ms;
    if (dt < FRAME_PERIOD_MS) return;
    last_ms = now_ms;

    emotion_update(dt);
    animator_update(dt);

    // Drive LVGL: it calls flush_cb which pushes pixels to GC9A01
    lv_task_handler();
}

void face_set_brightness(uint8_t brightness) {
    // First call configures LEDC; subsequent calls just update duty
    static bool ledc_ready = false;
    if (!ledc_ready) {
        ledc_timer_config_t timer = {};
        timer.speed_mode      = LEDC_LOW_SPEED_MODE;
        timer.timer_num       = BL_TIMER;
        timer.duty_resolution = BL_RES;
        timer.freq_hz         = BL_FREQ_HZ;
        timer.clk_cfg         = LEDC_AUTO_CLK;
        ledc_timer_config(&timer);

        ledc_channel_config_t ch = {};
        ch.speed_mode     = LEDC_LOW_SPEED_MODE;
        ch.channel        = BL_CHANNEL;
        ch.timer_sel      = BL_TIMER;
        ch.intr_type      = LEDC_INTR_DISABLE;
        ch.gpio_num       = DISP_PIN_BL;
        ch.duty           = brightness;
        ch.hpoint         = 0;
        ledc_channel_config(&ch);

        ledc_ready = true;
    } else {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, brightness);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
    }
}

void face_deinit() {
    if (!s_ready) return;
    s_ready = false;

    face_set_brightness(0);

    lv_display_delete(s_disp);
    s_disp = nullptr;

    heap_caps_free(s_buf1); s_buf1 = nullptr;
    heap_caps_free(s_buf2); s_buf2 = nullptr;

    spi_bus_remove_device(s_spi);
    spi_bus_free((spi_host_device_t)DISP_SPI_HOST);
    s_spi = nullptr;

    ESP_LOGI(TAG, "Display deinitialized");
}

} // namespace minbot::display
