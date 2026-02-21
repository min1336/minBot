#include "animator.h"
#include "sprites.h"
#include "../config.h"

#include <lvgl.h>
#include <esp_log.h>
#include <cstring>
#include <algorithm>

static const char* TAG = "animator";

namespace minbot::display {

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static const Animation* s_current  = nullptr;
static const Animation* s_from     = nullptr;
static const Animation* s_to       = nullptr;
static float            s_blend    = 0.0f;    // 0 = from, 1 = to
static bool             s_blending = false;

static uint8_t   s_frame_idx  = 0;
static uint32_t  s_frame_acc  = 0;   // accumulated time in current frame (ms)

// ---------------------------------------------------------------------------
// Internal: advance frame index
// ---------------------------------------------------------------------------
static void advance_frame(const Animation* anim) {
    if (!anim || anim->frame_count == 0) return;
    s_frame_acc = 0;
    if (anim->loop) {
        s_frame_idx = (s_frame_idx + 1) % anim->frame_count;
    } else {
        if (s_frame_idx + 1 < anim->frame_count) {
            s_frame_idx++;
        }
    }
}

// ---------------------------------------------------------------------------
// Internal: blit a Keyframe onto an LVGL canvas
// Handles transparency key and 4x4 pixel scaling
// ---------------------------------------------------------------------------
static void blit_keyframe(lv_obj_t* canvas, const Keyframe& kf, uint8_t global_alpha) {
    if (!kf.sprite_data || kf.w == 0 || kf.h == 0) return;

    const uint16_t key = sprites::COL_TRANS;
    const int scale = 4;  // logical px -> hw px

    for (uint16_t sy = 0; sy < kf.h; sy++) {
        for (uint16_t sx = 0; sx < kf.w; sx++) {
            uint16_t color = kf.sprite_data[sy * kf.w + sx];
            if (color == key) continue;  // transparent

            // Blend alpha into colour (simple multiply)
            if (global_alpha < 255) {
                uint8_t r = ((color >> 11) & 0x1F);
                uint8_t g = ((color >>  5) & 0x3F);
                uint8_t b = ( color        & 0x1F);
                r = (uint8_t)((r * global_alpha) / 255);
                g = (uint8_t)((g * global_alpha) / 255);
                b = (uint8_t)((b * global_alpha) / 255);
                color = (uint16_t)((r << 11) | (g << 5) | b);
            }

            lv_color_t lv_col = lv_color_make(
                ((color >> 11) & 0x1F) << 3,
                ((color >>  5) & 0x3F) << 2,
                ( color        & 0x1F) << 3
            );

            // Draw 4x4 block
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    int px = kf.x * scale + sx * scale + dx;
                    int py = kf.y * scale + sy * scale + dy;
                    if (px < DISP_WIDTH && py < DISP_HEIGHT) {
                        lv_canvas_set_px(canvas, px, py, lv_col, LV_OPA_COVER);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void animator_init() {
    s_current  = nullptr;
    s_from     = nullptr;
    s_to       = nullptr;
    s_blend    = 0.0f;
    s_blending = false;
    s_frame_idx = 0;
    s_frame_acc = 0;
    ESP_LOGI(TAG, "Animator initialized");
}

void animator_play(const Animation* anim) {
    if (!anim) return;
    s_current   = anim;
    s_blending  = false;
    s_from      = nullptr;
    s_to        = nullptr;
    s_frame_idx = 0;
    s_frame_acc = 0;
}

void animator_blend(const Animation* from, const Animation* to, float progress) {
    if (!from || !to) return;
    s_from     = from;
    s_to       = to;
    s_blend    = std::max(0.0f, std::min(1.0f, progress));
    s_blending = true;

    // When blend completes, switch to target
    if (s_blend >= 1.0f) {
        s_current  = to;
        s_blending = false;
        s_frame_idx = 0;
        s_frame_acc = 0;
    }
}

void animator_update(uint32_t dt_ms) {
    const Animation* active = s_blending ? s_to : s_current;
    if (!active || active->frame_count == 0) return;

    s_frame_acc += dt_ms;
    if (s_frame_acc >= active->frame_duration_ms) {
        advance_frame(active);
    }

    // Auto-advance blend progress (2 frames = 66ms crossfade)
    if (s_blending) {
        s_blend += (float)dt_ms / 66.0f;
        if (s_blend >= 1.0f) {
            s_current  = s_to;
            s_blending = false;
            s_frame_idx = 0;
            s_frame_acc = 0;
        }
    }
}

void animator_render(void* canvas_ptr) {
    if (!canvas_ptr) return;
    lv_obj_t* canvas = (lv_obj_t*)canvas_ptr;

    if (s_blending && s_from && s_to) {
        // Render "from" frame at (1-blend) alpha, "to" at blend alpha
        uint8_t alpha_to   = (uint8_t)(s_blend * 255.0f);
        uint8_t alpha_from = 255 - alpha_to;

        uint8_t fi_from = s_frame_idx % s_from->frame_count;
        uint8_t fi_to   = s_frame_idx % s_to->frame_count;

        const Keyframe& kf_from = s_from->frames[fi_from];
        const Keyframe& kf_to   = s_to->frames[fi_to];

        // Blend effective alpha with per-keyframe alpha
        uint8_t eff_from = (uint8_t)((kf_from.alpha * alpha_from) / 255);
        uint8_t eff_to   = (uint8_t)((kf_to.alpha   * alpha_to)   / 255);

        blit_keyframe(canvas, kf_from, eff_from);
        blit_keyframe(canvas, kf_to,   eff_to);

    } else if (s_current && s_current->frame_count > 0) {
        uint8_t fi = s_frame_idx % s_current->frame_count;
        const Keyframe& kf = s_current->frames[fi];
        blit_keyframe(canvas, kf, kf.alpha);
    }
}

} // namespace minbot::display
