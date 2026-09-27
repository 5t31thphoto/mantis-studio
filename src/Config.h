#pragma once
#include <Arduino.h>

namespace Mantis {
constexpr int W = 320;
constexpr int H = 240;
constexpr uint32_t PPQ = 96;
constexpr uint8_t BEATS_PER_BAR = 4;
constexpr uint8_t DEFAULT_BARS = 4;
constexpr float DEFAULT_BPM = 110.0f;
constexpr float MIN_BPM = 50.0f;
constexpr float MAX_BPM = 200.0f;
constexpr uint32_t MAX_TICKS = PPQ * BEATS_PER_BAR * 16; // 16-bar maximum
constexpr uint16_t MAX_EVENTS = 384;
constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t AUDIO_MAX_SECONDS = 30;
constexpr uint32_t AUDIO_MAX_SAMPLES = SAMPLE_RATE * AUDIO_MAX_SECONDS;
constexpr uint16_t DRUM_MAX_MS = 900;
constexpr uint16_t SYNTH_MAX_MS = 900;
constexpr uint16_t COLOR_TEAL = 0x03B9;
constexpr uint16_t COLOR_PLUM = 0x500B;
constexpr uint16_t COLOR_LIME = 0x07E0;
constexpr uint16_t COLOR_BG = 0x18C3;
constexpr uint16_t COLOR_PANEL = 0x2126;
}
