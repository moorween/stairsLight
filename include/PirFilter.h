#pragma once
#include "LightSettings.h"

// Separate accepted level and edge event: cooldown never hides occupancy.
class PirFilter {
public:
    void reset(bool raw, uint32_t now) {
        candidate_ = raw;
        active_ = false;
        since_ = now;
        haveEvent_ = false;
    }
    bool update(bool raw, uint32_t now, const PirSettings& config) {
        if (raw != candidate_) { candidate_ = raw; since_ = now; }
        if (candidate_ == active_ || uint32_t(now - since_) < (candidate_ ? config.onMs : config.offMs))
            return false;
        active_ = candidate_;
        if (!active_) return false;
        if (haveEvent_ && uint32_t(now - lastEvent_) < config.cooldownMs) return false;
        haveEvent_ = true;
        lastEvent_ = now;
        return true;
    }
    bool active() const { return active_; }
private:
    bool candidate_ = false, active_ = false, haveEvent_ = false;
    uint32_t since_ = 0, lastEvent_ = 0;
};
