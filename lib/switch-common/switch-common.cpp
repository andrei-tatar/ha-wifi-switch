#include "switch-common.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Ticker.h>

AsyncMqttClient mqtt;
Ticker reconnectMqtt;
const String lastWillMessage = "{\"online\":false}";
SwitchCommon::SwitchCommon(Io &io) : _io(io) {}

void SwitchCommon::appendStatus(JsonVariant doc) const {
  _io.appendStatus(doc);
  auto mqttStatus = doc.createNestedObject("mqtt");
  mqttStatus["connected"] = mqtt.connected();
}

bool SwitchCommon::configureIo(const JsonVariantConst config) {
  int8_t touch1 = config["pins"]["touch"][0] | -1;
  int8_t touch2 = config["pins"]["touch"][1] | -1;
  int8_t touch3 = config["pins"]["touch"][2] | -1;

  int8_t led1 = config["pins"]["led"][0] | -1;
  int8_t led2 = config["pins"]["led"][1] | -1;
  int8_t led3 = config["pins"]["led"][2] | -1;

  int8_t ledRed = config["pins"]["redLed"] | -1;

  bool invertRedLed = config["pins"]["redLedInvert"] | false;

  bool pinsChanged = _io.usePins(touch1, touch2, touch3, led1, led2, led3,
                                 ledRed, invertRedLed);
  _io.begin();

  return pinsChanged;
}

void SwitchCommon::configureMqtt(const JsonVariantConst config,
                                 const String host) {
  auto mdnsConfig = config["mqtt"];

  _mdnsHost = mdnsConfig["host"] | "";
  _mdnsPassword = mdnsConfig["password"] | "";
  _mdnsUser = mdnsConfig["user"] | "";
  _mdnsPort = mdnsConfig["port"] | 1883;

  if (_mdnsHost.length() && _mdnsPassword.length() && _mdnsUser.length() &&
      _mdnsPassword.length()) {

    _stateTopic = host + "/state";
    _stateSetTopic = _stateTopic + "/set";

    mqtt.setServer(_mdnsHost.c_str(), _mdnsPort)
        .setCredentials(_mdnsUser.c_str(), _mdnsPassword.c_str())
        .setWill(_stateTopic.c_str(), 1, true, lastWillMessage.c_str())
        .onMessage([this](char *topic, const char *payload,
                          AsyncMqttClientMessageProperties properties,
                          size_t len, size_t index, size_t total) {
          if (len != total) {
            return;
          }

          if (_stateChanged && _stateSetTopic == topic) {
            DynamicJsonDocument stateUpdate(500);
            if (deserializeJson(stateUpdate, payload, len) == 0) {
              _stateChanged(stateUpdate);
            }
          }
        })
        .onConnect([this, host](bool sessionPresent) {
          mqtt.subscribe(_stateSetTopic.c_str(), 1);
          publishState();
          publishVersion(host + "/version");
        });

    reconnectMqtt.attach_ms(1000, [] {
      if (WiFi.isConnected()) {
        mqtt.connect();
      } else {
        WiFi.begin();
      }
    });
  } else {
    mqtt.disconnect();
    reconnectMqtt.detach();
  }
}

void SwitchCommon::publishVersion(String topic) {
  mqtt.publish(topic.c_str(), 1, true, BUILD_VERSION);
}

void SwitchCommon::publishState() {
  DynamicJsonDocument stateJson(500);

  if (_getState) {
    _getState(stateJson);
  }
  stateJson["online"] = true;

  String state;
  serializeJson(stateJson, state);
  mqtt.publish(_stateTopic.c_str(), 1, true, state.c_str());
}

bool SwitchCommon::configure(const JsonVariantConst config) {
  bool otaEnabled = config["ota"]["enabled"] | true;
  String otaPassword = config["ota"]["password"] | "";
  String host = config["mdns"]["host"] | "ha-switch";

  bool mdnsEnabled = config["mdns"]["enabled"] | true;
  if (mdnsEnabled) {
    MDNS.begin(host.c_str());
    if (otaEnabled) {
      uint16_t otaPort = config["ota"]["port"] | 3232;
      MDNS.enableArduino(otaPort, otaPassword.length() > 0);
    }
    MDNS.addService("http", "tcp", 80);
  } else {
    MDNS.end();
  }

  if (otaEnabled) {
    if (otaPassword.length()) {
      ArduinoOTA.setPassword(otaPassword.c_str());
    }
    ArduinoOTA.setMdnsEnabled(false).begin();
  } else {
    ArduinoOTA.end();
  }

  configureMqtt(config, host);
  return configureIo(config);
}

void SwitchCommon::onGetState(GetJsonStateHandler getState) {
  _getState = getState;
}

void SwitchCommon::onStateChanged(JsonStateChangedHandler handler) {
  _stateChanged = handler;
}