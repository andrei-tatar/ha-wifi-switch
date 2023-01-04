#include "switch-common.h"
#include "esp32/rom/rtc.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Ticker.h>

AsyncMqttClient mqtt;
Ticker reconnectMqtt;
int reconnectWifiSkips = 0;
bool firstConnection = true;

SwitchCommon::SwitchCommon(Io &io) : _io(io) {}

void SwitchCommon::appendStatus(JsonVariant doc) const {
  _io.appendStatus(doc);
  auto mqttStatus = doc.createNestedObject("mqtt");
  mqttStatus["connected"] = mqtt.connected();
}

bool SwitchCommon::configureIo(const JsonVariantConst config) {
  auto pinsConfig = config["pins"];

  auto pinsLedConfig = pinsConfig["led"];
  int8_t led1 = pinsLedConfig[0] | -1;
  int8_t led2 = pinsLedConfig[1] | -1;
  int8_t led3 = pinsLedConfig[2] | -1;
  int8_t ledRed = pinsConfig["redLed"] | -1;
  bool invertRedLed = pinsConfig["redLedInvert"] | false;
  bool pinsChanged = _io.useLedPins(led1, led2, led3, ledRed, invertRedLed);

  auto pinsQtConfig = pinsConfig["qt"];
  auto inputPinsConfig = pinsConfig["input"];
  auto pinsTouchConfig = pinsConfig["touch"];

  if (!pinsQtConfig.isNull()) {
    int8_t qtSda = pinsQtConfig["sda"] | -1;
    int8_t qtScl = pinsQtConfig["scl"] | -1;
    int8_t qtCh1 = pinsQtConfig["ch"][0] | -1;
    int8_t qtCh2 = pinsQtConfig["ch"][1] | -1;
    int8_t qtCh3 = pinsQtConfig["ch"][2] | -1;
    pinsChanged |= _io.useQtTouch(qtSda, qtScl, qtCh1, qtCh2, qtCh3);
  } else if (!inputPinsConfig.isNull()) {
    int8_t input1 = inputPinsConfig[0] | -1;
    int8_t input2 = inputPinsConfig[1] | -1;
    int8_t input3 = inputPinsConfig[2] | -1;
    pinsChanged |= _io.useInputPins(input1, input2, input3);
  } else if (!pinsTouchConfig.isNull()) {
    int8_t touch1 = pinsTouchConfig[0] | -1;
    int8_t touch2 = pinsTouchConfig[1] | -1;
    int8_t touch3 = pinsTouchConfig[2] | -1;
    pinsChanged |= _io.useTouchPins(touch1, touch2, touch3);
  }
  _io.begin();

  return pinsChanged;
}

void SwitchCommon::configureMqtt(const JsonVariantConst config,
                                 const String host) {
  auto mqttConfig = config["mqtt"];

  _mqttHost = mqttConfig["host"] | "";
  _mqttPassword = mqttConfig["password"] | "";
  _mqttUser = mqttConfig["user"] | "";
  _mqttPort = mqttConfig["port"] | 1883;
  String mqttPrefix = mqttConfig["prefix"] | "ha-switch";
  mqttPrefix += "/";

  if (_mqttHost.length() && _mqttPassword.length() && _mqttUser.length() &&
      _mqttPassword.length()) {

    _onlineTopic = mqttPrefix + host + "/online";
    _stateTopic = mqttPrefix + host + "/state";
    _stateSetTopic = _stateTopic + "/set";

    mqtt.setServer(_mqttHost.c_str(), _mqttPort)
        .setCredentials(_mqttUser.c_str(), _mqttPassword.c_str())
        .setWill(_onlineTopic.c_str(), 0, true, "false")
        .onMessage([this](char *topic, const char *payload,
                          AsyncMqttClientMessageProperties properties,
                          size_t len, size_t index, size_t total) {
          if (len != total) {
            return;
          }

          if (_stateSetTopic == topic || _stateTopic == topic) {
            if (_updateFromStateOnBoot) {
              _updateFromStateOnBoot = false;
              mqtt.unsubscribe(_stateTopic.c_str());
              mqtt.subscribe(_stateSetTopic.c_str(), 0);
            }

            DynamicJsonDocument stateUpdate(500);
            if (deserializeJson(stateUpdate, payload, len) ==
                DeserializationError::Code::Ok) {
              _stateChanged(stateUpdate);
            }
          }
        })
        .onConnect([this, host, mqttPrefix](bool sessionPresent) {
          if (_updateFromStateOnBoot) {
            mqtt.subscribe(_stateTopic.c_str(), 0);
          } else {
            mqtt.subscribe(_stateSetTopic.c_str(), 0);
            publishStateInternal(false);
          }
          mqtt.publish((mqttPrefix + host + "/version").c_str(), 0, true,
                       BUILD_VERSION);
          mqtt.publish(_onlineTopic.c_str(), 0, true, "true");

          if (firstConnection) {
            auto resetReason = rtc_get_reset_reason(0);
            char resetReasonString[20];
            snprintf(resetReasonString, 5, "%d", resetReason);
            String resetReasonTopic = mqttPrefix + host + "/reset-reason";
            mqtt.publish(resetReasonTopic.c_str(), 0, false, resetReasonString);
          }
        });

    reconnectMqtt.attach_ms(5000, [] {
      if (WiFi.isConnected()) {
        reconnectWifiSkips = 0;
        mqtt.connect();
      } else {
        if (++reconnectWifiSkips == 12) {
          reconnectWifiSkips = 0;

          WiFi.mode(WIFI_STA);
          WiFi.disconnect();
          WiFi.begin();
        }
      }
    });
  } else {
    mqtt.disconnect();
    reconnectMqtt.detach();
  }
}

void SwitchCommon::publishStateInternal(bool resetSetTopic) {
  if (!_getState) {
    return;
  }

  if (_updateFromStateOnBoot) {
    _updateFromStateOnBoot = false;
    mqtt.unsubscribe(_stateTopic.c_str());
    mqtt.subscribe(_stateSetTopic.c_str(), 0);
  } else if (resetSetTopic) {
    mqtt.publish(_stateSetTopic.c_str(), 0, true); // reset
  }

  DynamicJsonDocument stateJson(500);
  _getState(stateJson);
  String state;
  serializeJson(stateJson, state);
  mqtt.publish(_stateTopic.c_str(), 0, true, state.c_str());
}

void SwitchCommon::publishState() { publishStateInternal(true); }

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

void SwitchCommon::setUpdateFromStateOnBoot(bool updateFromStateOnBoot) {
  _updateFromStateOnBoot = updateFromStateOnBoot;
}
