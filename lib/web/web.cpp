#include "web.h"
#include "util.h"
#include <ArduinoJson.h>

using namespace std;
using namespace std::placeholders;

void NO_OP_REQ(AsyncWebServerRequest *req) {}

Web::Web(Preferences &preferences, Dimmer &dimmer, Io &io)
    : _server(80), _preferences(preferences), _dimmer(dimmer), _io(io) {}

void Web::begin(String type) {
  _type = type;

  _server.on("/api/status", HTTP_GET, bind(&Web::getStatus, this, _1));
  _server.on("/api/config", HTTP_GET, bind(&Web::getConfig, this, _1));
  _server.on("/api/config", HTTP_POST, NO_OP_REQ, NULL,
             bind(&Web::updateConfig, this, _1, _2, _3, _4, _5));
  _server.onNotFound(bind(&Web::handleNotFound, this, _1));
  _server.begin();
}

void Web::handleNotFound(AsyncWebServerRequest *req) {
  req->redirect("/api/status");
}

void Web::getStatus(AsyncWebServerRequest *req) {
  StaticJsonDocument<2048> json;
  json["freeHeap"] = ESP.getFreeHeap();
  json["uptimeSeconds"] = millis() / 1000;
  json["type"] = _type;

  if (_type == "dimmer") {
    auto dimmer = json.createNestedObject("dimmer");
    dimmer["on"] = _dimmer.isOn();
    dimmer["brightness"] = _dimmer.getBrightness();
  }

  auto touch = json.createNestedArray("touch");
  for (uint8_t i = 0; i < IO_CNT; i++) {
    auto parent = touch.createNestedObject();
    parent["value"] = _io.touch[i].getValue();
    parent["threshold"] = _io.touch[i].getThreshold();
  }

  String response;
  serializeJson(json, response);
  req->send(200, "application/json", response);
}

void Web::getConfig(AsyncWebServerRequest *req) {
  auto config = _preferences.isKey(CONFIG_KEY)
                    ? _preferences.getString(CONFIG_KEY)
                    : "{}";
  req->send(200, "application/json", config);
}

void Web::updateConfig(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                       size_t index, size_t total) {
  if (!index) {
    req->_tempObject = malloc(total + 1);
  }

  uint8_t *dest = &((uint8_t *)req->_tempObject)[index];
  memcpy(dest, data, len);

  if (index + len == total) {
    char *str = (char *)req->_tempObject;
    str[total] = 0;

    _preferences.putString(CONFIG_KEY, str);
    free(req->_tempObject);

    req->send(204);

    _reboot.once_ms(1500, []() { esp_restart(); });
  }
}