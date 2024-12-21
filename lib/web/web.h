#ifndef _WEB_H_
#define _WEB_H_

#include "io.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Ticker.h>

typedef std::function<JsonDocument *()> ReadConfigHandler;
typedef std::function<void(String config)> SetConfigHandler;
typedef std::function<void(JsonVariant doc)> AppendStatusHandler;

class Web {
private:
  AsyncWebServer _server;
  ReadConfigHandler _readConfig;
  SetConfigHandler _setConfig;
  AppendStatusHandler _appendStatus;
  String _type;
  Ticker _rebootTicker;

  void getConfig(AsyncWebServerRequest *req);
  void getStatus(AsyncWebServerRequest *req);
  void updateConfig(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                    size_t index, size_t total);
  void reboot(AsyncWebServerRequest *req);
  void handleNotFound(AsyncWebServerRequest *req);

public:
  Web();

  Web &onReadConfig(ReadConfigHandler readConfig);
  Web &onSetConfig(SetConfigHandler setConfig);
  Web &onAppendStatus(AppendStatusHandler appendStatus);

  void begin(String type);
};

#endif