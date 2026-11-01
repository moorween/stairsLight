#include "BackgroundEffects.h"
#include <cassert>
#include <iostream>

namespace {
bool equal(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
int brightness(Rgb c) { return c.r + c.g + c.b; }

void catalogAndPersistence() {
    assert(effectCount == 11);
    assert(static_cast<uint8_t>(BackgroundEffect::Rainbow) == 5);
    for (uint8_t id = 0; id < effectCount; ++id) {
        LightSettings c;
        c.effect = id;
        c.normalize();
        assert(c.effect == id); // All saved IDs, including the old six, survive loading.
    }
}

void boundsAndBrightness() {
    LightSettings c;
    c.backgroundColor = 0xFFFFFF;
    for (uint8_t mode = 0; mode < effectCount; ++mode) {
        c.effect = mode;
        for (uint8_t speed : {0, 255}) for (uint8_t intensity : {0, 255}) {
            c.speed = speed; c.intensity = intensity;
            for (uint8_t limit : {0, 73, 255}) {
                c.backgroundBrightness = limit;
                for (uint32_t now : {0U, 1U, 250U, 1500U, 17000U, UINT32_MAX}) {
                    for (size_t s = 0; s < hw::stepCount; ++s)
                        for (size_t x = 0; x < hw::stepLengths[s]; ++x) {
                            const auto pixel = BackgroundEffects::sample(c, s, x, now);
                            assert(pixel.r <= limit && pixel.g <= limit && pixel.b <= limit);
                        }
                }
            }
        }
    }
}

void selectedColorAndAnimation() {
    LightSettings c;
    c.backgroundBrightness = 255;
    c.backgroundColor = 0xFF0000;
    c.speed = c.intensity = 255;
    for (auto mode : {BackgroundEffect::Candle, BackgroundEffect::Scanner,
                      BackgroundEffect::Checkerboard, BackgroundEffect::Rain}) {
        c.effect = static_cast<uint8_t>(mode);
        bool changes = false;
        const auto first = BackgroundEffects::sample(c, 4, 0, 0);
        for (uint32_t now = 0; now < 2000; now += 50) {
            for (size_t x = 0; x < hw::stepLengths[4]; ++x) {
                const auto pixel = BackgroundEffects::sample(c, 4, x, now);
                assert(pixel.g == 0 && pixel.b == 0);
                changes |= !equal(first, pixel);
            }
        }
        assert(changes);
    }
    c.effect = static_cast<uint8_t>(BackgroundEffect::ColorWave);
    c.intensity = 0;
    assert(equal(BackgroundEffects::sample(c, 4, 3, 700), Rgb{255, 0, 0}));
    c.intensity = 255;
    bool colored = false;
    for (uint32_t now = 0; now < 2000; now += 100) {
        const auto pixel = BackgroundEffects::sample(c, 4, 3, now);
        colored |= pixel.g > 0 || pixel.b > 0;
    }
    assert(colored);
}

void spatialBehavior() {
    LightSettings c;
    c.backgroundBrightness = 255;
    c.backgroundColor = 0xFFFFFF;
    c.speed = c.intensity = 255; // A complete cycle takes 2000 ms.
    c.effect = static_cast<uint8_t>(BackgroundEffect::Scanner);
    for (size_t s = 0; s < hw::stepCount; ++s) {
        const size_t right = hw::stepLengths[s] - 1;
        assert(brightness(BackgroundEffects::sample(c, s, 0, 0)) >
               brightness(BackgroundEffects::sample(c, s, right, 0)));
        assert(brightness(BackgroundEffects::sample(c, s, right, 1000)) >
               brightness(BackgroundEffects::sample(c, s, 0, 1000)));
    }
    c.effect = static_cast<uint8_t>(BackgroundEffect::Candle);
    for (size_t s = 0; s < hw::stepCount; ++s)
        assert(equal(BackgroundEffects::sample(c, s, 0, 175),
                     BackgroundEffects::sample(c, s, hw::stepLengths[s] - 1, 175)));
    c.effect = static_cast<uint8_t>(BackgroundEffect::Checkerboard);
    assert(!equal(BackgroundEffects::sample(c, 0, 0, 0), BackgroundEffects::sample(c, 1, 0, 0)));
    assert(equal(BackgroundEffects::sample(c, 0, 0, 0), BackgroundEffects::sample(c, 1, 0, 1000)));
    c.effect = static_cast<uint8_t>(BackgroundEffect::Rain);
    auto peakStep = [&](uint32_t now) {
        size_t peak = 0;
        for (size_t s = 1; s < hw::stepCount; ++s)
            if (brightness(BackgroundEffects::sample(c, s, 0, now)) >
                brightness(BackgroundEffects::sample(c, peak, 0, now))) peak = s;
        return peak;
    };
    bool checkedFall = false;
    for (uint32_t now = 0; now < 2000; now += 100) {
        const size_t before = peakStep(now);
        if (before >= 4 && before <= 11) {
            assert(peakStep(now + 250) < before);
            checkedFall = true;
        }
    }
    assert(checkedFall);
}
}

int main() {
    catalogAndPersistence(); boundsAndBrightness(); selectedColorAndAnimation(); spatialBehavior();
    std::cout << "PASS: 11 effect IDs, brightness limits, selected colors, animation,\n"
                 "scanner alignment, coherent candle, alternating checks, falling rain\n";
}
