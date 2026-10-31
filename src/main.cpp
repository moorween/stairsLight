#include <Arduino.h>
#include <atomic>
#include "Events.h"
#include "LedRenderer.h"
#include "NetworkUi.h"
#include "PirFilter.h"

CoreLoop lightLoop(64, pdMS_TO_TICKS(10));
CoreLoop networkLoop(64, pdMS_TO_TICKS(10));
Action<LightSettings> applyLight(lightLoop);
Action<TestCommand> testLight(lightLoop);
BehaviorSubject<RuntimeState> runtimeState(RuntimeState{}, lightLoop);
std::atomic<bool> otaActive{false};
std::atomic<bool> otaLightReady{false};

namespace {
LightSettings initialLight;
NetworkConfig initialNetwork;

void lightTask(void*) {
    static LedRenderer leds; // RMT symbol buffer lives outside the task stack.
    MotionEngine engine;
    LightSettings settings = initialLight;
    engine.configure(settings, millis());
    pinMode(hw::pirBottomPin, INPUT_PULLDOWN);
    pinMode(hw::pirTopPin, INPUT_PULLDOWN);
    if (!leds.begin()) Serial.println("LED: RMT initialization failed");
    PirFilter bottom, top;
    int testStep = -1;
    Job stepTest;
    bool warming = hw::pirWarmupMs > 0;
    bool rawBottom = false, rawTop = false;
    bool otaFrameSent = false;
    if (warming) lightLoop.async.delay(hw::pirWarmupMs, [&] { warming = false; });
    applyLight.subscribe([&](LightSettings next) {
        next.normalize();
        if (!(next.bottom == settings.bottom)) bottom.reset(false, millis());
        if (!(next.top == settings.top)) top.reset(false, millis());
        settings = next;
        engine.configure(settings, millis());
    });
    testLight.subscribe([&](TestCommand command) {
        if (command == TestCommand::Bottom) engine.motion(Direction::Up, millis());
        if (command == TestCommand::Top) engine.motion(Direction::Down, millis());
        if (command == TestCommand::Steps) {
            stepTest.breakFn();
            testStep = 0;
            stepTest = lightLoop.async.repeat(1000, [&] {
                if (++testStep >= int(hw::stepCount)) {
                    testStep = -1;
                    stepTest.breakFn();
                }
            });
        }
        if (command == TestCommand::Stop) { stepTest.breakFn(); testStep = -1; }
    });
    // Both inputs are sampled together so simultaneous edges keep their meaning.
    lightLoop.async.repeat(5, [&] {
        const uint32_t now = millis();
        rawBottom = digitalRead(hw::pirBottomPin) == HIGH;
        rawTop = digitalRead(hw::pirTopPin) == HIGH;
        const bool logicalBottom = (rawBottom == hw::pirActiveHigh) != settings.bottom.inverted;
        const bool logicalTop = (rawTop == hw::pirActiveHigh) != settings.top.inverted;
        bool bottomEvent = false, topEvent = false;
        if (warming) {
            bottom.reset(logicalBottom, now);
            top.reset(logicalTop, now);
        } else {
            bottomEvent = bottom.update(logicalBottom, now, settings.bottom);
            topEvent = top.update(logicalTop, now, settings.top);
        }
        engine.sensors(bottom.active(), top.active(), bottomEvent, topEvent, now);
        engine.tick(now);
    });
    lightLoop.async.repeat(hw::frameMs, [&] {
        const bool ota = otaActive.load();
        if (!ota) otaFrameSent = false;
        if (!ota || !otaFrameSent) {
            const uint32_t now = millis();
            engine.tick(now);
            leds.render(settings, engine, now, ota, testStep);
            const bool shown = leds.show();
            if (ota && shown) { otaFrameSent = true; otaLightReady.store(true); }
        }
    });
    lightLoop.async.repeat(100, [&] {
        const uint32_t now = millis();
        RuntimeState state;
        state.rawBottom = rawBottom; state.rawTop = rawTop;
        state.bottom = bottom.active(); state.top = top.active();
        state.warmup = warming; state.ledReady = leds.ready();
        state.testing = testStep >= 0; state.crossed = engine.crossed();
        state.phase = engine.phase(); state.origin = engine.origin();
        state.fading = engine.transitionDirection(); state.remainingMs = engine.remaining(now);
        state.ledErrors = leds.errors();
        state.freeStack = uxTaskGetStackHighWaterMark(nullptr);
        runtimeState.next(state);
    });
    for (;;) {
        // run() also ticks Async. Yield even if the command queue stays busy.
        lightLoop.run(0);
        vTaskDelay(1);
    }
}

void networkTask(void*) {
    static NetworkUi network;
    networkLoop.run(0); // Establish the home task before subscribing.
    network.begin(initialLight, initialNetwork);
    for (;;) {
        networkLoop.run(0);
        network.tick();
        vTaskDelay(1);
    }
}
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("\nStairs Light 1.0.0: %u steps, %u LEDs\n", unsigned(hw::stepCount), unsigned(hw::ledCount));
    {
        Storage storage;
        if (storage.begin()) storage.load(initialLight, initialNetwork);
        else Serial.println("NVS unavailable, starting with defaults");
    }
    TaskHandle_t light = nullptr, network = nullptr;
    const auto a = xTaskCreatePinnedToCore(lightTask, "stairs-light", 8192, nullptr, 2, &light, 1);
    const auto b = xTaskCreatePinnedToCore(networkTask, "stairs-network", 16384, nullptr, 1, &network, 0);
    if (a != pdPASS || b != pdPASS) {
        Serial.println("FATAL: cannot create controller tasks");
        for (;;) delay(1000);
    }
}

void loop() { delay(1000); }
