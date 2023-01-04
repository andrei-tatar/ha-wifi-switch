#ifndef _SWITCHCOMMON_H_
#define _SWITCHCOMMON_H_

#include "io.h"
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>

typedef std::function<void(JsonVariant state)> GetJsonStateHandler;
typedef std::function<void(JsonVariant state)> JsonStateChangedHandler;

class SwitchCommon {
  Io &_io;
  String _mqttHost;
  String _mqttPassword;
  String _mqttUser;
  uint16_t _mqttPort;
  String _onlineTopic;
  String _stateTopic;
  String _stateSetTopic;
  GetJsonStateHandler _getState;
  JsonStateChangedHandler _stateChanged;
  bool _updateFromStateOnBoot;

  bool configureIo(const JsonVariantConst config);
  void configureMqtt(const JsonVariantConst config, String host);
  void publishStateInternal(bool resetSetTopic);

public:
  SwitchCommon(Io &io);

  void onGetState(GetJsonStateHandler getState);
  bool configure(const JsonVariantConst config);
  void appendStatus(JsonVariant doc) const;
  void publishState();
  void onStateChanged(JsonStateChangedHandler stateChanged);
  void setUpdateFromStateOnBoot(bool updateFromStateOnBoot);
};

#endif