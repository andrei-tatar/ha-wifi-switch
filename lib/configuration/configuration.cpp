#include "configuration.h"

#define CONFIG_KEY "config"

void Configuration::begin() { preferences.begin("ha-switch"); }

JsonDocument *Configuration::read() {
  if (!preferences.isKey(CONFIG_KEY)) {
    return new JsonDocument();
  }

  auto json = preferences.getString(CONFIG_KEY);
  JsonDocument *config = new JsonDocument();
  deserializeJson(*config, json);
  return config;
}

void Configuration::update(String json) {
  // TODO: patch existing config
  preferences.putString(CONFIG_KEY, json);
}
