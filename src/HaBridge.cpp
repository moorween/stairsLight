#include "HaBridge.h"

HaBridge* HaBridge::instance_ = nullptr;

HaBridge::HaBridge(const NetworkConfig& config, LightSettings& settings, Apply apply)
    : config_(config), settings_(settings), apply_(std::move(apply)), mqtt_(client_, device_, 6),
      background_("background", HALight::BrightnessFeature | HALight::RGBFeature),
      motion_("motion_light", HALight::BrightnessFeature | HALight::RGBFeature),
      bottom_("pir_bottom"), top_("pir_top"), active_("animation_active"), effect_("background_effect") {
    instance_ = this;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    device_.setUniqueId(mac, sizeof(mac));
    device_.setName(config_.deviceName.c_str());
    device_.setManufacturer("moorween");
    device_.setModel("ESP32 Stairs Light");
    device_.setSoftwareVersion("1.0.0");
    device_.enableSharedAvailability();
    device_.enableLastWill();
    background_.setName("Background");
    motion_.setName("Motion lighting enabled");
    bottom_.setName("Bottom motion"); bottom_.setDeviceClass("motion");
    top_.setName("Top motion"); top_.setDeviceClass("motion");
    active_.setName("Animation active");
    effect_.setName("Background effect");
    effect_.setOptions(effectOptions);
    for (auto* light : {&background_, &motion_}) {
        light->onStateCommand(onLightState);
        light->onBrightnessCommand(onBrightness);
        light->onRGBColorCommand(onColor);
    }
    effect_.onCommand(onEffect);
    mqtt_.setDiscoveryPrefix(config_.discoveryPrefix.c_str());
    mqtt_.setBufferSize(1536);
    mqtt_.onConnected([] { if (instance_) instance_->republish_ = true; });
    client_.setConnectionTimeout(1500);
    if (config_.mqttEnabled && config_.mqttHost.length())
        started_ = mqtt_.begin(config_.mqttHost.c_str(), config_.mqttPort,
                              config_.mqttUser.length() ? config_.mqttUser.c_str() : nullptr,
                              config_.mqttPassword.length() ? config_.mqttPassword.c_str() : nullptr);
    publishSettings();
}

HaBridge::~HaBridge() {
    if (mqtt_.isConnected()) device_.setAvailability(false);
    mqtt_.disconnect();
    instance_ = nullptr;
}

void HaBridge::tick() {
    if (!started_ || WiFi.status() != WL_CONNECTED) return;
    mqtt_.loop();
    // Discovery and subscriptions must finish before reporting the complete state.
    if (republish_ && mqtt_.isConnected()) {
        republish_ = false;
        publishSettings(true);
        publishRuntime(runtime_, true);
    }
}

void HaBridge::publishSettings(bool force) {
    background_.setState(settings_.backgroundOn, force);
    background_.setBrightness(settings_.backgroundBrightness, force);
    auto color = settings_.backgroundColor;
    background_.setRGBColor({uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color)}, force);
    motion_.setState(settings_.motionEnabled, force);
    motion_.setBrightness(settings_.motionBrightness, force);
    color = settings_.motionColor;
    motion_.setRGBColor({uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color)}, force);
    effect_.setState(settings_.effect, force);
}

void HaBridge::publishRuntime(const RuntimeState& state, bool force) {
    runtime_ = state;
    bottom_.setState(state.bottom, force);
    top_.setState(state.top, force);
    active_.setState(state.phase != Phase::Idle, force);
}

void HaBridge::onLightState(bool value, HALight* sender) {
    auto& self = *instance_;
    auto next = self.settings_;
    if (sender == &self.background_) next.backgroundOn = value;
    else next.motionEnabled = value;
    self.apply_(next);
    self.publishSettings(true);
}
void HaBridge::onBrightness(uint8_t value, HALight* sender) {
    auto& self = *instance_;
    auto next = self.settings_;
    if (sender == &self.background_) next.backgroundBrightness = value;
    else next.motionBrightness = value;
    self.apply_(next);
    self.publishSettings(true);
}
void HaBridge::onColor(HALight::RGBColor value, HALight* sender) {
    auto& self = *instance_;
    auto next = self.settings_;
    const uint32_t color = uint32_t(value.red) << 16 | uint32_t(value.green) << 8 | value.blue;
    if (sender == &self.background_) next.backgroundColor = color;
    else next.motionColor = color;
    self.apply_(next);
    self.publishSettings(true);
}
void HaBridge::onEffect(int8_t value, HASelect*) {
    auto& self = *instance_;
    if (value < 0 || value > 5) return;
    auto next = self.settings_;
    next.effect = value;
    self.apply_(next);
    self.publishSettings(true);
}
