#include "dimmer.h"
#include "io.h"
#include "touch.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <WebServer.h>

WebServer server(80);
Io io;
Dimmer dimmer;

#define PIN_TOUCH1 33
#define PIN_TOUCH2 32
#define PIN_TOUCH3 27

#define PIN_LED1 21
#define PIN_LED2 22
#define PIN_LED3 19
#define PIN_LED_RED 23

#define PIN_ZERO 26
#define PIN_TRIAC 25

void addTouchData(ARDUINOJSON_NAMESPACE::ObjectRef parent, Touch &t) {
  parent["value"] = t.getValue();
  parent["threshold"] = t.getThreshold();
}

void onGetStatus() {
  StaticJsonDocument<1024> json;

  json["freeHeap"] = ESP.getFreeHeap();
  json["uptimeSeconds"] = millis() / 1000;
  json["on"] = dimmer.isOn();
  json["brightness"] = dimmer.getBrightness();

  auto touch = json.createNestedArray("touch");
  for (uint8_t i = 0; i < IO_CNT; i++) {
    addTouchData(touch.createNestedObject(), io.touch[i]);
  }

  String response;
  serializeJson(json, response);

  server.send(200, "application/json", response);
}

void setupDimmer();

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  ArduinoOTA.setMdnsEnabled(true).setHostname("dimmer-test").begin();

  server.on("/", onGetStatus);
  server.begin();

  setupDimmer();
}

void setupDimmer() {
  dimmer.usePins(PIN_ZERO, PIN_TRIAC).begin();

  static bool wasOff;

  io.useTouchPins(PIN_TOUCH1, PIN_TOUCH2, PIN_TOUCH3)
      .useLedPins(PIN_LED1, PIN_LED2, PIN_LED3)
      .useRedLedPin(PIN_LED_RED)
      .onTouchDown([](int8_t key) {
        wasOff = !dimmer.isOn();
        if (key == 0 || wasOff) {
          dimmer.toggle();
        }

        if (!wasOff) {
          dimmer.changeBrightness(key == 1 ? 1 : -1);
        }
      })
      .onTouchPress([](int8_t key) {
        if (key == 0 || wasOff) {
          return;
        }
        dimmer.changeBrightness(key == 1 ? 1 : -1);
      })
      .begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
}