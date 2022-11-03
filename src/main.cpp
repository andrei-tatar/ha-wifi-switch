#include "configuration.h"
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

Io io;
SwitchCommon switchCommon(io);
SwitchDimmer switchDimmer(io);
SwitchOnOff switchOnOff(io);
SwitchBlinds switchBlinds(io);

Ticker reboot;
Web web;
String type;
Configuration configuration;

void applyConfiguration(bool init) {
  DynamicJsonDocument config(5000);
  configuration.read(config);
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
  configuration.begin();

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
  web.onReadConfig([] { return configuration.readRaw(); });
  web.onSetConfig([](String config) {
    configuration.update(config);
    applyConfiguration(false);
  });
  web.begin(type);
}

void loop() { ArduinoOTA.handle(); }