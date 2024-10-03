#include "configuration.h"

#define CONFIG_KEY "config"

void Configuration::begin() { preferences.begin("ha-switch"); }

DynamicJsonDocument *Configuration::read() {
  if (!preferences.isKey(CONFIG_KEY)) {
    return new DynamicJsonDocument(10);
  }

  auto json = preferences.getString(CONFIG_KEY);
  DynamicJsonDocument *config = new DynamicJsonDocument(2048);
  deserializeJson(*config, json);
  return config;
}

void Configuration::update(String json) {
  // TODO: patch existing config
  preferences.putString(CONFIG_KEY, json);
}
