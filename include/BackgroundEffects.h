#pragma once
#include "HardwareConfig.h"
#include "LightSettings.h"
#include <cmath>

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

class BackgroundEffects {
public:
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
    static Rgb sample(const LightSettings& c, size_t s, size_t x, uint32_t now) {
        const float period = 18000.0f - c.speed * (16000.0f / 255);
        const float time = std::fmod(float(now), period) / period;
        const float y = float(s) / std::max<size_t>(1, hw::stepCount - 1);
        const float width = float(x) / std::max<uint16_t>(1, hw::stepLengths[s] - 1);
        const float depth = c.intensity / 255.0f;
        constexpr float tau = 6.28318530718f;
        float brightness = 1;
        Rgb base = unpack(c.backgroundColor);
        switch (static_cast<BackgroundEffect>(c.effect)) {
            case BackgroundEffect::Breathing: brightness = 1 - depth * (0.5f + 0.5f * std::cos(time * tau)); break;
            case BackgroundEffect::Wave: brightness = 1 - depth * (0.5f + 0.5f * std::cos((y - time) * tau)); break;
            case BackgroundEffect::Comet: {
                float distance = time - y;
                distance -= std::floor(distance);
                const float tail = 0.08f + depth * 0.5f;
                brightness = 0.03f + 0.97f * std::max(0.0f, 1 - distance / tail);
                break;
            }
            case BackgroundEffect::Twinkle: {
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
            case BackgroundEffect::Rainbow: base = rainbow(time + y * depth + width * depth * 0.25f); break;
            case BackgroundEffect::Candle: {
                // A coherent flame on each step rather than independent pixel noise.
                const float tick = float(now) / std::max(40.0f, period / 40);
                const uint32_t block = uint32_t(tick);
                float t = tick - block;
                t = t * t * (3 - 2 * t);
                const float a = (hash(uint32_t(s) ^ (block * 7919U)) & 255) / 255.0f;
                const float b = (hash(uint32_t(s) ^ ((block + 1) * 7919U)) & 255) / 255.0f;
                brightness = 1 - depth * (0.15f + 0.65f * (a + (b - a) * t));
                break;
            }
            case BackgroundEffect::Scanner: {
                // Shared horizontal position respects the strip's serpentine layout.
                const float head = 1 - std::abs(2 * time - 1);
                const float radius = 0.06f + depth * 0.3f;
                const float beam = std::max(0.0f, 1 - std::abs(width - head) / radius);
                brightness = 0.03f + 0.97f * beam * beam * (3 - 2 * beam);
                break;
            }
            case BackgroundEffect::Checkerboard: {
                const size_t column = std::min<size_t>(3, size_t(width * 4));
                const float pulse = 0.5f + 0.5f * std::cos(time * tau);
                brightness = 1 - depth * ((s + column) % 2 ? pulse : 1 - pulse);
                break;
            }
            case BackgroundEffect::ColorWave: {
                const float wave = 0.5f + 0.5f * std::sin((y + width * 0.5f - time) * tau);
                base = blend(base, rainbow(time + y * 0.6f + width * 0.4f), depth * wave);
                break;
            }
            case BackgroundEffect::Rain: {
                // Eight logical lanes align even on steps with different LED counts.
                const uint32_t lane = std::min<uint32_t>(7, uint32_t(width * 8));
                const float offset = (hash(lane + 700U) & 65535) / 65536.0f;
                float distance = time + offset - (1 - y);
                distance -= std::floor(distance);
                const float tail = 0.08f + depth * 0.3f;
                const float drop = std::max(0.0f, 1 - distance / tail);
                brightness = 0.02f + 0.98f * drop * drop;
                break;
            }
            default: break;
        }
        return scaled(base, brightness * c.backgroundBrightness / 255.0f);
    }
};
