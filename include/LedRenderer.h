#pragma once
#include "HardwareConfig.h"
#include "LightSettings.h"
#include "MotionEngine.h"
#include <esp32-hal-rmt.h>
#include <array>
#include <cmath>

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

class LedRenderer {
public:
    bool begin() {
        ready_ = rmtInit(hw::ledPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 40000000);
        if (ready_) rmtSetEOT(hw::ledPin, LOW);
        return ready_;
    }
    bool ready() const { return ready_; }
    uint32_t errors() const { return errors_; }
    void render(const LightSettings& settings, const MotionEngine& motion, uint32_t now,
                bool ota, int testStep = -1) {
        pixels_.fill({});
        for (size_t s = 0; s < hw::stepCount; ++s) {
            const float mix = motion.level(s);
            const auto foreground = scaled(unpack(settings.motionColor), settings.motionBrightness / 255.0f);
            for (size_t x = 0; x < hw::stepLengths[s]; ++x) {
                auto background = settings.backgroundOn ? effect(settings, s, x, now) : Rgb{};
                auto color = blend(background, foreground, mix);
                if (testStep >= 0) color = int(s) == testStep ? Rgb{100, 50, 10} : Rgb{};
                if (ota) color = {80, 60, 35};
                pixels_[hw::pixelIndex(s, x)] = color;
            }
        }
    }
    bool show() {
        if (!ready_) return false;
        size_t index = 0;
        for (const auto pixel : pixels_) {
            const uint8_t rgb[]{pixel.r, pixel.g, pixel.b};
            for (auto channel : hw::wireChannels) {
                const uint8_t byte = rgb[channel];
                for (int bit = 7; bit >= 0; --bit) {
                    auto& symbol = symbols_[index++];
                    symbol.level0 = 1;
                    symbol.level1 = 0;
                    const bool one = byte & (1U << bit);
                    symbol.duration0 = one ? 32 : 16;
                    symbol.duration1 = one ? 18 : 34;
                }
            }
        }
        // 300 us reset for WS2812 variants requiring a longer latch interval.
        auto& latch = symbols_[index++];
        latch.level0 = latch.level1 = 0;
        latch.duration0 = latch.duration1 = 6000;
        const bool ok = rmtWrite(hw::ledPin, symbols_.data(), index, RMT_WAIT_FOR_EVER);
        if (!ok) ++errors_;
        return ok;
    }
private:
    bool ready_ = false;
    uint32_t errors_ = 0;
    std::array<Rgb, hw::ledCount> pixels_{};
    std::array<rmt_data_t, hw::ledCount * 24 + 1> symbols_{};
    static Rgb unpack(uint32_t color) {
        return {uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color)};
    }
    static Rgb scaled(Rgb c, float scale) {
        scale = std::clamp(scale, 0.0f, 1.0f);
        return {uint8_t(c.r * scale), uint8_t(c.g * scale), uint8_t(c.b * scale)};
    }
    static Rgb blend(Rgb a, Rgb b, float t) {
        return {uint8_t(a.r + (b.r - a.r) * t), uint8_t(a.g + (b.g - a.g) * t),
                uint8_t(a.b + (b.b - a.b) * t)};
    }
    static uint32_t hash(uint32_t n) {
        n ^= n >> 16; n *= 0x7feb352d; n ^= n >> 15; n *= 0x846ca68b;
        return n ^ (n >> 16);
    }
    static Rgb rainbow(float hue) {
        hue -= std::floor(hue);
        const float h = hue * 6;
        const int i = int(h);
        const uint8_t q = uint8_t(255 * (1 - (h - i)));
        const uint8_t t = 255 - q;
        switch (i) {
            case 0: return {255, t, 0}; case 1: return {q, 255, 0};
            case 2: return {0, 255, t}; case 3: return {0, q, 255};
            case 4: return {t, 0, 255}; default: return {255, 0, q};
        }
    }
    static Rgb effect(const LightSettings& c, size_t s, size_t x, uint32_t now) {
        const float period = 18000.0f - c.speed * (16000.0f / 255);
        const float time = std::fmod(float(now), period) / period;
        const float y = float(s) / std::max<size_t>(1, hw::stepCount - 1);
        const float width = float(x) / std::max<uint16_t>(1, hw::stepLengths[s] - 1);
        const float depth = c.intensity / 255.0f;
        constexpr float tau = 6.28318530718f;
        float brightness = 1;
        Rgb base = unpack(c.backgroundColor);
        switch (c.effect) {
            case 1: brightness = 1 - depth * (0.5f + 0.5f * std::cos(time * tau)); break;
            case 2: brightness = 1 - depth * (0.5f + 0.5f * std::cos((y - time) * tau)); break;
            case 3: {
                float distance = time - y;
                distance -= std::floor(distance);
                const float tail = 0.08f + depth * 0.5f;
                brightness = 0.03f + 0.97f * std::max(0.0f, 1 - distance / tail);
                break;
            }
            case 4: {
                const float tick = float(now) / std::max(100.0f, period / 8);
                const uint32_t seed = hw::stepOffset(s) + x;
                const auto block = uint32_t(tick);
                float t = tick - block;
                t = t * t * (3 - 2 * t);
                const float a = (hash(seed ^ (block * 7919)) & 255) / 255.0f;
                const float b = (hash(seed ^ ((block + 1) * 7919)) & 255) / 255.0f;
                brightness = 1 - depth * (a + (b - a) * t);
                break;
            }
            case 5: base = rainbow(time + y * depth + width * depth * 0.25f); break;
            default: break;
        }
        return scaled(base, brightness * c.backgroundBrightness / 255.0f);
    }
};
