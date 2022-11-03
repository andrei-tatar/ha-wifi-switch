#ifndef _CONFIGURATION_H_
#define _CONFIGURATION_H_

#include <ArduinoJson.h>
#include <Preferences.h>

class Configuration {
private:
  Preferences preferences;

public:
  void begin();
  String readRaw();
  void read(JsonDocument &doc);
  void update(String json);
};

#endif