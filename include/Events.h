#pragma once
#include <Subject.h>
#include "LightSettings.h"
#include "MotionEngine.h"
#include <tuple>

struct RuntimeState {
    bool rawBottom = false, rawTop = false, bottom = false, top = false;
    bool warmup = true, ledReady = false, crossed = false, testing = false;
    Phase phase = Phase::Idle;
    Direction origin = Direction::Up, fading = Direction::Up;
    uint32_t remainingMs = 0, ledErrors = 0;
    uint32_t freeStack = 0;
    bool operator==(const RuntimeState& o) const {
        return std::tie(rawBottom, rawTop, bottom, top, warmup, ledReady, crossed,
                        testing, phase, origin, fading, remainingMs, ledErrors, freeStack) ==
               std::tie(o.rawBottom, o.rawTop, o.bottom, o.top, o.warmup, o.ledReady,
                        o.crossed, o.testing, o.phase, o.origin, o.fading, o.remainingMs, o.ledErrors, o.freeStack);
    }
};

enum class TestCommand : uint8_t { Bottom, Top, Steps, Stop };

extern CoreLoop lightLoop;
extern CoreLoop networkLoop;
extern Action<LightSettings> applyLight;
extern Action<TestCommand> testLight;
extern BehaviorSubject<RuntimeState> runtimeState;
