#pragma once
#include <algorithm>
#include <cstdint>
#include <tuple>

struct PirSettings {
    uint32_t onMs = 80;
    uint32_t offMs = 150;
    uint32_t cooldownMs = 1000;
    bool inverted = false;
    bool operator==(const PirSettings& o) const {
        return std::tie(onMs, offMs, cooldownMs, inverted) ==
               std::tie(o.onMs, o.offMs, o.cooldownMs, o.inverted);
    }
};

struct LightSettings {
    bool backgroundOn = false;
    bool motionEnabled = true;
    uint8_t backgroundBrightness = 35;
    uint8_t motionBrightness = 180;
    uint32_t backgroundColor = 0xFFAA55;
    uint32_t motionColor = 0xFFE0B0;
    uint8_t effect = 0;
    uint8_t speed = 100;
    uint8_t intensity = 120;
    uint32_t stepDelayMs = 120;
    uint32_t fadeInMs = 400;
    uint32_t fadeOutMs = 500;
    uint32_t holdMs = 20000;
    PirSettings bottom;
    PirSettings top;
    bool operator==(const LightSettings& o) const {
        return std::tie(backgroundOn, motionEnabled, backgroundBrightness, motionBrightness,
                        backgroundColor, motionColor, effect, speed, intensity, stepDelayMs,
                        fadeInMs, fadeOutMs, holdMs, bottom, top) ==
               std::tie(o.backgroundOn, o.motionEnabled, o.backgroundBrightness, o.motionBrightness,
                        o.backgroundColor, o.motionColor, o.effect, o.speed, o.intensity,
                        o.stepDelayMs, o.fadeInMs, o.fadeOutMs, o.holdMs, o.bottom, o.top);
    }
    void normalize() {
        backgroundColor &= 0xFFFFFF;
        motionColor &= 0xFFFFFF;
        effect = std::min<uint8_t>(effect, 5);
        stepDelayMs = std::min<uint32_t>(stepDelayMs, 3000);
        fadeInMs = std::min<uint32_t>(fadeInMs, 10000);
        fadeOutMs = std::min<uint32_t>(fadeOutMs, 10000);
        holdMs = std::clamp<uint32_t>(holdMs, 1000, 600000);
        for (auto* p : {&bottom, &top}) {
            p->onMs = std::min<uint32_t>(p->onMs, 5000);
            p->offMs = std::min<uint32_t>(p->offMs, 10000);
            p->cooldownMs = std::min<uint32_t>(p->cooldownMs, 30000);
        }
    }
};

inline constexpr const char* effectOptions = "Solid;Breathing;Wave;Comet;Twinkle;Rainbow";
