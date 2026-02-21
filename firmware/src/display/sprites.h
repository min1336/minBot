#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Pixel-art sprite data for minBot face (RGB565, stored in flash)
//
// Coordinate system: 240x240 canvas, origin top-left.
// Sprites use "chunky pixel" style: each logical pixel is 4x4 hardware pixels.
// Logical grid = 60x60 cells; each sprite dimension is given in logical pixels.
//
// Colour palette (RGB565):
//   BLACK       0x0000    WHITE       0xFFFF
//   SKIN        0xFDAF    EYE_DARK    0x1082
//   EYE_SHINE   0xFFFF    BLUSH       0xF810
//   HEART_RED   0xF800    TEAR_BLUE   0x041F
//   STAR_YELLOW 0xFFE0    ZZZ_GRAY    0x8410
//   BG          0x0861    (dark navy)
// ---------------------------------------------------------------------------

namespace minbot::display::sprites {

// ---------------------------------------------------------------------------
// Helper: encode RGB888 -> RGB565 at compile time
// ---------------------------------------------------------------------------
#define RGB565(r, g, b) \
    ((uint16_t)(((r) & 0xF8) << 8) | (uint16_t)(((g) & 0xFC) << 3) | (uint16_t)((b) >> 3))

static constexpr uint16_t COL_BLACK  = RGB565(0x00, 0x00, 0x00);
static constexpr uint16_t COL_WHITE  = RGB565(0xFF, 0xFF, 0xFF);
static constexpr uint16_t COL_TRANS  = 0xF81F;  // magenta used as transparency key
static constexpr uint16_t COL_BG     = RGB565(0x08, 0x10, 0x30);
static constexpr uint16_t COL_SHINE  = RGB565(0xFF, 0xFF, 0xFF);
static constexpr uint16_t COL_PUPIL  = RGB565(0x10, 0x10, 0x20);
static constexpr uint16_t COL_BLUSH  = RGB565(0xFF, 0x60, 0x80);
static constexpr uint16_t COL_HEART  = RGB565(0xFF, 0x10, 0x10);
static constexpr uint16_t COL_TEAR   = RGB565(0x40, 0x80, 0xFF);
static constexpr uint16_t COL_STAR   = RGB565(0xFF, 0xE0, 0x00);
static constexpr uint16_t COL_ZZZ    = RGB565(0xA0, 0xA0, 0xA0);

// ---------------------------------------------------------------------------
// Sprite descriptor
// ---------------------------------------------------------------------------
struct Sprite {
    uint8_t         w;          // logical width  (hardware px = w*4)
    uint8_t         h;          // logical height (hardware px = h*4)
    const uint16_t* data;       // RGB565 pixels, row-major, w*h entries
    uint16_t        key_color;  // transparent colour (COL_TRANS if keyed)
};

// ---------------------------------------------------------------------------
// Eye sprites  (12x8 logical px = 48x32 hw px)
// Each row = 12 uint16_t values
// ---------------------------------------------------------------------------
#define _ COL_TRANS
#define B COL_PUPIL
#define W COL_WHITE
#define S COL_SHINE

// IDLE eye — soft oval
static const uint16_t eye_idle_data[8*12] = {
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
};
static const Sprite eye_idle = { 12, 8, eye_idle_data, COL_TRANS };

// LISTENING eye — wider oval
static const uint16_t eye_listen_data[10*12] = {
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
};
static const Sprite eye_listen = { 12, 10, eye_listen_data, COL_TRANS };

// THINKING eye — side-glance (pupil offset right)
static const uint16_t eye_think_data[8*12] = {
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,S,S,B,B,
    B,B,B,B,B,B,B,B,S,S,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
};
static const Sprite eye_think = { 12, 8, eye_think_data, COL_TRANS };

// SPEAKING eye — slightly narrowed
static const uint16_t eye_speak_data[6*12] = {
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,_,
};
static const Sprite eye_speak = { 12, 6, eye_speak_data, COL_TRANS };

// HAPPY eye — crescent (^_^)
static const uint16_t eye_happy_data[5*12] = {
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
    _,_,_,B,B,B,B,B,_,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,
};
static const Sprite eye_happy = { 12, 5, eye_happy_data, COL_TRANS };

// SAD eye — droopy (pupils low, upper lid heavy)
static const uint16_t eye_sad_data[8*12] = {
    B,B,B,B,B,B,B,B,B,B,B,B,  // heavy lid
    B,B,B,B,B,B,B,B,B,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,_,B,B,B,S,S,B,B,B,_,_,
    _,B,B,B,B,S,S,B,B,B,B,_,
    _,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
};
static const Sprite eye_sad = { 12, 8, eye_sad_data, COL_TRANS };

// SURPRISED eye — large circle
static const uint16_t eye_surprised_data[12*12] = {
    _,_,_,B,B,B,B,B,B,_,_,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    B,B,B,B,S,S,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    B,B,B,B,B,B,B,B,B,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,_,_,B,B,B,B,B,B,_,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,
};
static const Sprite eye_surprised = { 12, 12, eye_surprised_data, COL_TRANS };

// SLEEPING eye — closed line
static const uint16_t eye_sleep_data[3*12] = {
    _,_,_,B,B,B,B,B,B,_,_,_,
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
};
static const Sprite eye_sleep = { 12, 3, eye_sleep_data, COL_TRANS };

// TILTED eye — swirl/dizzy (X shape)
static const uint16_t eye_tilt_data[8*8] = {
    B,B,_,_,_,_,B,B,
    B,B,B,_,_,B,B,_,
    _,B,B,B,B,B,_,_,
    _,_,B,B,B,B,_,_,
    _,_,B,B,B,B,_,_,
    _,B,B,B,B,B,_,_,
    B,B,B,_,_,B,B,_,
    B,B,_,_,_,_,B,B,
};
static const Sprite eye_tilt = { 8, 8, eye_tilt_data, COL_TRANS };

// Blink intermediate (half-closed)
static const uint16_t eye_blink_half_data[4*12] = {
    B,B,B,B,B,B,B,B,B,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,_,_,
    _,_,_,_,_,_,_,_,_,_,_,_,
};
static const Sprite eye_blink_half = { 12, 4, eye_blink_half_data, COL_TRANS };

// Blink closed
static const uint16_t eye_blink_closed_data[2*12] = {
    _,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,B,B,B,B,B,B,B,B,
};
static const Sprite eye_blink_closed = { 12, 2, eye_blink_closed_data, COL_TRANS };

#undef _
#undef B
#undef W
#undef S

// ---------------------------------------------------------------------------
// Mouth sprites  (16x8 logical px)
// ---------------------------------------------------------------------------
#define _ COL_TRANS
#define B COL_PUPIL

// Neutral / closed mouth
static const uint16_t mouth_closed_data[2*16] = {
    _,_,_,_,B,B,B,B,B,B,B,B,_,_,_,_,
    _,_,_,_,B,B,B,B,B,B,B,B,_,_,_,_,
};
static const Sprite mouth_closed = { 16, 2, mouth_closed_data, COL_TRANS };

// Half-open mouth
static const uint16_t mouth_half_data[5*16] = {
    _,_,_,B,B,B,B,B,B,B,B,B,B,_,_,_,
    _,_,B,B,B,B,B,B,B,B,B,B,B,B,_,_,
    _,B,B,B,_,_,_,_,_,_,_,_,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,B,B,B,B,_,_,
    _,_,_,B,B,B,B,B,B,B,B,B,B,_,_,_,
};
static const Sprite mouth_half = { 16, 5, mouth_half_data, COL_TRANS };

// Full open mouth (speaking loud)
static const uint16_t mouth_open_data[8*16] = {
    _,_,B,B,B,B,B,B,B,B,B,B,B,B,_,_,
    _,B,B,B,B,B,B,B,B,B,B,B,B,B,B,_,
    B,B,B,B,_,_,_,_,_,_,_,_,_,B,B,B,
    B,B,B,_,_,_,_,_,_,_,_,_,_,_,B,B,
    B,B,B,_,_,_,_,_,_,_,_,_,_,_,B,B,
    B,B,B,B,_,_,_,_,_,_,_,_,_,B,B,B,
    _,B,B,B,B,B,B,B,B,B,B,B,B,B,B,_,
    _,_,B,B,B,B,B,B,B,B,B,B,B,B,_,_,
};
static const Sprite mouth_open = { 16, 8, mouth_open_data, COL_TRANS };

// Happy smile
static const uint16_t mouth_smile_data[5*16] = {
    B,B,_,_,_,_,_,_,_,_,_,_,_,_,B,B,
    _,B,B,_,_,_,_,_,_,_,_,_,_,B,B,_,
    _,_,B,B,_,_,_,_,_,_,_,_,B,B,_,_,
    _,_,_,B,B,B,_,_,_,_,B,B,B,_,_,_,
    _,_,_,_,_,B,B,B,B,B,B,_,_,_,_,_,
};
static const Sprite mouth_smile = { 16, 5, mouth_smile_data, COL_TRANS };

// Sad frown
static const uint16_t mouth_frown_data[5*16] = {
    _,_,_,_,_,B,B,B,B,B,B,_,_,_,_,_,
    _,_,_,B,B,B,_,_,_,_,B,B,B,_,_,_,
    _,_,B,B,_,_,_,_,_,_,_,_,B,B,_,_,
    _,B,B,_,_,_,_,_,_,_,_,_,_,B,B,_,
    B,B,_,_,_,_,_,_,_,_,_,_,_,_,B,B,
};
static const Sprite mouth_frown = { 16, 5, mouth_frown_data, COL_TRANS };

#undef _
#undef B

// ---------------------------------------------------------------------------
// Effect sprites
// ---------------------------------------------------------------------------
#define _ COL_TRANS
#define H COL_HEART
#define T COL_TEAR
#define Y COL_STAR
#define Z COL_ZZZ

// Heart (8x7)
static const uint16_t heart_data[7*8] = {
    _,H,H,_,_,H,H,_,
    H,H,H,H,H,H,H,H,
    H,H,H,H,H,H,H,H,
    _,H,H,H,H,H,H,_,
    _,_,H,H,H,H,_,_,
    _,_,_,H,H,_,_,_,
    _,_,_,_,_,_,_,_,
};
static const Sprite spr_heart = { 8, 7, heart_data, COL_TRANS };

// Teardrop (4x8)
static const uint16_t tear_data[8*4] = {
    _,T,_,_,
    _,T,_,_,
    T,T,T,_,
    T,T,T,_,
    T,T,T,_,
    T,T,T,_,
    _,T,T,_,
    _,_,_,_,
};
static const Sprite spr_tear = { 4, 8, tear_data, COL_TRANS };

// Star (8x8)
static const uint16_t star_data[8*8] = {
    _,_,_,Y,Y,_,_,_,
    _,_,_,Y,Y,_,_,_,
    Y,_,_,Y,Y,_,_,Y,
    Y,Y,Y,Y,Y,Y,Y,Y,
    Y,Y,Y,Y,Y,Y,Y,Y,
    Y,_,_,Y,Y,_,_,Y,
    _,_,_,Y,Y,_,_,_,
    _,_,_,Y,Y,_,_,_,
};
static const Sprite spr_star = { 8, 8, star_data, COL_TRANS };

// ZZZ (12x6)
static const uint16_t zzz_data[6*12] = {
    Z,Z,Z,Z,Z,_,_,_,_,_,_,_,
    _,_,_,Z,Z,_,_,_,_,_,_,_,
    _,_,Z,Z,_,Z,Z,Z,Z,_,_,_,
    _,Z,Z,_,_,_,_,Z,Z,_,_,_,
    Z,Z,_,_,_,_,Z,Z,Z,Z,Z,Z,
    _,_,_,_,_,_,_,_,_,_,_,_,
};
static const Sprite spr_zzz = { 12, 6, zzz_data, COL_TRANS };

// Exclamation mark (3x10)
static const uint16_t exclaim_data[10*3] = {
    COL_WHITE,COL_WHITE,COL_WHITE,
    COL_WHITE,COL_WHITE,COL_WHITE,
    COL_WHITE,COL_WHITE,COL_WHITE,
    COL_WHITE,COL_WHITE,COL_WHITE,
    COL_WHITE,COL_WHITE,COL_WHITE,
    COL_WHITE,COL_WHITE,COL_WHITE,
    _,_,_,
    _,_,_,
    COL_WHITE,COL_WHITE,COL_WHITE,
    COL_WHITE,COL_WHITE,COL_WHITE,
};
static const Sprite spr_exclaim = { 3, 10, exclaim_data, COL_TRANS };

// Thinking dots "..." (14x3)
static const uint16_t dots_data[3*14] = {
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,
    COL_ZZZ,COL_ZZZ,_,COL_ZZZ,COL_ZZZ,_,COL_ZZZ,COL_ZZZ,_,_,_,_,_,_,
    COL_ZZZ,COL_ZZZ,_,COL_ZZZ,COL_ZZZ,_,COL_ZZZ,COL_ZZZ,_,_,_,_,_,_,
};
static const Sprite spr_dots = { 14, 3, dots_data, COL_TRANS };

// Blush (10x4, semi-transparent look via sparse pixels)
static const uint16_t blush_data[4*10] = {
    _,COL_BLUSH,_,COL_BLUSH,_,COL_BLUSH,_,COL_BLUSH,_,_,
    COL_BLUSH,_,COL_BLUSH,_,COL_BLUSH,_,COL_BLUSH,_,COL_BLUSH,_,
    _,COL_BLUSH,_,COL_BLUSH,_,COL_BLUSH,_,COL_BLUSH,_,_,
    _,_,_,_,_,_,_,_,_,_,
};
static const Sprite spr_blush = { 10, 4, blush_data, COL_TRANS };

#undef _
#undef H
#undef T
#undef Y
#undef Z

} // namespace minbot::display::sprites
