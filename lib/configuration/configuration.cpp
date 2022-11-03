#include "configuration.h"

#define CONFIG_KEY "config"

void Configuration::begin() { preferences.begin("ha-switch"); }

void Configuration::read(JsonDocument &config) {
  deserializeJson(config, readRaw());
}

String Configuration::readRaw() {
  return preferences.isKey(CONFIG_KEY) ? preferences.getString(CONFIG_KEY)
                                       : "{}";
}

void Configuration::update(String json) {
  // TODO: patch existing config
  preferences.putString(CONFIG_KEY, json);
}
