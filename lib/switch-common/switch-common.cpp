#include "switch-common.h"
#include "esp32/rom/rtc.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Ticker.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#define FIRMWARE_HEALTHY_TIME SECS(30)
#define FIRMWARE_ROLLBACK_TIME SECS(300)

// Stops the framework from accepting a new firmware as soon as it boots, so that
// checkNewFirmware() can decide instead.
extern "C" bool verifyRollbackLater() { return true; }

static const char *firmwareState() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK) {
    return "none"; // never flashed over the network
  }

  switch (state) {
  case ESP_OTA_IMG_VALID:
    return "valid";
  case ESP_OTA_IMG_PENDING_VERIFY:
    return "pending";
  case ESP_OTA_IMG_NEW:
    return "new";
  default:
    return "undefined";
  }
}

SwitchCommon::SwitchCommon(Io &io) : _io(io) {}

void SwitchCommon::appendStatus(JsonVariant doc) {
  _io.appendStatus(doc);
  doc["firmwareState"] = firmwareState();
  auto mqttStatus = doc["mqtt"].to<JsonObject>();
  mqttStatus["connected"] = _mqtt.connected();
}

void SwitchCommon::startFirmwareCheck() {
  esp_ota_img_states_t state;
  _firmwarePending =
      esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY;
}

// A new firmware is kept once it stays connected for FIRMWARE_HEALTHY_TIME. If it
// isn't by FIRMWARE_ROLLBACK_TIME, this switches back to the previous firmware. The
// bootloader does the same if the device resets before the firmware is kept.
//
// Called from loop() rather than a Ticker: ArduinoOTA.handle() blocks loop() during
// an upload, so the rollback can't interrupt a fix that's being flashed.
void SwitchCommon::checkNewFirmware() {
  if (!_firmwarePending) {
    return;
  }

  uint32_t now = millis();
  bool healthy = _mqttUri.length() ? _mqtt.connected() : WiFi.isConnected();
  if (!healthy) {
    _firmwareHealthySince = 0;
  } else if (!_firmwareHealthySince) {
    _firmwareHealthySince = now;
  }

  if (_firmwareHealthySince && now - _firmwareHealthySince >= FIRMWARE_HEALTHY_TIME) {
    _firmwarePending = false;
    esp_ota_mark_app_valid_cancel_rollback();
  } else if (!_firmwareHealthySince && now >= FIRMWARE_ROLLBACK_TIME) {
    _firmwarePending = false; // the call only returns if there's nothing to go back to
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
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
  _io.begin(isSwitch ? 0 : MSEC(150), isSwitch ? false : true);

  return pinsChanged;
}

bool SwitchCommon::configureMqtt(const JsonVariantConst config,
                                 const String host) {
  auto mqttConfig = config["mqtt"];

  String mqttHost = mqttConfig["host"] | "";
  String mqttPassword = mqttConfig["password"] | "";
  String mqttUser = mqttConfig["user"] | "";
  uint16_t mqttPort = mqttConfig["port"] | 1883;
  String mqttPrefix = mqttConfig["prefix"] | "ha-switch";
  mqttPrefix += "/";

  // The client only reads its settings when it starts, so changes need a reboot.
  String mqttSettings = mqttHost + "\n" + String(mqttPort) + "\n" + mqttUser +
                        "\n" + mqttPassword + "\n" + mqttPrefix;
  bool isFirstCall = _mqttSettings.isEmpty();
  bool settingsChanged = mqttSettings != _mqttSettings;
  _mqttSettings = mqttSettings;

  // Set up the client only once. It keeps every callback it's given, so repeating
  // this would run each command again, and the topics are in use by other tasks.
  if (!isFirstCall) {
    return settingsChanged;
  }

  if (mqttHost.length() && mqttPassword.length() && mqttUser.length()) {
    _mqttPassword = mqttPassword;
    _mqttUser = mqttUser;

    _onlineTopic = mqttPrefix + host + "/online";
    _stateTopic = mqttPrefix + host + "/state";
    _stateSetTopic = _stateTopic + "/set";
    _debugTopic = mqttPrefix + host + "/debug";

    _mqttUri = "mqtt://" + mqttHost + ":" + String(mqttPort);
    _mqttClientId = host;

    _mqtt.setServer(_mqttUri.c_str())
        .setClientId(_mqttClientId.c_str())
        .setCredentials(_mqttUser.c_str(), _mqttPassword.c_str())
        .setWill(_onlineTopic.c_str(), 0, true, "false")
        .onMessage([this](char *topic, const char *payload, int retain, int qos, bool dup) {
          auto total = strlen(payload);
          if (!total) {
            return;
          }

          JsonDocument stateUpdate;
          auto error = deserializeJson(stateUpdate, payload);
          if (error != DeserializationError::Code::Ok) {
            char debugMsg[512];
            auto offset = snprintf(debugMsg, sizeof(debugMsg), "json err %d,tot %u:", error.code(), (unsigned)total);

            // long payloads are cut off: each byte needs 2 digits, plus the terminator
            for (size_t i = 0; i < total && offset + 3 <= (int)sizeof(debugMsg); i++) {
              offset += snprintf(debugMsg + offset, sizeof(debugMsg) - offset, "%02x", (uint8_t)payload[i]);
            }

            _mqtt.publish(_debugTopic.c_str(), 0, false, debugMsg);
            return;
          }

          auto isRecall = _stateTopic == topic;
          if (_stateSetTopic == topic || isRecall) {
            if (isRecall) {
              unsubsribeFromState();
            }

            if (stateUpdate["suspendInputs"].is<bool>()) {
              _io.setSuspendInputs(stateUpdate["suspendInputs"]);
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
          _mqtt.publish((mqttPrefix + host + "/version").c_str(), 0, false,
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
  }

  return settingsChanged;
}

void SwitchCommon::handle(SwitchCommon *instance) {
  SwitchCommon &me = *instance;
  auto now = millis();
  if (WiFi.isConnected()) {
    me._reconnectWifiSkips = 0;
    // Start once and let esp-mqtt reconnect by itself. Calling connect() again
    // waits for its API lock, which is held during connection attempts, and that
    // would stall every Ticker while the broker is unreachable.
    if (!me._mqttStarted) {
      me._mqttStarted = true;
      me._mqtt.connect();
    }

    if (me._connectedAt && me._mqtt.connected() &&
        now > me._connectedAt + 5000) {
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
    if (++me._reconnectWifiSkips == 6) {
      me._reconnectWifiSkips = 0;

      WiFi.mode(WIFI_MODE_STA);
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

  JsonDocument stateJson;
  if (_getState) {
    _getState(stateJson);
  }
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

  bool needsReboot = configureMqtt(config, host);
  needsReboot |= configureIo(config);
  return needsReboot;
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
