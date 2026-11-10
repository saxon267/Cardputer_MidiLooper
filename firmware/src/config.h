#pragma once
#include <stdint.h>

// ---------------------------------------------------------------- hardware
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_I2S_BCLK  41
#define PIN_I2S_LRCK  43
#define PIN_I2S_DOUT  42
#define PIN_I2S_DIN   46

#define PIN_SD_SCK    40
#define PIN_SD_MISO   39
#define PIN_SD_MOSI   14
#define PIN_SD_CS     12

#define PIN_TCA_INT   11

// ---------------------------------------------------------------- audio
static constexpr uint32_t AUDIO_RATE  = 44100;
static constexpr int      AUDIO_BLOCK = 64;   // 1.45 ms: USB-MIDI timing base
static constexpr int      CLICK_MS    = 28;

// ---------------------------------------------------------------- loop
static constexpr int LOOP_MAX_BARS   = 32;
static constexpr int LOOP_PPQN       = 480;
static constexpr int LOOP_BEATS      = 4;     // 4/4
static constexpr int MAX_LOOP_EVENTS = 8192;  // 8 B each = 64 KB per buffer
static constexpr int MAX_TAKE_EVENTS = 4096;  // 8 B each = 32 KB

// ---------------------------------------------------------------- files
static constexpr int MAX_FILES = 64;
#define LOOP_DIR "/MidiLoop"

// ---------------------------------------------------------------- layout
static constexpr int SCR_W = 240;
static constexpr int SCR_H = 135;
static constexpr int TOP_H = 13;
static constexpr int BOT_H = 11;
static constexpr int BOT_Y = SCR_H - BOT_H;

// ---------------------------------------------------------------- Gruvbox Dark
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define C_BG      RGB565(29, 32, 33)     // #1D2021
#define C_BAR     RGB565(40, 40, 40)     // #282828
#define C_PANEL   RGB565(40, 40, 40)
#define C_PAD     RGB565(50, 48, 47)     // #32302F
#define C_EDGE    RGB565(80, 73, 69)     // #504945
#define C_LINE    RGB565(60, 56, 54)     // #3C3836
#define C_TXT     RGB565(235, 219, 178)  // #EBDBB2
#define C_LT2     RGB565(213, 196, 161)  // #D5C4A1
#define C_DIM     RGB565(168, 153, 132)  // #A89984
#define C_FAINT   RGB565(124, 111, 100)  // #7C6F64
#define C_ACC     RGB565(254, 128, 25)   // #FE8019
#define C_ACC_DIM RGB565(74, 42, 23)     // #4A2A17
#define C_AMB     RGB565(250, 189, 47)   // #FABD2F
#define C_AQUA    RGB565(142, 192, 124)  // #8EC07C
#define C_BLUE    RGB565(131, 165, 152)  // #83A598
#define C_PURPLE  RGB565(211, 134, 155)
#define C_RED     RGB565(251, 73, 52)    // #FB4934
#define C_GREEN   RGB565(184, 187, 38)   // #B8BB26

#define APP_VERSION "1.3.2"
