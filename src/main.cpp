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
    const uint32_t boot = millis();
    engine.configure(settings, boot);
    pinMode(hw::pirBottomPin, INPUT_PULLDOWN);
    pinMode(hw::pirTopPin, INPUT_PULLDOWN);
    if (!leds.begin()) Serial.println("LED: RMT initialization failed");
    PirFilter bottom, top;
    bool testing = false;
    uint32_t testAt = 0;
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
        if (command == TestCommand::Steps) { testing = true; testAt = millis(); }
        if (command == TestCommand::Stop) testing = false;
    });
    uint32_t lastFrame = 0, lastPublish = 0;
    bool warming = hw::pirWarmupMs > 0;
    bool otaFrameSent = false;
    for (;;) {
        lightLoop.run(0);
        const uint32_t now = millis();
        const bool rawBottom = digitalRead(hw::pirBottomPin) == HIGH;
        const bool rawTop = digitalRead(hw::pirTopPin) == HIGH;
        const bool logicalBottom = (rawBottom == hw::pirActiveHigh) != settings.bottom.inverted;
        const bool logicalTop = (rawTop == hw::pirActiveHigh) != settings.top.inverted;
        if (warming && uint32_t(now - boot) >= hw::pirWarmupMs) warming = false;
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
        int testStep = -1;
        if (testing) {
            const uint32_t step = uint32_t(now - testAt) / 1000;
            if (step >= hw::stepCount) testing = false;
            else testStep = int(step);
        }
        const bool ota = otaActive.load();
        if (!ota) otaFrameSent = false;
        if ((ota && !otaFrameSent) || (!ota && uint32_t(now - lastFrame) >= hw::frameMs)) {
            lastFrame = now;
            leds.render(settings, engine, now, ota, testStep);
            const bool shown = leds.show();
            if (ota && shown) { otaFrameSent = true; otaLightReady.store(true); }
        }
        if (uint32_t(now - lastPublish) >= 100) {
            lastPublish = now;
            RuntimeState state;
            state.rawBottom = rawBottom; state.rawTop = rawTop;
            state.bottom = bottom.active(); state.top = top.active();
            state.warmup = warming; state.ledReady = leds.ready();
            state.testing = testing; state.crossed = engine.crossed();
            state.phase = engine.phase(); state.origin = engine.origin();
            state.fading = engine.transitionDirection(); state.remainingMs = engine.remaining(now);
            state.ledErrors = leds.errors();
            state.freeStack = uxTaskGetStackHighWaterMark(nullptr);
            runtimeState.next(state);
        }
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
