#include "dimmer.h"
#include "io.h"
#include "util.h"
#include "web.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

Io io;
Dimmer dimmer;
Preferences preferences;
Web web(preferences, dimmer, io);

void setupCommon(JsonDocument &config) {
  bool mdnsEnabled = config["mdns"]["enabled"] | true;
  const char *mdnsHostname = config["mdns"]["host"] | "dimmer-test";

  ArduinoOTA.setMdnsEnabled(mdnsEnabled).setHostname(mdnsHostname).begin();

  int8_t touch1 = config["pins"]["touch"][0] | -1;
  int8_t touch2 = config["pins"]["touch"][1] | -1;
  int8_t touch3 = config["pins"]["touch"][2] | -1;

  int8_t led1 = config["pins"]["led"][0] | -1;
  int8_t led2 = config["pins"]["led"][1] | -1;
  int8_t led3 = config["pins"]["led"][2] | -1;

  int8_t ledRed = config["pins"]["redLed"] | -1;

  io.useTouchPins(touch1, touch2, touch3)
      .useLedPins(led1, led2, led3)
      .useRedLedPin(ledRed)
      .begin();
}

void setupDimmer(const JsonVariantConst &dimmerConfig) {
  if (!dimmer.isInitialized()) {
    static bool wasOff;

    int8_t zero = dimmerConfig["pins"]["zero"] | -1;
    int8_t triac = dimmerConfig["pins"]["triac"] | -1;

    dimmer.usePins(zero, triac);
    dimmer.begin();

    io.onTouchDown([](int8_t key) {
      wasOff = !dimmer.isOn();
      if (!key || wasOff) {
        dimmer.toggle();
      } else {
        dimmer.changeBrightness(key == 1 ? 1 : -1);
      }
    });
    io.onTouchPress([](int8_t key) {
      if (key == 0 || wasOff) {
        return;
      }
      dimmer.changeBrightness(key == 1 ? 1 : -1);
    });
  }

  if (dimmerConfig.containsKey("curve")) {
    uint16_t curve[100];
    for (uint8_t i = 0; i < 100; i++) {
      curve[i] = dimmerConfig["curve"].getElement(i);
    }
    dimmer.setBrightnessCurve(curve);
  }

  uint8_t min = dimmerConfig["min"] | 1;
  uint8_t max = dimmerConfig["max"] | 100;
  dimmer.setMinMax(min, max);

  auto onLevels = dimmerConfig["levels"]["on"];
  auto offLevels = dimmerConfig["levels"]["off"];

  static uint8_t onBlueLevel = onLevels["blue"] | 20;
  static uint8_t onBlueTouchLevel = onLevels["touch"] | 255;
  static uint8_t onRedLevel = onLevels["red"] | 0;

  static uint8_t offBlueLevel = offLevels["blue"] | 0;
  static uint8_t offBlueTouchLevel = offLevels["touch"] | 255;
  static uint8_t offRedLevel = offLevels["red"] | 20;

  io.setLedLevels(offBlueLevel, offBlueTouchLevel, offRedLevel);
  dimmer.onStateChanged([](bool on, uint8_t brightness) {
    if (on) {
      io.setLedLevels(onBlueLevel, onBlueTouchLevel, onRedLevel);
    } else {
      io.setLedLevels(offBlueLevel, offBlueTouchLevel, offRedLevel);
    }
  });
}

String readConfigAndInit() {
  DynamicJsonDocument config(2048);
  if (preferences.isKey(CONFIG_KEY)) {
    deserializeJson(config, preferences.getString(CONFIG_KEY));
  }

  setupCommon(config);

  String type = config["type"];
  if (type == "dimmer") {
    setupDimmer(config.getMember("dimmer"));
  }

  return type;
}

void setup() {
  preferences.begin("ha-switch");
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  auto type = readConfigAndInit();
  web.begin(type);
}

void loop() { ArduinoOTA.handle(); }