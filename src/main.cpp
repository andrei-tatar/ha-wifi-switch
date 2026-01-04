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
#include <WiFi.h>

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
  auto config = configuration.read();

  static String lastHostName;
  String hostName = (*config)["mdns"]["host"] | "";
  bool needsReboot = false;

  if (init) {
    lastHostName = hostName;
    if (hostName.length()) {
      WiFi.setHostname(hostName.c_str());
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.begin();

  } else {
    needsReboot |= lastHostName != hostName;
  }

  needsReboot |= switchCommon.configure(*config);
  String configType = (*config)["type"] | "undefined";

  if (configType == "dimmer") {
    needsReboot |= switchDimmer.configure((*config)["dimmer"]);
  } else if (configType == "switch") {
    needsReboot |= switchOnOff.configure((*config)["switch"]);
  } else if (configType == "blinds") {
    needsReboot |= switchBlinds.configure((*config)["blinds"]);
  }

  needsReboot |= type != configType;
  type = configType;

  if (!init && needsReboot) {
    reboot.once_ms(1500, []() { ESP.restart(); });
  }

  delete config;
}

void setup() {
  setCpuFrequencyMhz(80);
  configuration.begin();
  applyConfiguration(true);
  web.onAppendStatus([](JsonVariant doc) {
    doc["type"] = type;
    switchCommon.appendStatus(doc);

    auto state = doc["state"].to<JsonObject>();
    switchDimmer.appendState(state);
    switchOnOff.appendState(state);
    switchBlinds.appendState(state);
  });
  switchCommon.onGetState([](JsonVariant state) {
    switchDimmer.appendState(state);
    switchOnOff.appendState(state);
    switchBlinds.appendState(state);
  });
  switchDimmer.onStateChanged([] { switchCommon.publishState(); });
  switchOnOff.onStateChanged([] { switchCommon.publishState(); });
  switchBlinds.onStateChanged([] { switchCommon.publishState(); });
  switchCommon.onStateChanged(
      [](JsonVariantConst state, bool isFromStoredState) {
        switchDimmer.updateState(state, isFromStoredState);
        switchOnOff.updateState(state, isFromStoredState);
        switchBlinds.updateState(state, isFromStoredState);
      });
  web.onReadConfig([] { return configuration.read(); });
  web.onSetConfig([](String config) {
    configuration.update(config);
    applyConfiguration(false);
  });
  web.begin(type);
}

void loop() { ArduinoOTA.handle(); }