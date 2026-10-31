#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace hw {
inline constexpr int pirBottomPin = 26;
inline constexpr int pirTopPin = 25;
inline constexpr int ledPin = 23;
inline constexpr bool sacrificialLed = true;
inline constexpr bool firstStepReversed = true;
inline constexpr bool pirActiveHigh = true;
inline constexpr uint32_t pirWarmupMs = 30000;
inline constexpr uint32_t frameMs = 20;
// Set RGB order here if your strip differs. Default WS2812 = GRB.
inline constexpr std::array<uint8_t, 3> wireChannels{1, 0, 2};
inline constexpr std::array<uint16_t, 15> stepLengths{
    19, 19, 19, 19, 18, 19, 25, 19, 18, 18, 18, 18, 18, 18, 18};
inline constexpr size_t stepCount = stepLengths.size();
constexpr size_t countLeds() {
    size_t n = 0;
    for (auto len : stepLengths) n += len;
    return n;
}
inline constexpr size_t activeLeds = countLeds();
inline constexpr size_t ledCount = activeLeds + (sacrificialLed ? 1 : 0);
constexpr size_t stepOffset(size_t step) {
    size_t n = sacrificialLed ? 1 : 0;
    for (size_t i = 0; i < step; ++i) n += stepLengths[i];
    return n;
}
constexpr size_t pixelIndex(size_t step, size_t x) {
    const bool reversed = firstStepReversed != bool(step % 2);
    return stepOffset(step) + (reversed ? stepLengths[step] - 1 - x : x);
}
constexpr bool validGeometry() {
    for (auto len : stepLengths) if (!len) return false;
    return stepCount > 0 && ledCount <= 2048;
}
static_assert(validGeometry(), "Invalid LED geometry");
static_assert(wireChannels[0] < 3 && wireChannels[1] < 3 && wireChannels[2] < 3 &&
              wireChannels[0] != wireChannels[1] && wireChannels[0] != wireChannels[2] &&
              wireChannels[1] != wireChannels[2], "wireChannels must be a permutation of 0,1,2");
static_assert(ledPin != pirBottomPin && ledPin != pirTopPin && pirBottomPin != pirTopPin,
              "GPIO pins must be different");
}
