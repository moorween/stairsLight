#include "NetworkUi.h"
#include <ESPmDNS.h>

namespace id {
enum : size_t {
    Status = 1, BgOn, BgBrightness, BgColor, Effect, Speed, Intensity,
    MotionEnabled, MotionBrightness, MotionColor, StepDelay, FadeIn, FadeOut, Hold,
    BottomOn, BottomOff, BottomCooldown, BottomInvert, TopOn, TopOff, TopCooldown, TopInvert,
    RawBottom, RawTop, Bottom, Top, Phase, Origin, Fading, Crossed, Warmup, Remaining,
    LedStatus, Queues, Heap, Stack, Testing,
    SimBottom, SimTop, TestSteps, TestStop,
    WifiStatus, Ip, Ap, Scan, WifiSelect, Ssid, WifiPass, WifiOpen, Connect,
    MqttEnabled, MqttHost, MqttPort, MqttUser, MqttPass, MqttClearPass, Discovery,
    DeviceName, MqttTest, MqttSave, MqttStatus,
    Hostname, ApPass, WebPass, OtaPass, SecuritySave,
    SaveNow, Restart, ResetChoice, ResetButton, ResetConfirm
};
}

void NetworkUi::begin(LightSettings light, NetworkConfig network) {
    light_ = light;
    net_ = draft_ = network;
    if (!storage_.begin()) status_ = "Ошибка открытия NVS: настройки не сохраняются";
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(false);
    WiFi.setHostname(net_.hostname.c_str());
    char suffix[9];
    snprintf(suffix, sizeof(suffix), "%06lX", static_cast<unsigned long>((ESP.getEfuseMac() >> 24) & 0xFFFFFF));
    apName_ = String("StairsLight-") + suffix;
    if (!net_.ssid.length()) startAp();
    else connectWifi(net_.ssid, net_.wifiPassword);
    ui_.setVersion("1.0.0");
    ui_.setPass(net_.webPassword);
    ui_.setUpdatePeriod(1000);
    ui_.config.requestTout = 10000;
    ui_.onBuild([this](sets::Builder& b) { build(b); });
    ui_.onUpdate([this](sets::Updater& u) { update(u); });
    ui_.begin(true);
    runtimeState.subscribe([this](RuntimeState state) {
        runtime_ = state;
        if (ha_) ha_->publishRuntime(state);
    }, networkLoop, Delivery::Latest);
    rebuildMqtt();
    configureOta();
    Serial.printf("Web UI: AP=%s, hostname=%s.local\n", apName_.c_str(), net_.hostname.c_str());
}

void NetworkUi::tick() {
    // Once flash writing begins, don't issue MQTT or NVS writes.
    ArduinoOTA.handle();
    if (otaActive.load()) return;
    pollWifi();
    pollScan();
    ui_.tick();
    if (restartAt_ && int32_t(millis() - restartAt_) >= 0) { saveDirty(); ESP.restart(); }
    if (mqttRebuild_) { mqttRebuild_ = false; rebuildMqtt(); }
    if (ha_ && !candidate_) ha_->tick();
    if (dirty_ && uint32_t(millis() - dirtyAt_) >= 2000) saveDirty();
}

bool NetworkUi::submit(LightSettings next) {
    next.normalize();
    if (next == light_) return true;
    if (!applyLight.call(next)) {
        message("Очередь контроллера занята. Изменение не принято");
        ui_.reload();
        return false;
    }
    light_ = next;
    dirty_ = true;
    dirtyAt_ = millis();
    if (ha_) ha_->publishSettings();
    return true;
}

void NetworkUi::message(const String& text) {
    status_ = text;
    Serial.println(text);
}

void NetworkUi::saveDirty() {
    if (!dirty_) return;
    if (storage_.saveLight(light_)) dirty_ = false;
    else { message("Ошибка сохранения настроек света в NVS"); dirtyAt_ = millis(); }
}

void NetworkUi::startAp() {
    if (apStarted_) return;
    apStarted_ = WiFi.softAP(apName_.c_str(), net_.apPassword.c_str());
    if (apStarted_) message("Точка доступа: " + apName_ + ", http://192.168.4.1");
    else message("Не удалось открыть точку доступа");
}

void NetworkUi::connectWifi(const String& ssid, const String& password) {
    WiFi.disconnect(false, false);
    WiFi.begin(ssid.c_str(), password.c_str());
    connecting_ = true;
    stableAt_ = 0;
    connectAt_ = lastAttempt_ = millis();
}

void NetworkUi::applyWifi() {
    if (scanBusy_) { message("Дождитесь завершения поиска Wi-Fi"); return; }
    if (!draft_.ssid.length() || draft_.ssid.length() > 32) {
        message("SSID должен содержать от 1 до 32 байт"); return;
    }
    String password = wifiOpen_ ? String() : wifiPassEdit_;
    if (!wifiOpen_ && !password.length() && draft_.ssid == net_.ssid) password = net_.wifiPassword;
    if (!wifiOpen_ && (password.length() < 8 || password.length() > 63)) {
        message("Введите пароль Wi-Fi (8–63 символа) или отметьте открытую сеть"); return;
    }
    startAp(); // Keep provisioning reachable when testing another network.
    candidateSsid_ = draft_.ssid;
    candidatePassword_ = password;
    candidate_ = true;
    connectWifi(candidateSsid_, candidatePassword_);
    message("Проверка подключения к Wi-Fi…");
}

void NetworkUi::pollWifi() {
    const uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED && (!candidate_ || WiFi.SSID() == candidateSsid_)) {
        disconnectedAt_ = 0;
        if (!stableAt_) stableAt_ = now;
        if (connecting_ && uint32_t(now - stableAt_) >= 2000) {
            connecting_ = false;
            if (candidate_) {
                auto next = net_;
                next.ssid = candidateSsid_; next.wifiPassword = candidatePassword_;
                if (storage_.saveNetwork(next)) {
                    net_ = next;
                    draft_.ssid = next.ssid;
                    wifiPassEdit_ = "";
                    message("Wi-Fi сохранён. IP: " + WiFi.localIP().toString());
                } else {
                    message("Wi-Fi работает, но сохранить настройки не удалось");
                }
                candidatePassword_ = "";
                candidate_ = false;
            } else message("Wi-Fi подключён. IP: " + WiFi.localIP().toString());
        }
        return;
    }
    if (!disconnectedAt_) disconnectedAt_ = now;
    if (!net_.ssid.length() || uint32_t(now - disconnectedAt_) >= 60000) startAp();
    if (connecting_ && uint32_t(now - connectAt_) >= 20000) {
        connecting_ = false;
        if (candidate_) {
            candidate_ = false;
            candidatePassword_ = "";
            message("Новый Wi-Fi недоступен. Восстанавливаю прежние настройки");
            if (net_.ssid.length()) connectWifi(net_.ssid, net_.wifiPassword);
        } else message("Wi-Fi недоступен. Повтор подключения через 30 секунд");
    }
    if (!connecting_ && net_.ssid.length() && uint32_t(now - lastAttempt_) >= 30000)
        connectWifi(net_.ssid, net_.wifiPassword);
}

void NetworkUi::scan() {
    if (scanBusy_ || candidate_) { message("Уже выполняется операция Wi-Fi"); return; }
    WiFi.scanDelete();
    const int result = WiFi.scanNetworks(true, false);
    scanBusy_ = result == WIFI_SCAN_RUNNING || result >= 0;
    message(scanBusy_ ? "Поиск сетей Wi-Fi…" : "Не удалось начать поиск Wi-Fi");
}

void NetworkUi::pollScan() {
    if (!scanBusy_) return;
    const int count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    scanBusy_ = false;
    scanned_.clear(); scanOptions_ = ""; scanIndex_ = 0;
    for (int i = 0; i < count; ++i) {
        String name = WiFi.SSID(i);
        if (!name.length()) continue;
        bool exists = false;
        for (const auto& found : scanned_) if (found == name) exists = true;
        if (exists) continue;
        scanned_.push_back(name);
        String label = name;
        label.replace(';', ',');
        if (scanOptions_.length()) scanOptions_ += ';';
        scanOptions_ += label + " (" + String(WiFi.RSSI(i)) + " dBm)";
    }
    WiFi.scanDelete();
    if (!scanned_.empty()) draft_.ssid = scanned_[0];
    else scanOptions_ = "Сети не найдены — введите SSID вручную";
    message(count < 0 ? "Ошибка поиска Wi-Fi" : "Поиск Wi-Fi завершён");
    ui_.reload();
}

NetworkConfig NetworkUi::mqttDraft() const {
    auto next = net_;
    next.mqttEnabled = draft_.mqttEnabled;
    next.mqttHost = draft_.mqttHost; next.mqttHost.trim();
    next.mqttPort = draft_.mqttPort;
    next.mqttUser = draft_.mqttUser;
    next.discoveryPrefix = draft_.discoveryPrefix; next.discoveryPrefix.trim();
    next.deviceName = draft_.deviceName; next.deviceName.trim();
    if (clearMqttPass_) next.mqttPassword = "";
    else if (mqttPassEdit_.length()) next.mqttPassword = mqttPassEdit_;
    return next;
}

bool NetworkUi::validateMqtt(const NetworkConfig& c) {
    if (c.mqttEnabled && (!c.mqttHost.length() || c.mqttHost.length() > 253 || !c.mqttPort)) {
        message("Укажите адрес MQTT и порт 1–65535"); return false;
    }
    if (!c.deviceName.length() || c.deviceName.length() > 64 || !c.discoveryPrefix.length() ||
        c.discoveryPrefix.length() > 64 || c.discoveryPrefix.indexOf('+') >= 0 || c.discoveryPrefix.indexOf('#') >= 0) {
        message("Проверьте имя устройства и префикс Discovery (без + и #)"); return false;
    }
    if (c.mqttUser.length() > 128 || c.mqttPassword.length() > 128) {
        message("Пользователь и пароль MQTT: не более 128 байт"); return false;
    }
    return true;
}

void NetworkUi::testMqtt() {
    auto c = mqttDraft();
    c.mqttEnabled = true;
    if (!validateMqtt(c)) return;
    if (WiFi.status() != WL_CONNECTED || candidate_) { message("Сначала подключите Wi-Fi"); return; }
    WiFiClient transport;
    transport.setConnectionTimeout(1500);
    PubSubClient probe(transport);
    probe.setSocketTimeout(2);
    probe.setServer(c.mqttHost.c_str(), c.mqttPort);
    const String probeId = apName_ + "-test";
    const bool ok = probe.connect(probeId.c_str(), c.mqttUser.length() ? c.mqttUser.c_str() : nullptr,
                                 c.mqttPassword.length() ? c.mqttPassword.c_str() : nullptr);
    const int result = probe.state();
    probe.disconnect();
    if (ok) message("MQTT: подключение успешно. Можно сохранить настройки");
    else if (result == 4 || result == 5) message("MQTT: ошибка пользователя/пароля или нет разрешения");
    else message("MQTT: подключение не удалось, код " + String(result));
}

void NetworkUi::saveMqtt() {
    const auto next = mqttDraft();
    if (!validateMqtt(next)) return;
    if (!storage_.saveNetwork(next)) { message("Не удалось сохранить MQTT в NVS"); return; }
    net_ = next;
    mqttPassEdit_ = ""; clearMqttPass_ = false;
    mqttRebuild_ = true;
    message("Настройки MQTT сохранены");
}

void NetworkUi::rebuildMqtt() {
    ha_.reset();
    ha_ = std::make_unique<HaBridge>(net_, light_, [this](LightSettings next) { return submit(next); });
    ha_->publishRuntime(runtime_);
}

void NetworkUi::configureOta() {
    if (otaConfigured_) ArduinoOTA.end();
    ArduinoOTA.setHostname(net_.hostname.c_str());
    ArduinoOTA.setPassword(net_.otaPassword.c_str());
    ArduinoOTA.onStart([this] {
        saveDirty();
        otaLightReady.store(false);
        otaActive.store(true);
        const auto start = millis();
        while (!otaLightReady.load() && uint32_t(millis() - start) < 1000) delay(1);
        Serial.println("OTA: receiving firmware");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int last = 101;
        const auto percent = total ? progress * 100U / total : 0;
        if (percent / 10 != last / 10) { Serial.printf("OTA: %u%%\n", percent); last = percent; }
    });
    ArduinoOTA.onEnd([] { Serial.println("OTA: complete, restarting"); });
    ArduinoOTA.onError([](ota_error_t error) {
        otaActive.store(false);
        Serial.printf("OTA error: %u\n", error);
    });
    ArduinoOTA.begin();
    MDNS.addService("http", "tcp", 80);
    otaConfigured_ = true;
}

void NetworkUi::saveSecurity() {
    auto next = net_;
    String host = draft_.hostname; host.trim(); host.toLowerCase();
    bool valid = host.length() > 0 && host.length() <= 32 && host[0] != '-' && host[host.length() - 1] != '-';
    for (size_t i = 0; i < host.length(); ++i)
        valid = valid && ((host[i] >= 'a' && host[i] <= 'z') || (host[i] >= '0' && host[i] <= '9') || host[i] == '-');
    if (!valid) { message("Имя сети: 1–32 символа a–z, 0–9, дефис внутри имени"); return; }
    for (const auto* password : {&apPassEdit_, &webPassEdit_, &otaPassEdit_}) {
        if (password->length() && (password->length() < 8 || password->length() > 63)) {
            message("Новый пароль должен содержать 8–63 символа"); return;
        }
    }
    next.hostname = host;
    if (apPassEdit_.length()) next.apPassword = apPassEdit_;
    if (webPassEdit_.length()) next.webPassword = webPassEdit_;
    if (otaPassEdit_.length()) next.otaPassword = otaPassEdit_;
    if (!storage_.saveNetwork(next)) { message("Ошибка сохранения паролей в NVS"); return; }
    net_ = next;
    saveDirty();
    message("Сохранено. Перезагрузка через 3 секунды");
    restartAt_ = millis() + 3000;
}

void NetworkUi::reset() {
    if (resetChoice_ == 1) {
        auto next = net_;
        next.ssid = next.wifiPassword = next.mqttHost = next.mqttUser = next.mqttPassword = "";
        next.mqttEnabled = false;
        if (!storage_.saveNetwork(next)) { message("Не удалось сбросить сеть"); return; }
        net_ = next;
        saveDirty();
    } else if (resetChoice_ == 2) {
        if (!storage_.clear()) { message("Ошибка сброса NVS"); return; }
        dirty_ = false;
    } else { message("Выберите, какие настройки сбросить"); return; }
    message("Сброс выполнен. Перезагрузка через 3 секунды");
    restartAt_ = millis() + 3000;
}

void NetworkUi::build(sets::Builder& b) {
    if (!b.build.granted) {
        b.beginGuest();
        b.Label("Stairs Light", "Для настройки войдите с паролем web UI");
        b.endGuest();
        return;
    }
    b.Label(id::Status, "Состояние", status_);
    auto next = light_;
    bool changed = false;
    b.beginGroup("Фоновая подсветка");
    changed |= b.Switch(id::BgOn, "Включена", &next.backgroundOn);
    changed |= b.Slider(id::BgBrightness, "Яркость", 0, 255, 1, "", &next.backgroundBrightness);
    changed |= b.Color(id::BgColor, "Цвет", &next.backgroundColor);
    changed |= b.Select(id::Effect, "Эффект", effectOptions, &next.effect);
    changed |= b.Slider(id::Speed, "Скорость эффекта", 0, 255, 1, "", &next.speed);
    changed |= b.Slider(id::Intensity, "Интенсивность эффекта", 0, 255, 1, "", &next.intensity);
    b.endGroup();
    b.beginGroup("Подсветка по движению");
    changed |= b.Switch(id::MotionEnabled, "Работа по датчикам", &next.motionEnabled);
    changed |= b.Slider(id::MotionBrightness, "Яркость", 0, 255, 1, "", &next.motionBrightness);
    changed |= b.Color(id::MotionColor, "Цвет", &next.motionColor);
    changed |= b.Number(id::StepDelay, "Интервал ступеней, мс", &next.stepDelayMs, 0, 3000);
    changed |= b.Number(id::FadeIn, "Разгорание ступени, мс", &next.fadeInMs, 0, 10000);
    changed |= b.Number(id::FadeOut, "Гашение ступени, мс", &next.fadeOutMs, 0, 10000);
    changed |= b.Number(id::Hold, "Таймаут после активности, мс", &next.holdMs, 1000, 600000);
    b.Label("Отсчёт", "Пока PIR активен, свет удерживается. Гашение добавляется к таймауту.");
    b.endGroup();
    for (int i = 0; i < 2; ++i) {
        auto& p = i ? next.top : next.bottom;
        const size_t base = i ? id::TopOn : id::BottomOn;
        b.beginGroup(i ? "Верхний PIR: программная фильтрация" : "Нижний PIR: программная фильтрация");
        changed |= b.Number(base, "Подтверждение движения, мс", &p.onMs, 0, 5000);
        changed |= b.Number(base + 1, "Подтверждение отсутствия, мс", &p.offMs, 0, 10000);
        changed |= b.Number(base + 2, "Пауза повторного события, мс", &p.cooldownMs, 0, 30000);
        changed |= b.Switch(base + 3, "Инверсия входа", &p.inverted);
        b.endGroup();
    }
    if (changed) submit(next);
    b.beginGroup("Диагностика и проверка");
    b.LED(id::RawBottom, "GPIO нижнего PIR", runtime_.rawBottom);
    b.LED(id::RawTop, "GPIO верхнего PIR", runtime_.rawTop);
    b.LED(id::Bottom, "Нижний PIR обработан", runtime_.bottom);
    b.LED(id::Top, "Верхний PIR обработан", runtime_.top);
    b.LED(id::Warmup, "Стабилизация PIR", runtime_.warmup);
    b.LED(id::Crossed, "Противоположный PIR сработал", runtime_.crossed);
    b.LED(id::Testing, "Тест ступеней", runtime_.testing);
    b.Label(id::Phase, "Анимация", phaseName(runtime_.phase));
    b.Label(id::Origin, "Начальное направление", directionName(runtime_.origin));
    b.Label(id::Fading, "Направление перехода", directionName(runtime_.fading));
    b.Label(id::Remaining, "До гашения, с", String(runtime_.remainingMs / 1000));
    b.Label(id::LedStatus, "LED", runtime_.ledReady ? "RMT готов" : "Ошибка RMT");
    b.Label(id::Queues, "Потери очередей", String(lightLoop.dropped()) + " / " + String(networkLoop.dropped()));
    b.Label(id::Heap, "Свободная память", String(ESP.getFreeHeap()));
    b.Label(id::Stack, "Запас стека света", String(runtime_.freeStack));
    b.Label("Лента", String(hw::activeLeds) + " рабочих + " + (hw::sacrificialLed ? "1 жертвенный" : "0 жертвенных"));
    if (b.Button(id::SimBottom, "Имитировать нижний PIR") && !testLight.call(TestCommand::Bottom)) message("Очередь занята");
    if (b.Button(id::SimTop, "Имитировать верхний PIR") && !testLight.call(TestCommand::Top)) message("Очередь занята");
    if (b.Button(id::TestSteps, "Проверить ступени по очереди") && !testLight.call(TestCommand::Steps)) message("Очередь занята");
    if (b.Button(id::TestStop, "Остановить тест") && !testLight.call(TestCommand::Stop)) message("Очередь занята");
    b.endGroup();
    b.beginGroup("Wi-Fi");
    b.Label(id::WifiStatus, "Подключение", WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Нет подключения");
    b.Label(id::Ip, "Адрес", WiFi.localIP().toString());
    b.Label(id::Ap, "Точка доступа", apStarted_ ? apName_ : "Выключена");
    if (b.Button(id::Scan, "Поиск сетей")) scan();
    if (b.Select(id::WifiSelect, "Найденные сети", scanOptions_, &scanIndex_)) {
        if (scanIndex_ >= 0 && size_t(scanIndex_) < scanned_.size()) draft_.ssid = scanned_[scanIndex_];
        b.reload();
    }
    b.Input(id::Ssid, "SSID (можно вручную)", &draft_.ssid);
    b.Pass(id::WifiPass, "Пароль: пусто = прежний для того же SSID", &wifiPassEdit_);
    b.Switch(id::WifiOpen, "Открытая сеть без пароля", &wifiOpen_);
    if (b.Button(id::Connect, "Проверить и сохранить Wi-Fi")) applyWifi();
    b.endGroup();
    b.beginGroup("MQTT / Home Assistant");
    b.Switch(id::MqttEnabled, "Включить MQTT", &draft_.mqttEnabled);
    b.Input(id::MqttHost, "Адрес брокера", &draft_.mqttHost);
    b.Number(id::MqttPort, "Порт", &draft_.mqttPort, 1, 65535);
    b.Input(id::MqttUser, "Пользователь", &draft_.mqttUser);
    b.Pass(id::MqttPass, "Новый пароль: пусто = оставить прежний", &mqttPassEdit_);
    b.Switch(id::MqttClearPass, "Удалить пароль MQTT", &clearMqttPass_);
    b.Input(id::Discovery, "Префикс Discovery", &draft_.discoveryPrefix);
    b.Input(id::DeviceName, "Имя устройства в HA", &draft_.deviceName);
    b.Label(id::MqttStatus, "MQTT", ha_ && ha_->connected() ? "Подключён" : net_.mqttEnabled ? "Нет подключения" : "Отключён");
    if (b.Button(id::MqttTest, "Проверить MQTT без сохранения")) testMqtt();
    if (b.Button(id::MqttSave, "Сохранить MQTT")) saveMqtt();
    b.endGroup();
    b.beginGroup("Доступ и OTA");
    b.Input(id::Hostname, "Имя устройства .local", &draft_.hostname);
    b.Pass(id::ApPass, "Новый пароль точки доступа", &apPassEdit_);
    b.Pass(id::WebPass, "Новый пароль web UI", &webPassEdit_);
    b.Pass(id::OtaPass, "Новый пароль OTA", &otaPassEdit_);
    b.Label("Пароли", "Пустые поля сохраняют текущие значения. Применение перезагрузит ESP32.");
    if (b.Button(id::SecuritySave, "Сохранить доступ и перезагрузить")) saveSecurity();
    b.endGroup();
    b.beginGroup("Обслуживание");
    if (b.Button(id::SaveNow, "Сохранить настройки света сейчас")) {
        saveDirty(); if (!dirty_) message("Настройки света сохранены");
    }
    if (b.Button(id::Restart, "Перезагрузить")) { saveDirty(); restartAt_ = millis() + 2000; }
    b.Select(id::ResetChoice, "Сброс настроек", "Не выбран;Только Wi-Fi и MQTT;Все настройки", &resetChoice_);
    if (b.Button(id::ResetButton, "Выполнить сброс…")) confirmReset_ = true;
    bool confirmed = false;
    if (b.Confirm(id::ResetConfirm, "Сбросить выбранные настройки и перезагрузить?", &confirmed) && confirmed) reset();
    b.endGroup();
}

void NetworkUi::update(sets::Updater& u) {
    u.update(id::Status, status_);
    u.update(id::BgOn, light_.backgroundOn); u.update(id::BgBrightness, light_.backgroundBrightness);
    u.update(id::BgColor, light_.backgroundColor); u.update(id::Effect, light_.effect);
    u.update(id::Speed, light_.speed); u.update(id::Intensity, light_.intensity);
    u.update(id::MotionEnabled, light_.motionEnabled); u.update(id::MotionBrightness, light_.motionBrightness);
    u.update(id::MotionColor, light_.motionColor);
    u.update(id::RawBottom, runtime_.rawBottom); u.update(id::RawTop, runtime_.rawTop);
    u.update(id::Bottom, runtime_.bottom); u.update(id::Top, runtime_.top);
    u.update(id::Warmup, runtime_.warmup); u.update(id::Crossed, runtime_.crossed); u.update(id::Testing, runtime_.testing);
    u.update(id::Phase, phaseName(runtime_.phase)); u.update(id::Origin, directionName(runtime_.origin));
    u.update(id::Fading, directionName(runtime_.fading));
    u.update(id::Remaining, String(runtime_.remainingMs / 1000));
    const String ledStatus = runtime_.ledReady ? String("RMT готов, ошибок: ") + runtime_.ledErrors : "Ошибка RMT";
    const String queues = String(lightLoop.dropped()) + " / " + String(networkLoop.dropped());
    u.update(id::LedStatus, ledStatus);
    u.update(id::Queues, queues);
    u.update(id::Heap, String(ESP.getFreeHeap())); u.update(id::Stack, String(runtime_.freeStack));
    u.update(id::WifiStatus, WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Нет подключения");
    u.update(id::Ip, WiFi.localIP().toString()); u.update(id::Ap, apStarted_ ? apName_ : "Выключена");
    const String mqttStatus = ha_ && ha_->connected() ? String("Подключён") : net_.mqttEnabled ?
        String("Нет подключения, код ") + (ha_ ? ha_->connectionState() : -1) : "Отключён";
    u.update(id::MqttStatus, mqttStatus);
    if (confirmReset_) { confirmReset_ = false; u.confirm(id::ResetConfirm); }
}
