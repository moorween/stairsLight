#pragma once
#include <SettingsGyver.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <memory>
#include <vector>
#include <atomic>
#include "Events.h"
#include "Storage.h"
#include "HaBridge.h"

extern std::atomic<bool> otaActive;
extern std::atomic<bool> otaLightReady;

class NetworkUi {
public:
    void begin(LightSettings light, NetworkConfig network);
    void tick();
private:
    SettingsGyver ui_{"Stairs Light"};
    Storage storage_;
    LightSettings light_;
    NetworkConfig net_, draft_;
    RuntimeState runtime_;
    std::unique_ptr<HaBridge> ha_;
    bool dirty_ = false, apStarted_ = false, candidate_ = false, connecting_ = false;
    bool scanBusy_ = false, mqttRebuild_ = false, wifiOpen_ = false, clearMqttPass_ = false;
    bool otaConfigured_ = false;
    bool confirmReset_ = false;
    uint32_t connectAt_ = 0, lastAttempt_ = 0, disconnectedAt_ = 0;
    uint32_t stableAt_ = 0;
    Job saveJob_, restartJob_;
    String apName_, candidateSsid_, candidatePassword_;
    String wifiPassEdit_, mqttPassEdit_, apPassEdit_, webPassEdit_, otaPassEdit_;
    String status_ = "Готово", scanOptions_ = "Сначала нажмите Поиск";
    int scanIndex_ = 0;
    std::vector<String> scanned_;
    uint8_t resetChoice_ = 0;
    void build(sets::Builder& b);
    void update(sets::Updater& u);
    bool submit(LightSettings next);
    void startAp();
    void connectWifi(const String& ssid, const String& password);
    void applyWifi();
    void pollWifi();
    void pollScan();
    void scan();
    NetworkConfig mqttDraft() const;
    bool validateMqtt(const NetworkConfig& cfg);
    void testMqtt();
    void saveMqtt();
    void rebuildMqtt();
    void configureOta();
    void saveSecurity();
    void saveDirty();
    void scheduleSave();
    void scheduleRestart(int delayMs);
    void reset();
    void message(const String& message);
};
