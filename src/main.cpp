#include "io.h"
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

Preferences preferences;
Ticker reboot;
Web web;
String type = "undefined";

void applyConfiguration(bool init) {
  DynamicJsonDocument config(2048);
  if (preferences.isKey(CONFIG_KEY)) {
    deserializeJson(config, preferences.getString(CONFIG_KEY));
  }

  auto needsReboot = switchCommon.configure(config);

  String configType = config["type"];
  if (configType == "dimmer") {
    needsReboot |= switchDimmer.configure(config.getMember("dimmer"));
  } else if (configType == "switch") {
    needsReboot |= switchOnOff.configure(config.getMember("switch"));
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
    switchDimmer.appendStatus(doc);
    switchOnOff.appendStatus(doc);
  });
  switchCommon.onGetState([](JsonVariant doc) {
    switchDimmer.appendState(doc);
    switchOnOff.appendState(doc);
  });
  switchDimmer.onStateChanged([] { switchCommon.publishState(); });
  switchOnOff.onStateChanged([] { switchCommon.publishState(); });
  switchCommon.onStateChanged([](JsonVariantConst state) {
    switchDimmer.updateState(state);
    switchOnOff.updateState(state);
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