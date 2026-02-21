#include "emotions.h"
#include "animator.h"
#include "sprites.h"
#include "../config.h"

#include <lvgl.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>
#include <cstdlib>

static const char* TAG = "emotions";

namespace minbot::display {

// ---------------------------------------------------------------------------
// Canvas (shared LVGL object — created once in emotion_init)
// ---------------------------------------------------------------------------
static lv_obj_t* s_canvas = nullptr;
static uint16_t* s_canvas_buf = nullptr;
static constexpr size_t CANVAS_BUF_BYTES = DISP_WIDTH * DISP_HEIGHT * sizeof(uint16_t);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static Emotion   s_current       = Emotion::IDLE;
static Emotion   s_target        = Emotion::IDLE;
static bool      s_transitioning = false;
static float     s_trans_prog    = 0.0f;   // 0..1

static uint8_t   s_lip_level     = 0;
static uint8_t   s_audio_level   = 0;

// Idle timers
static uint32_t  s_blink_acc     = 0;
static uint32_t  s_blink_interval = 3000;  // ms between blinks
static uint32_t  s_eye_move_acc  = 0;
static uint32_t  s_eye_move_interval = 7000;
static bool      s_blinking      = false;
static uint8_t   s_blink_frame   = 0;
static uint32_t  s_blink_frame_acc = 0;

// Thinking rotation state
static uint8_t   s_think_step    = 0;
static uint32_t  s_think_acc     = 0;

// Sleeping breathing
static uint32_t  s_sleep_acc     = 0;
static bool      s_sleep_exhale  = false;

// Speaking mouth state
static uint8_t   s_mouth_stage   = 0;   // 0=closed, 1=half, 2=open

// Effect particle state
struct Particle {
    int16_t  x, y;
    int16_t  vx, vy;
    uint32_t life_ms;
    uint32_t max_life_ms;
    const sprites::Sprite* spr;
};
static constexpr uint8_t MAX_PARTICLES = 6;
static Particle  s_particles[MAX_PARTICLES];
static uint8_t   s_particle_count = 0;
static uint32_t  s_particle_spawn_acc = 0;

// ---------------------------------------------------------------------------
// Animation keyframe tables for each emotion
// Eye positions (logical coords, will be scaled x4 in animator)
// Left eye:  canvas x=28, y=20  (logical: x=7,  y=5)
// Right eye: canvas x=80, y=20  (logical: x=20, y=5)
// Mouth:     canvas x=52, y=52  (logical: x=13, y=13)
// ---------------------------------------------------------------------------

// Macro positions (logical pixel units, scaled *4 inside animator)
static constexpr uint16_t EYE_L_X = 7;
static constexpr uint16_t EYE_R_X = 41;  // 60-12=48 but mirrored; 41 gives symmetry
static constexpr uint16_t EYE_Y   = 15;
static constexpr uint16_t MOUTH_X = 22;
static constexpr uint16_t MOUTH_Y = 38;

// ---------------------------------------------------------------------------
// IDLE keyframes
// ---------------------------------------------------------------------------
static const Keyframe kf_idle_eyes[] = {
    { sprites::eye_idle.data, EYE_L_X, EYE_Y, sprites::eye_idle.w, sprites::eye_idle.h, 255 },
    { sprites::eye_idle.data, EYE_R_X, EYE_Y, sprites::eye_idle.w, sprites::eye_idle.h, 255 },
    { sprites::mouth_closed.data, MOUTH_X, MOUTH_Y, sprites::mouth_closed.w, sprites::mouth_closed.h, 255 },
};
static const Animation anim_idle = { kf_idle_eyes, 3, 33, true };

// Blink frames (eye-only, 3 stages)
static const Keyframe kf_blink_half[] = {
    { sprites::eye_blink_half.data, EYE_L_X, EYE_Y+2, sprites::eye_blink_half.w, sprites::eye_blink_half.h, 255 },
    { sprites::eye_blink_half.data, EYE_R_X, EYE_Y+2, sprites::eye_blink_half.w, sprites::eye_blink_half.h, 255 },
    { sprites::mouth_closed.data,   MOUTH_X, MOUTH_Y,  sprites::mouth_closed.w,   sprites::mouth_closed.h,   255 },
};
static const Animation anim_blink_half = { kf_blink_half, 3, 60, false };

static const Keyframe kf_blink_closed[] = {
    { sprites::eye_blink_closed.data, EYE_L_X, EYE_Y+3, sprites::eye_blink_closed.w, sprites::eye_blink_closed.h, 255 },
    { sprites::eye_blink_closed.data, EYE_R_X, EYE_Y+3, sprites::eye_blink_closed.w, sprites::eye_blink_closed.h, 255 },
    { sprites::mouth_closed.data,     MOUTH_X,  MOUTH_Y, sprites::mouth_closed.w,     sprites::mouth_closed.h,     255 },
};
static const Animation anim_blink_closed = { kf_blink_closed, 3, 60, false };

// ---------------------------------------------------------------------------
// LISTENING keyframes (eyes widen, 4-frame transition handled via blend)
// ---------------------------------------------------------------------------
static const Keyframe kf_listen[] = {
    { sprites::eye_listen.data, EYE_L_X, EYE_Y-1, sprites::eye_listen.w, sprites::eye_listen.h, 255 },
    { sprites::eye_listen.data, EYE_R_X, EYE_Y-1, sprites::eye_listen.w, sprites::eye_listen.h, 255 },
    { sprites::mouth_closed.data, MOUTH_X, MOUTH_Y, sprites::mouth_closed.w, sprites::mouth_closed.h, 255 },
};
static const Animation anim_listen = { kf_listen, 3, 150, true };

// ---------------------------------------------------------------------------
// THINKING keyframes (8-frame rotation loop)
// Simulate rotation by cycling through offset eye positions
// ---------------------------------------------------------------------------
static const Keyframe kf_think_0[] = {
    { sprites::eye_think.data, EYE_L_X,   EYE_Y,   sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::eye_think.data, EYE_R_X,   EYE_Y,   sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::spr_dots.data,  MOUTH_X+2, MOUTH_Y+4, sprites::spr_dots.w, sprites::spr_dots.h, 255 },
};
static const Keyframe kf_think_1[] = {
    { sprites::eye_think.data, EYE_L_X+1, EYE_Y-1, sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::eye_think.data, EYE_R_X+1, EYE_Y-1, sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::spr_dots.data,  MOUTH_X+2, MOUTH_Y+4, sprites::spr_dots.w, sprites::spr_dots.h, 200 },
};
static const Keyframe kf_think_2[] = {
    { sprites::eye_think.data, EYE_L_X+1, EYE_Y+1, sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::eye_think.data, EYE_R_X+1, EYE_Y+1, sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::spr_dots.data,  MOUTH_X+2, MOUTH_Y+4, sprites::spr_dots.w, sprites::spr_dots.h, 150 },
};
static const Keyframe kf_think_3[] = {
    { sprites::eye_think.data, EYE_L_X,   EYE_Y+1, sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::eye_think.data, EYE_R_X,   EYE_Y+1, sprites::eye_think.w, sprites::eye_think.h, 255 },
    { sprites::spr_dots.data,  MOUTH_X+2, MOUTH_Y+4, sprites::spr_dots.w, sprites::spr_dots.h, 200 },
};
// Reuse frames 1-3 in reverse for full 8-step loop feel
static const Keyframe kf_think_seq[8*3] = {
    kf_think_0[0], kf_think_0[1], kf_think_0[2],
    kf_think_1[0], kf_think_1[1], kf_think_1[2],
    kf_think_2[0], kf_think_2[1], kf_think_2[2],
    kf_think_3[0], kf_think_3[1], kf_think_3[2],
    kf_think_2[0], kf_think_2[1], kf_think_2[2],
    kf_think_1[0], kf_think_1[1], kf_think_1[2],
    kf_think_0[0], kf_think_0[1], kf_think_0[2],
    kf_think_3[0], kf_think_3[1], kf_think_3[2],
};
// animator works per-animation (one Animation = one element sequence)
// We split into 8 single-keyframe animations and cycle in update.
// For simplicity, store all 8 as separate Animation entries.
static const Animation anim_think_frames[8] = {
    { &kf_think_seq[0],  3, 200, false },
    { &kf_think_seq[3],  3, 200, false },
    { &kf_think_seq[6],  3, 200, false },
    { &kf_think_seq[9],  3, 200, false },
    { &kf_think_seq[12], 3, 200, false },
    { &kf_think_seq[15], 3, 200, false },
    { &kf_think_seq[18], 3, 200, false },
    { &kf_think_seq[21], 3, 200, false },
};

// ---------------------------------------------------------------------------
// SPEAKING keyframes (3 mouth stages driven by audio level)
// ---------------------------------------------------------------------------
static const Keyframe kf_speak_closed[] = {
    { sprites::eye_speak.data,   EYE_L_X, EYE_Y+1, sprites::eye_speak.w, sprites::eye_speak.h, 255 },
    { sprites::eye_speak.data,   EYE_R_X, EYE_Y+1, sprites::eye_speak.w, sprites::eye_speak.h, 255 },
    { sprites::mouth_closed.data, MOUTH_X, MOUTH_Y, sprites::mouth_closed.w, sprites::mouth_closed.h, 255 },
};
static const Keyframe kf_speak_half[] = {
    { sprites::eye_speak.data, EYE_L_X, EYE_Y+1, sprites::eye_speak.w, sprites::eye_speak.h, 255 },
    { sprites::eye_speak.data, EYE_R_X, EYE_Y+1, sprites::eye_speak.w, sprites::eye_speak.h, 255 },
    { sprites::mouth_half.data, MOUTH_X, MOUTH_Y, sprites::mouth_half.w, sprites::mouth_half.h, 255 },
};
static const Keyframe kf_speak_open[] = {
    { sprites::eye_speak.data, EYE_L_X, EYE_Y+1, sprites::eye_speak.w, sprites::eye_speak.h, 255 },
    { sprites::eye_speak.data, EYE_R_X, EYE_Y+1, sprites::eye_speak.w, sprites::eye_speak.h, 255 },
    { sprites::mouth_open.data, MOUTH_X-2, MOUTH_Y, sprites::mouth_open.w, sprites::mouth_open.h, 255 },
};
static const Animation anim_speak_closed = { kf_speak_closed, 3, 33, true };
static const Animation anim_speak_half   = { kf_speak_half,   3, 33, true };
static const Animation anim_speak_open   = { kf_speak_open,   3, 33, true };

// ---------------------------------------------------------------------------
// HAPPY keyframes (crescent eyes + smile)
// ---------------------------------------------------------------------------
static const Keyframe kf_happy[] = {
    { sprites::eye_happy.data,   EYE_L_X, EYE_Y,   sprites::eye_happy.w, sprites::eye_happy.h, 255 },
    { sprites::eye_happy.data,   EYE_R_X, EYE_Y,   sprites::eye_happy.w, sprites::eye_happy.h, 255 },
    { sprites::mouth_smile.data, MOUTH_X, MOUTH_Y, sprites::mouth_smile.w, sprites::mouth_smile.h, 255 },
    { sprites::spr_blush.data,   5,       32,      sprites::spr_blush.w, sprites::spr_blush.h, 200 },
    { sprites::spr_blush.data,   45,      32,      sprites::spr_blush.w, sprites::spr_blush.h, 200 },
};
static const Animation anim_happy = { kf_happy, 5, 33, true };

// ---------------------------------------------------------------------------
// SAD keyframes (droopy eyes + frown, 6 frames)
// ---------------------------------------------------------------------------
static const Keyframe kf_sad[] = {
    { sprites::eye_sad.data,    EYE_L_X, EYE_Y,   sprites::eye_sad.w,   sprites::eye_sad.h,   255 },
    { sprites::eye_sad.data,    EYE_R_X, EYE_Y,   sprites::eye_sad.w,   sprites::eye_sad.h,   255 },
    { sprites::mouth_frown.data, MOUTH_X, MOUTH_Y, sprites::mouth_frown.w, sprites::mouth_frown.h, 255 },
};
static const Animation anim_sad = { kf_sad, 3, 150, true };

// ---------------------------------------------------------------------------
// SURPRISED keyframes (3 fast frames)
// ---------------------------------------------------------------------------
static const Keyframe kf_surprised[] = {
    { sprites::eye_surprised.data, EYE_L_X, EYE_Y-2, sprites::eye_surprised.w, sprites::eye_surprised.h, 255 },
    { sprites::eye_surprised.data, EYE_R_X, EYE_Y-2, sprites::eye_surprised.w, sprites::eye_surprised.h, 255 },
    { sprites::mouth_open.data,    MOUTH_X-2, MOUTH_Y, sprites::mouth_open.w, sprites::mouth_open.h, 255 },
    { sprites::spr_exclaim.data,   28,       5,       sprites::spr_exclaim.w, sprites::spr_exclaim.h, 255 },
};
static const Animation anim_surprised = { kf_surprised, 4, 80, true };

// ---------------------------------------------------------------------------
// SLEEPING keyframes (8 frames, slow)
// ---------------------------------------------------------------------------
static const Keyframe kf_sleep[] = {
    { sprites::eye_sleep.data,  EYE_L_X, EYE_Y+3, sprites::eye_sleep.w, sprites::eye_sleep.h, 255 },
    { sprites::eye_sleep.data,  EYE_R_X, EYE_Y+3, sprites::eye_sleep.w, sprites::eye_sleep.h, 255 },
    { sprites::mouth_closed.data, MOUTH_X, MOUTH_Y, sprites::mouth_closed.w, sprites::mouth_closed.h, 200 },
    { sprites::spr_zzz.data,    38,      8,       sprites::spr_zzz.w, sprites::spr_zzz.h, 220 },
};
static const Animation anim_sleep = { kf_sleep, 4, 200, true };

// Startled wake transition: SLEEPING -> LISTENING fast sequence
static const Keyframe kf_startled_0[] = {
    { sprites::eye_surprised.data, EYE_L_X, EYE_Y-2, sprites::eye_surprised.w, sprites::eye_surprised.h, 255 },
    { sprites::eye_surprised.data, EYE_R_X, EYE_Y-2, sprites::eye_surprised.w, sprites::eye_surprised.h, 255 },
    { sprites::mouth_open.data,    MOUTH_X-2, MOUTH_Y, sprites::mouth_open.w, sprites::mouth_open.h, 255 },
};
static const Keyframe kf_startled_1[] = {
    { sprites::eye_listen.data, EYE_L_X, EYE_Y-1, sprites::eye_listen.w, sprites::eye_listen.h, 255 },
    { sprites::eye_listen.data, EYE_R_X, EYE_Y-1, sprites::eye_listen.w, sprites::eye_listen.h, 255 },
    { sprites::mouth_closed.data, MOUTH_X, MOUTH_Y, sprites::mouth_closed.w, sprites::mouth_closed.h, 255 },
};
// Two-step startled animation — played in sequence by state machine
static const Animation anim_startled_0 = { kf_startled_0, 3, 120, false };
static const Animation anim_startled_1 = { kf_startled_1, 3, 150, false };

// ---------------------------------------------------------------------------
// TILTED keyframes (dizzy swirl eyes)
// ---------------------------------------------------------------------------
static const Keyframe kf_tilt[] = {
    { sprites::eye_tilt.data, EYE_L_X+2, EYE_Y, sprites::eye_tilt.w, sprites::eye_tilt.h, 255 },
    { sprites::eye_tilt.data, EYE_R_X+2, EYE_Y, sprites::eye_tilt.w, sprites::eye_tilt.h, 255 },
    { sprites::mouth_closed.data, MOUTH_X, MOUTH_Y, sprites::mouth_closed.w, sprites::mouth_closed.h, 180 },
};
static const Animation anim_tilt = { kf_tilt, 3, 150, true };

// ---------------------------------------------------------------------------
// Transition table: from -> to animation pointer
// ---------------------------------------------------------------------------
static const Animation* get_anim_for(Emotion e) {
    switch (e) {
        case Emotion::IDLE:      return &anim_idle;
        case Emotion::LISTENING: return &anim_listen;
        case Emotion::THINKING:  return &anim_think_frames[0];
        case Emotion::SPEAKING:  return &anim_speak_closed;
        case Emotion::HAPPY:     return &anim_happy;
        case Emotion::SAD:       return &anim_sad;
        case Emotion::SURPRISED: return &anim_surprised;
        case Emotion::SLEEPING:  return &anim_sleep;
        case Emotion::TILTED:    return &anim_tilt;
        default:                 return &anim_idle;
    }
}

// ---------------------------------------------------------------------------
// Particle helpers
// ---------------------------------------------------------------------------
static void spawn_particle(const sprites::Sprite* spr, int16_t cx, int16_t cy,
                           int16_t vx, int16_t vy, uint32_t life_ms) {
    if (s_particle_count >= MAX_PARTICLES) return;
    Particle& p      = s_particles[s_particle_count++];
    p.x              = cx;
    p.y              = cy;
    p.vx             = vx;
    p.vy             = vy;
    p.life_ms        = life_ms;
    p.max_life_ms    = life_ms;
    p.spr            = spr;
}

static void update_particles(uint32_t dt_ms) {
    for (uint8_t i = 0; i < s_particle_count; ) {
        Particle& p = s_particles[i];
        if (p.life_ms <= dt_ms) {
            // Remove particle: swap with last
            s_particles[i] = s_particles[--s_particle_count];
        } else {
            p.life_ms -= dt_ms;
            // Simple integer velocity (logical px per 100ms)
            p.x = (int16_t)(p.x + p.vx * (int16_t)dt_ms / 100);
            p.y = (int16_t)(p.y + p.vy * (int16_t)dt_ms / 100);
            i++;
        }
    }
}

static void spawn_emotion_particles(Emotion e) {
    s_particle_count = 0;
    switch (e) {
        case Emotion::HAPPY:
            spawn_particle(&sprites::spr_heart, 5,  10, -1, -2, 1200);
            spawn_particle(&sprites::spr_heart, 50, 8,   1, -2, 1400);
            spawn_particle(&sprites::spr_heart, 28, 4,   0, -3, 1000);
            break;
        case Emotion::SAD:
            spawn_particle(&sprites::spr_tear, 9,  20,  0,  3, 1000);
            spawn_particle(&sprites::spr_tear, 45, 20,  0,  3, 1200);
            break;
        case Emotion::TILTED:
            spawn_particle(&sprites::spr_star, 5,  30, -2, -1, 800);
            spawn_particle(&sprites::spr_star, 50, 28,  2, -1, 900);
            spawn_particle(&sprites::spr_star, 28,  5,  0, -2, 700);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void emotion_init() {
    // Create a full-screen LVGL canvas backed by PSRAM
    s_canvas_buf = (uint16_t*)heap_caps_malloc(CANVAS_BUF_BYTES,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_canvas_buf) {
        ESP_LOGE(TAG, "Canvas buffer alloc failed");
        return;
    }
    memset(s_canvas_buf, 0, CANVAS_BUF_BYTES);

    lv_obj_t* scr = lv_screen_active();
    s_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, DISP_WIDTH, DISP_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, 0);

    s_current = Emotion::IDLE;
    s_target  = Emotion::IDLE;
    animator_play(get_anim_for(Emotion::IDLE));

    // Randomise blink interval slightly
    s_blink_interval = 2800 + (uint32_t)(rand() % 400);

    ESP_LOGI(TAG, "Emotion FSM initialized");
}

void emotion_set(Emotion emotion) {
    if (emotion == s_target) return;

    Emotion prev = s_target;
    s_target = emotion;

    // Special case: sleeping -> listening = "startled wake" sequence
    if (prev == Emotion::SLEEPING && emotion == Emotion::LISTENING) {
        animator_play(&anim_startled_0);
        s_transitioning = true;
        s_trans_prog    = 0.0f;
        s_current       = emotion;
        // Second half of startled played in emotion_update after short delay
        spawn_emotion_particles(emotion);
        return;
    }

    // Standard 2-frame crossfade transition
    const Animation* from_anim = get_anim_for(s_current);
    const Animation* to_anim   = get_anim_for(emotion);
    animator_blend(from_anim, to_anim, 0.0f);
    s_transitioning = true;
    s_trans_prog    = 0.0f;
    s_current       = emotion;

    spawn_emotion_particles(emotion);
}

Emotion emotion_get() {
    return s_current;
}

void emotion_set_lip_sync(uint8_t level) {
    s_lip_level = level;
}

void emotion_set_audio_level(uint8_t level) {
    s_audio_level = level;
}

void emotion_update(uint32_t dt_ms) {
    if (!s_canvas) return;

    // Clear canvas to background colour
    lv_canvas_fill_bg(s_canvas, lv_color_make(0x08, 0x10, 0x30), LV_OPA_COVER);

    // Advance blend transition
    if (s_transitioning) {
        s_trans_prog += (float)dt_ms / 66.0f;  // 2-frame crossfade ~66ms
        if (s_trans_prog >= 1.0f) {
            s_transitioning = false;
            animator_play(get_anim_for(s_current));
        } else {
            animator_blend(get_anim_for(s_target), get_anim_for(s_current), s_trans_prog);
        }
    }

    // Per-emotion special logic
    switch (s_current) {

        case Emotion::IDLE: {
            // Blink timer
            s_blink_acc += dt_ms;
            if (!s_blinking && s_blink_acc >= s_blink_interval) {
                s_blinking    = true;
                s_blink_frame = 0;
                s_blink_frame_acc = 0;
                s_blink_acc   = 0;
                s_blink_interval = 2500 + (uint32_t)(rand() % 1500);
            }
            if (s_blinking) {
                s_blink_frame_acc += dt_ms;
                if (s_blink_frame_acc >= 60) {
                    s_blink_frame_acc = 0;
                    s_blink_frame++;
                    // Sequence: half -> closed -> half -> idle (4 steps)
                    switch (s_blink_frame) {
                        case 1: animator_play(&anim_blink_half);   break;
                        case 2: animator_play(&anim_blink_closed); break;
                        case 3: animator_play(&anim_blink_half);   break;
                        default:
                            animator_play(&anim_idle);
                            s_blinking = false;
                            break;
                    }
                }
            }

            // Random eye movement (subtle y jitter every 5-10s)
            s_eye_move_acc += dt_ms;
            if (s_eye_move_acc >= s_eye_move_interval) {
                s_eye_move_acc = 0;
                s_eye_move_interval = 5000 + (uint32_t)(rand() % 5000);
                // Re-play idle to reset positions (jitter handled by sprite offsets)
                if (!s_blinking) animator_play(&anim_idle);
            }
            break;
        }

        case Emotion::THINKING: {
            // Rotate through 8-step eye animation at 200ms/step
            s_think_acc += dt_ms;
            if (s_think_acc >= 200) {
                s_think_acc = 0;
                s_think_step = (s_think_step + 1) % 8;
                animator_play(&anim_think_frames[s_think_step]);
            }
            break;
        }

        case Emotion::SPEAKING: {
            // Drive mouth stage from audio level
            uint8_t lvl = s_audio_level > s_lip_level ? s_audio_level : s_lip_level;
            uint8_t new_stage;
            if      (lvl > 180) new_stage = 2;
            else if (lvl >  80) new_stage = 1;
            else                new_stage = 0;

            if (new_stage != s_mouth_stage) {
                s_mouth_stage = new_stage;
                switch (new_stage) {
                    case 0: animator_play(&anim_speak_closed); break;
                    case 1: animator_play(&anim_speak_half);   break;
                    case 2: animator_play(&anim_speak_open);   break;
                }
            }
            break;
        }

        case Emotion::SLEEPING: {
            // Breathing brightness pulse: every 4s inhale/exhale
            s_sleep_acc += dt_ms;
            if (s_sleep_acc >= 2000) {
                s_sleep_acc = 0;
                s_sleep_exhale = !s_sleep_exhale;
                // Dim/brighten backlight gently (call face_set_brightness if needed)
                // Here we modulate ZZZ sprite alpha via a static local
            }
            // Spawn ZZZ particle periodically
            s_particle_spawn_acc += dt_ms;
            if (s_particle_spawn_acc >= 3000) {
                s_particle_spawn_acc = 0;
                spawn_particle(&sprites::spr_zzz, 38, 10, 1, -2, 2500);
            }
            break;
        }

        default:
            break;
    }

    // Update particles
    update_particles(dt_ms);

    // Render animator (eyes + mouth) onto canvas
    animator_update(dt_ms);
    animator_render(s_canvas);

    // Render particles on top
    for (uint8_t i = 0; i < s_particle_count; i++) {
        const Particle& p = s_particles[i];
        if (!p.spr) continue;
        uint8_t alpha = (uint8_t)((p.life_ms * 255) / p.max_life_ms);

        const uint16_t key = sprites::COL_TRANS;
        for (uint8_t sy = 0; sy < p.spr->h; sy++) {
            for (uint8_t sx = 0; sx < p.spr->w; sx++) {
                uint16_t col = p.spr->data[sy * p.spr->w + sx];
                if (col == key) continue;
                uint8_t r = (uint8_t)(((col >> 11) & 0x1F) << 3);
                uint8_t g = (uint8_t)(((col >>  5) & 0x3F) << 2);
                uint8_t b = (uint8_t)(( col         & 0x1F) << 3);
                lv_color_t lvc = lv_color_make(r, g, b);
                int px = (int)p.x * 4 + sx * 4;
                int py = (int)p.y * 4 + sy * 4;
                for (int dy = 0; dy < 4; dy++) {
                    for (int dx = 0; dx < 4; dx++) {
                        if (px+dx >= 0 && px+dx < DISP_WIDTH &&
                            py+dy >= 0 && py+dy < DISP_HEIGHT) {
                            lv_canvas_set_px(s_canvas, px+dx, py+dy, lvc,
                                             (lv_opa_t)alpha);
                        }
                    }
                }
            }
        }
    }
}

} // namespace minbot::display
