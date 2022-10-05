#ifndef _WEB_H_
#define _WEB_H_

#include "dimmer.h"
#include "io.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Ticker.h>

class Web {
private:
  AsyncWebServer _server;
  Preferences &_preferences;
  Dimmer &_dimmer;
  Io &_io;
  Ticker _reboot;
  String _type;

  void getConfig(AsyncWebServerRequest *req);
  void getStatus(AsyncWebServerRequest *req);
  void updateConfig(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                    size_t index, size_t total);
  void handleNotFound(AsyncWebServerRequest *req);

public:
  Web(Preferences &preferences, Dimmer &dimmer, Io &io);

  void begin(String type);
};

#endif