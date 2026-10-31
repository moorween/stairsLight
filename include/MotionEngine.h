#pragma once
#include "HardwareConfig.h"
#include "LightSettings.h"
#include <array>
#include <cmath>

enum class Phase : uint8_t { Idle, Rising, Holding, Falling };
enum class Direction : uint8_t { Up, Down, Both };
inline const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Idle: return "Idle";
        case Phase::Rising: return "Rising";
        case Phase::Holding: return "Holding";
        default: return "Falling";
    }
}
inline const char* directionName(Direction d) {
    return d == Direction::Up ? "Up" : d == Direction::Down ? "Down" : "Both";
}

class MotionEngine {
public:
    void configure(LightSettings config, uint32_t now) {
        config.normalize();
        const bool disabled = config_.motionEnabled && !config.motionEnabled;
        config_ = config;
        if (disabled && phase_ != Phase::Idle) {
            advance(now);
            beginTransition(false, origin_, now);
        }
    }
    void sensors(bool bottom, bool top, bool bottomEvent, bool topEvent, uint32_t now) {
        bottom_ = bottom; top_ = top;
        if (!config_.motionEnabled) return;
        if (bottom || top) lastActivity_ = now;
        if (bottomEvent || topEvent) {
            motion(bottomEvent && topEvent ? Direction::Both : bottomEvent ? Direction::Up : Direction::Down, now);
        } else if (phase_ == Phase::Idle && (bottom || top)) {
            // Enabling automation while a PIR is already held high.
            motion(bottom && top ? Direction::Both : bottom ? Direction::Up : Direction::Down, now);
        }
    }
    void motion(Direction d, uint32_t now) {
        if (!config_.motionEnabled) return;
        advance(now);
        lastActivity_ = now;
        if (phase_ == Phase::Idle || phase_ == Phase::Falling) {
            origin_ = d;
            crossed_ = false;
            beginTransition(true, d, now);
        } else if (d == Direction::Both || d != origin_) {
            crossed_ = true;
        }
    }
    void tick(uint32_t now) {
        if (config_.motionEnabled && (bottom_ || top_)) lastActivity_ = now;
        advance(now);
        if (phase_ == Phase::Holding && (!config_.motionEnabled ||
            (!(bottom_ || top_) && uint32_t(now - lastActivity_) >= config_.holdMs))) {
            Direction off = origin_;
            if (!crossed_ && origin_ != Direction::Both)
                off = origin_ == Direction::Up ? Direction::Down : Direction::Up;
            beginTransition(false, off, now);
        }
    }
    float level(size_t step) const { return levels_[step]; }
    Phase phase() const { return phase_; }
    Direction origin() const { return origin_; }
    Direction transitionDirection() const { return direction_; }
    bool crossed() const { return crossed_; }
    uint32_t remaining(uint32_t now) const {
        if (phase_ == Phase::Idle || phase_ == Phase::Falling) return 0;
        const auto age = uint32_t(now - lastActivity_);
        return age >= config_.holdMs ? 0 : config_.holdMs - age;
    }
private:
    LightSettings config_;
    Phase phase_ = Phase::Idle;
    Direction origin_ = Direction::Up, direction_ = Direction::Up;
    bool crossed_ = false, bottom_ = false, top_ = false;
    uint32_t started_ = 0, lastActivity_ = 0, duration_ = 0, delay_ = 0;
    std::array<float, hw::stepCount> levels_{}, from_{};
    void beginTransition(bool on, Direction d, uint32_t now) {
        from_ = levels_;
        started_ = now;
        direction_ = d;
        duration_ = on ? config_.fadeInMs : config_.fadeOutMs;
        delay_ = d == Direction::Both ? 0 : config_.stepDelayMs;
        phase_ = on ? Phase::Rising : Phase::Falling;
    }
    void advance(uint32_t now) {
        if (phase_ != Phase::Rising && phase_ != Phase::Falling) return;
        bool complete = true;
        const auto elapsed = uint32_t(now - started_);
        for (size_t i = 0; i < hw::stepCount; ++i) {
            const size_t order = direction_ == Direction::Down ? hw::stepCount - 1 - i : i;
            const uint32_t wait = order * delay_;
            float t = 0;
            if (elapsed >= wait) t = duration_ ? std::min(1.0f, float(elapsed - wait) / duration_) : 1.0f;
            if (t < 1) complete = false;
            t = t * t * (3 - 2 * t); // smoothstep
            const float target = phase_ == Phase::Rising ? 1.0f : 0.0f;
            levels_[i] = from_[i] + (target - from_[i]) * t;
        }
        if (complete) phase_ = phase_ == Phase::Rising ? Phase::Holding : Phase::Idle;
    }
};
