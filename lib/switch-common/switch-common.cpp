#include "switch-common.h"
#include "esp32/rom/rtc.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Ticker.h>

SwitchCommon::SwitchCommon(Io &io) : _io(io) {}

void SwitchCommon::appendStatus(JsonVariant doc) const {
  _io.appendStatus(doc);
  auto mqttStatus = doc["mqtt"].to<JsonObject>();
  mqttStatus["connected"] = _mqtt.connected();
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
  }

  bool isSwitch = config["type"] == "switch";
  _io.begin(isSwitch ? 0 : MSEC(500), isSwitch ? false : true);

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
    _debugTopic = mqttPrefix + host + "/debug";

    _mqttClientId = host;
    _mqtt.setServer(_mqttHost.c_str(), _mqttPort)
        .setClientId(_mqttClientId.c_str())
        .setCredentials(_mqttUser.c_str(), _mqttPassword.c_str())
        .setWill(_onlineTopic.c_str(), 0, true, "false")
        .onMessage([this](char *topic, const char *payload,
                          AsyncMqttClientMessageProperties properties,
                          size_t len, size_t index, size_t total) {
          if (!total) {
            // skip empty messages, they are sent to reset the pending command
            return;
          }

          JsonDocument stateUpdate;
          if (deserializeJson(stateUpdate, payload, total) !=
              DeserializationError::Code::Ok) {
            char debugMessage[512];
            auto offset = snprintf(debugMessage, sizeof(debugMessage), "JSON failure: len %d, index %d, total %d: ", len, index, total);

            for (uint8_t i = 0; i < total; i++) {
              offset += snprintf(debugMessage + offset, sizeof(debugMessage) - offset, " %2x", payload[i]);
            }

            if (offset < sizeof(debugMessage)) {
              _mqtt.publish(_debugTopic.c_str(), 0, false, debugMessage);
            }
            return;
          }

          auto isRecall = _stateTopic == topic;
          if (_stateSetTopic == topic || isRecall) {
            if (isRecall) {
              unsubsribeFromState();
            }

            _stateChanged(stateUpdate, isRecall);
            _lastReceivedMessage = millis();
          }
        })
        .onConnect([this, host, mqttPrefix](bool sessionPresent) {
          _connectedAt = millis();

          if (_updateFromStateOnBoot) {
            _mqtt.subscribe(_stateTopic.c_str(), 0);
          } else {
            _mqtt.subscribe(_stateSetTopic.c_str(), 0);
            publishStateInternal();
          }
          _mqtt.publish((mqttPrefix + host + "/version").c_str(), 0, true,
                        BUILD_VERSION);
          _mqtt.publish(_onlineTopic.c_str(), 0, true, "true");

          if (_firstConnection) {
            _firstConnection = false;
            auto resetReason = rtc_get_reset_reason(0);
            char resetReasonString[20];
            snprintf(resetReasonString, 5, "%d", resetReason);
            String resetReasonTopic = mqttPrefix + host + "/reset-reason";
            _mqtt.publish(resetReasonTopic.c_str(), 0, false,
                          resetReasonString);
          }
        });

    _timer.attach_ms(SECS(1), SwitchCommon::handle, this);
  } else {
    _mqtt.disconnect();
    _timer.detach();
  }
}

void SwitchCommon::handle(SwitchCommon *instance) {
  SwitchCommon &me = *instance;
  auto now = millis();
  if (WiFi.isConnected()) {
    me._reconnectWifiSkips = 0;
    me._mqtt.connect();

    if (me._connectedAt && now > me._connectedAt + 5000) {
      me._connectedAt = 0;
      me.unsubsribeFromState();
      me.publishStateInternal();
    }

    if (++me._sendStateSkips == 300) {
      me._sendStateSkips = 0;
      me.publishStateInternal();
    }

    if (me._lastReceivedMessage &&
        me._lastReceivedMessage > me._lastStateUpdateSent &&
        now > me._lastStateUpdateSent + 1000) {
      me._lastReceivedMessage = 0;
      me.resetPendingCommand();
    }
  } else {
    me._sendStateSkips = 0;
    if (++me._reconnectWifiSkips == 60) {
      me._reconnectWifiSkips = 0;

      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      WiFi.begin();
    }
  }
}

void SwitchCommon::publishState() {
  publishStateInternal();
  resetPendingCommand();
}

void SwitchCommon::resetPendingCommand() {
  _mqtt.publish(_stateSetTopic.c_str(), 0, true);
}

void SwitchCommon::publishStateInternal() {
  _lastStateUpdateSent = millis();

  if (!_getState) {
    return;
  }

  JsonDocument stateJson;
  _getState(stateJson);
  String state;
  serializeJson(stateJson, state);
  _mqtt.publish(_stateTopic.c_str(), 0, true, state.c_str());
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

void SwitchCommon::unsubsribeFromState() {
  if (_updateFromStateOnBoot) {
    _updateFromStateOnBoot = false;
    _mqtt.unsubscribe(_stateTopic.c_str());
    _mqtt.subscribe(_stateSetTopic.c_str(), 0);
  }
}
