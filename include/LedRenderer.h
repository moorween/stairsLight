#pragma once
#include "HardwareConfig.h"
#include "LightSettings.h"
#include "BackgroundEffects.h"
#include "MotionEngine.h"
#include <esp32-hal-rmt.h>
#include <array>

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
            const auto foreground = BackgroundEffects::scaled(
                BackgroundEffects::unpack(settings.motionColor), settings.motionBrightness / 255.0f);
            for (size_t x = 0; x < hw::stepLengths[s]; ++x) {
                auto background = settings.backgroundOn ? BackgroundEffects::sample(settings, s, x, now) : Rgb{};
                auto color = BackgroundEffects::blend(background, foreground, mix);
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
};
