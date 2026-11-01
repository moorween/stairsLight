#include "HardwareConfig.h"
#include "MotionEngine.h"
#include "PirFilter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>

namespace {
LightSettings config() {
    LightSettings c;
    c.stepDelayMs = 100;
    c.fadeInMs = 200;
    c.fadeOutMs = 200;
    c.holdMs = 3000;
    return c;
}
void fullyOn(MotionEngine& e, uint32_t at = 100) {
    e.tick(at + 1600);
    assert(e.phase() == Phase::Holding);
    for (size_t i = 0; i < hw::stepCount; ++i) assert(e.level(i) == 1.0f);
}
void geometry() {
    assert(hw::activeLeds == 283 && hw::ledCount == 284);
    assert(hw::pixelIndex(0, 0) == 19 && hw::pixelIndex(0, 18) == 1);
    assert(hw::pixelIndex(1, 0) == 20 && hw::pixelIndex(1, 18) == 38);
    assert(hw::pixelIndex(6, 0) == hw::stepOffset(6) + 24);
    std::set<size_t> indices;
    for (size_t s = 0; s < hw::stepCount; ++s)
        for (size_t x = 0; x < hw::stepLengths[s]; ++x) {
            const auto i = hw::pixelIndex(s, x);
            assert(i > 0 && i < hw::ledCount);
            assert(indices.insert(i).second);
        }
    assert(indices.size() == 283);
}
void directions() {
    for (auto origin : {Direction::Up, Direction::Down}) {
        for (bool crossed : {false, true}) {
            MotionEngine e;
            e.configure(config(), 0);
            e.motion(origin, 100);
            e.tick(200);
            const size_t first = origin == Direction::Up ? 0 : hw::stepCount - 1;
            const size_t last = hw::stepCount - 1 - first;
            assert(e.level(first) > 0 && e.level(last) == 0);
            fullyOn(e);
            if (crossed) e.motion(origin == Direction::Up ? Direction::Down : Direction::Up, 1800);
            const uint32_t offAt = crossed ? 4800 : 3100;
            e.tick(offAt - 1);
            assert(e.phase() == Phase::Holding);
            e.tick(offAt);
            assert(e.phase() == Phase::Falling);
            const auto expected = crossed ? origin : origin == Direction::Up ? Direction::Down : Direction::Up;
            assert(e.transitionDirection() == expected);
            e.tick(offAt + 100);
            const size_t offFirst = expected == Direction::Up ? 0 : hw::stepCount - 1;
            assert(e.level(offFirst) < 1 && e.level(hw::stepCount - 1 - offFirst) == 1);
            e.tick(offAt + 1600);
            assert(e.phase() == Phase::Idle);
            for (size_t i = 0; i < hw::stepCount; ++i) assert(e.level(i) == 0);
        }
    }
}
void retrigger() {
    MotionEngine e;
    e.configure(config(), 0);
    e.motion(Direction::Up, 100);
    fullyOn(e);
    e.motion(Direction::Up, 2000);
    e.tick(4999);
    assert(e.phase() == Phase::Holding && !e.crossed());
    e.tick(5000);
    e.tick(5750);
    float before[hw::stepCount];
    for (size_t i = 0; i < hw::stepCount; ++i) before[i] = e.level(i);
    e.motion(Direction::Down, 5750);
    assert(e.phase() == Phase::Rising && e.origin() == Direction::Down);
    for (size_t i = 0; i < hw::stepCount; ++i) assert(e.level(i) == before[i]);
    e.tick(5800);
    for (size_t i = 0; i < hw::stepCount; ++i) assert(e.level(i) >= before[i]);
}
void heldHigh() {
    MotionEngine e;
    e.configure(config(), 0);
    e.sensors(true, false, true, false, 100);
    fullyOn(e);
    e.tick(90000);
    assert(e.phase() == Phase::Holding);
    e.sensors(false, false, false, false, 90001);
    e.tick(92999);
    assert(e.phase() == Phase::Holding);
    e.tick(93000);
    assert(e.phase() == Phase::Falling);
}
void disabledAndReenabled() {
    MotionEngine e;
    auto c = config();
    e.configure(c, 0);
    e.motion(Direction::Up, 100);
    fullyOn(e);
    e.sensors(true, false, false, false, 1800);
    c.motionEnabled = false;
    e.configure(c, 1800);
    assert(e.phase() == Phase::Falling);
    e.tick(3400);
    assert(e.phase() == Phase::Idle);
    e.motion(Direction::Down, 3500);
    assert(e.phase() == Phase::Idle);
    c.motionEnabled = true;
    e.configure(c, 3600);
    e.sensors(true, false, false, false, 3600);
    assert(e.phase() == Phase::Rising);
}
void simultaneous() {
    MotionEngine e;
    e.configure(config(), 0);
    e.sensors(true, true, true, true, 100);
    e.tick(200);
    for (size_t i = 1; i < hw::stepCount; ++i) assert(e.level(i) == e.level(0));
    e.sensors(false, false, false, false, 301);
    e.tick(3300);
    assert(e.transitionDirection() == Direction::Both);
}
void minimumHoldAndLongRise() {
    auto c = config();
    c.holdMs = 1000;
    MotionEngine e;
    e.configure(c, 0);
    e.motion(Direction::Up, 100);
    e.tick(1200);
    assert(e.phase() == Phase::Rising);
    e.tick(1700);
    assert(e.phase() == Phase::Falling);
    assert(e.level(14) == 1); // Last step still fully lights before fading begins.
}
void zeroTimingsAndRollover() {
    auto c = config();
    c.stepDelayMs = c.fadeInMs = c.fadeOutMs = 0;
    MotionEngine e;
    e.configure(c, 0);
    const uint32_t start = UINT32_MAX - 1000;
    e.motion(Direction::Down, start);
    e.tick(start);
    assert(e.phase() == Phase::Holding);
    e.tick(start + 3000U);
    assert(e.phase() == Phase::Falling);
    e.tick(start + 3001U);
    assert(e.phase() == Phase::Idle);
}
void pirFiltering() {
    PirFilter f;
    PirSettings c;
    c.onMs = 80; c.offMs = 100; c.cooldownMs = 1000;
    f.reset(false, 0);
    assert(!f.update(true, 10, c));
    assert(!f.update(false, 50, c));
    assert(!f.active()); // Rejected short pulse.
    assert(!f.update(true, 100, c));
    assert(f.update(true, 180, c));
    assert(f.active());
    assert(!f.update(false, 200, c));
    assert(!f.update(true, 250, c));
    assert(f.active()); // Rejected short low gap.
    assert(!f.update(false, 300, c));
    assert(!f.update(false, 400, c));
    assert(!f.active());
    assert(!f.update(true, 500, c));
    assert(!f.update(true, 580, c)); // Repeated edge suppressed by cooldown.
    assert(f.active()); // Occupancy remains true despite suppression.
    f.update(false, 700, c); f.update(false, 800, c);
    f.update(true, 1200, c);
    assert(f.update(true, 1280, c));
    assert(!f.update(true, 9999, c)); // A held HIGH doesn't create repeated edges.
    const uint32_t start = UINT32_MAX - 40;
    f.reset(false, start);
    f.update(true, start, c);
    assert(f.update(true, start + 80U, c));
}
void validation() {
    auto c = config();
    c.effect = 255; c.stepDelayMs = UINT32_MAX; c.holdMs = 0;
    c.bottom.onMs = UINT32_MAX; c.top.offMs = UINT32_MAX;
    c.backgroundColor = UINT32_MAX;
    c.normalize();
    assert(c.effect == effectCount - 1 && c.stepDelayMs == 3000 && c.holdMs == 1000);
    assert(c.bottom.onMs == 5000 && c.top.offMs == 10000);
    assert(c.backgroundColor == 0xFFFFFF);
}
}

int main() {
    geometry(); directions(); retrigger(); heldHigh(); disabledAndReenabled();
    simultaneous(); minimumHoldAndLongRise(); zeroTimingsAndRollover(); pirFiltering(); validation();
    std::cout << "PASS: geometry, 4 direction scenarios, retrigger, occupancy, enable/disable,\n"
                 "simultaneous PIR, timing limits, millis rollover, PIR filtering, validation\n";
}
