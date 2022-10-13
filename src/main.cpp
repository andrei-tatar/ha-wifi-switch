#include "io.h"
#include "switch-blinds.h"
#include "switch-common.h"
#include "switch-dimmer.h"
#include "switch-onoff.h"
#include "util.h"
#include "web.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <AsyncJson.h>
#include <Preferences.h>

Io io;
SwitchCommon switchCommon(io);
SwitchDimmer switchDimmer(io);
SwitchOnOff switchOnOff(io);
SwitchBlinds switchBlinds(io);

Preferences preferences;
Ticker reboot;
Web web;
String type;

void applyConfiguration(bool init) {
  DynamicJsonDocument config(2048);
  if (preferences.isKey(CONFIG_KEY)) {
    deserializeJson(config, preferences.getString(CONFIG_KEY));
  }

  auto needsReboot = switchCommon.configure(config);

  String configType = config["type"] | "undefined";
  if (configType == "dimmer") {
    needsReboot |= switchDimmer.configure(config.getMember("dimmer"));
  } else if (configType == "switch") {
    needsReboot |= switchOnOff.configure(config.getMember("switch"));
  } else if (configType == "blinds") {
    needsReboot |= switchBlinds.configure(config.getMember("blinds"));
  }

  type = configType;

  if (!init /*&& needsReboot*/) {
    reboot.once_ms(1500, []() { esp_restart(); });
  }
}

void setup() {
  preferences.begin("ha-switch");
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  applyConfiguration(true);
  web.onAppendStatus([](JsonVariant doc) {
    doc["type"] = type;
    switchCommon.appendStatus(doc);

    auto state = doc.createNestedObject("state");
    switchDimmer.appendState(state);
    switchOnOff.appendState(state);
    switchBlinds.appendState(state);
  });
  switchCommon.onGetState([](JsonVariant state) {
    switchDimmer.appendState(state);
    switchOnOff.appendState(state);
    switchBlinds.appendState(state);
  });
  auto publishState = std::bind(&SwitchCommon::publishState, switchCommon);
  switchDimmer.onStateChanged(publishState);
  switchOnOff.onStateChanged(publishState);
  switchBlinds.onStateChanged(publishState);
  switchCommon.onStateChanged([](JsonVariantConst state) {
    switchDimmer.updateState(state);
    switchOnOff.updateState(state);
    switchBlinds.updateState(state);
  });
  web.onReadConfig([] {
    return preferences.isKey(CONFIG_KEY) ? preferences.getString(CONFIG_KEY)
                                         : "{}";
  });
  web.onSetConfig([](String config) {
    preferences.putString(CONFIG_KEY, config);
    applyConfiguration(false);
  });
  web.begin(type);
}

void loop() { ArduinoOTA.handle(); }