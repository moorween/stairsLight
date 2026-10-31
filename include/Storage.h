#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "LightSettings.h"

#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#include "Secrets.example.h"
#endif

struct NetworkConfig {
    String ssid, wifiPassword;
    bool mqttEnabled = false;
    String mqttHost;
    uint16_t mqttPort = 1883;
    String mqttUser, mqttPassword;
    String discoveryPrefix = "homeassistant";
    String deviceName = "Stairs Light";
    String hostname = "stairslight";
    String apPassword = STAIRS_AP_PASSWORD;
    String webPassword = STAIRS_WEB_PASSWORD;
    String otaPassword = STAIRS_OTA_PASSWORD;
};

class Storage {
public:
    bool begin() { return prefs_.begin("stairs", false); }
    void load(LightSettings& light, NetworkConfig& net) {
        if (prefs_.getUInt("schema", 0) == schema && prefs_.getBytesLength("light") == sizeof(light))
            prefs_.getBytes("light", &light, sizeof(light));
        light.normalize();
        NetworkRecord r{};
        if (prefs_.getBytesLength("network") != sizeof(r) ||
            prefs_.getBytes("network", &r, sizeof(r)) != sizeof(r) || r.version != schema) return;
        net.ssid = string(r.ssid); net.wifiPassword = string(r.wifiPassword);
        net.mqttEnabled = r.mqttEnabled;
        net.mqttHost = string(r.mqttHost); net.mqttPort = r.mqttPort ? r.mqttPort : 1883;
        net.mqttUser = string(r.mqttUser); net.mqttPassword = string(r.mqttPassword);
        net.discoveryPrefix = string(r.discoveryPrefix); net.deviceName = string(r.deviceName);
        net.hostname = string(r.hostname);
        if (string(r.apPassword).length() >= 8) net.apPassword = string(r.apPassword);
        if (string(r.webPassword).length() >= 8) net.webPassword = string(r.webPassword);
        if (string(r.otaPassword).length() >= 8) net.otaPassword = string(r.otaPassword);
    }
    bool saveLight(const LightSettings& light) {
        return prefs_.putBytes("light", &light, sizeof(light)) == sizeof(light) &&
               prefs_.putUInt("schema", schema) == sizeof(uint32_t);
    }
    bool saveNetwork(const NetworkConfig& n) {
        NetworkRecord r{};
        r.version = schema; r.mqttEnabled = n.mqttEnabled; r.mqttPort = n.mqttPort;
        const bool valid = copy(r.ssid, n.ssid) && copy(r.wifiPassword, n.wifiPassword) &&
            copy(r.mqttHost, n.mqttHost) && copy(r.mqttUser, n.mqttUser) && copy(r.mqttPassword, n.mqttPassword) &&
            copy(r.discoveryPrefix, n.discoveryPrefix) && copy(r.deviceName, n.deviceName) &&
            copy(r.hostname, n.hostname) && copy(r.apPassword, n.apPassword) &&
            copy(r.webPassword, n.webPassword) && copy(r.otaPassword, n.otaPassword);
        // A single NVS blob keeps SSID and password consistent if power is lost.
        return valid && prefs_.putBytes("network", &r, sizeof(r)) == sizeof(r);
    }
    bool clear() { return prefs_.clear(); }
private:
    static constexpr uint32_t schema = 1;
    struct NetworkRecord {
        uint32_t version;
        bool mqttEnabled;
        uint16_t mqttPort;
        char ssid[33], wifiPassword[64], mqttHost[254], mqttUser[129], mqttPassword[129];
        char discoveryPrefix[65], deviceName[65], hostname[33];
        char apPassword[64], webPassword[64], otaPassword[64];
    };
    template<size_t N> static bool copy(char (&out)[N], const String& value) {
        if (value.length() >= N) return false;
        value.toCharArray(out, N);
        return true;
    }
    template<size_t N> static String string(char (&value)[N]) {
        value[N - 1] = '\0';
        return String(value);
    }
    Preferences prefs_;
};
