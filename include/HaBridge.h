#pragma once
#include <ArduinoHA.h>
#include <WiFi.h>
#include <functional>
#include "Events.h"
#include "Storage.h"

class HaBridge {
public:
    using Apply = std::function<bool(LightSettings)>;
    HaBridge(const NetworkConfig& config, LightSettings& settings, Apply apply);
    ~HaBridge();
    void tick();
    void publishSettings(bool force = false);
    void publishRuntime(const RuntimeState& state, bool force = false);
    bool connected() const { return mqtt_.isConnected(); }
    int connectionState() const { return mqtt_.getState(); }
private:
    static HaBridge* instance_;
    NetworkConfig config_; // HAMqtt stores pointers into these strings.
    LightSettings& settings_;
    Apply apply_;
    RuntimeState runtime_;
    WiFiClient client_;
    HADevice device_;
    HAMqtt mqtt_;
    HALight background_, motion_;
    HABinarySensor bottom_, top_, active_;
    HASelect effect_;
    bool started_ = false, republish_ = false;
    static void onLightState(bool value, HALight* sender);
    static void onBrightness(uint8_t value, HALight* sender);
    static void onColor(HALight::RGBColor value, HALight* sender);
    static void onEffect(int8_t value, HASelect* sender);
};
