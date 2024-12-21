#include "web.h"

using namespace std;
using namespace std::placeholders;

void NO_OP_REQ(AsyncWebServerRequest *req) {}

Web::Web() : _server(80) {}

void Web::begin(String type) {
  _server.on("/api/status", HTTP_GET, bind(&Web::getStatus, this, _1));
  _server.on("/api/config", HTTP_GET, bind(&Web::getConfig, this, _1));
  _server.on("/api/config", HTTP_POST, NO_OP_REQ, NULL,
             bind(&Web::updateConfig, this, _1, _2, _3, _4, _5));
  _server.on("/api/reboot", HTTP_POST, bind(&Web::reboot, this, _1));
  _server.onNotFound(bind(&Web::handleNotFound, this, _1));
  _server.begin();
}

void Web::handleNotFound(AsyncWebServerRequest *req) {
  req->redirect("/api/status");
}

void Web::getStatus(AsyncWebServerRequest *req) {
  JsonDocument json;
  json["freeHeap"] = ESP.getFreeHeap();
  json["uptimeSeconds"] = millis() / 1000;
  json["version"] = BUILD_VERSION;
  json["cpuFreqMhz"] = getCpuFrequencyMhz();

  auto wifi = json["wifi"].to<JsonObject>();
  wifi["rssi"] = WiFi.RSSI();
  wifi["ip"] = WiFi.localIP().toString();

  if (_appendStatus) {
    _appendStatus(json);
  }

  String response;
  serializeJson(json, response);
  req->send(200, "application/json", response);
}

void Web::getConfig(AsyncWebServerRequest *req) {
  if (_readConfig) {
    auto config = _readConfig();
    String response;
    serializeJson(*config, response);
    delete config;

    req->send(200, "application/json", response);
  } else {
    req->send(500, "text/html", "NO GET HANDLER");
  }
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

    if (_setConfig) {
      _setConfig(str);
      req->send(204);
    } else {
      req->send(500, "text/html", "NO SET HANDLER");
    }

    free(req->_tempObject);
    req->_tempObject = NULL;
  }
}

void Web::reboot(AsyncWebServerRequest *req) {
  req->send(204);
  _rebootTicker.once_ms(1500, []() { ESP.restart(); });
}

Web &Web::onReadConfig(ReadConfigHandler readConfig) {
  _readConfig = readConfig;
  return *this;
}

Web &Web::onSetConfig(SetConfigHandler setConfig) {
  _setConfig = setConfig;
  return *this;
}

Web &Web::onAppendStatus(AppendStatusHandler appendStatus) {
  _appendStatus = appendStatus;
  return *this;
}