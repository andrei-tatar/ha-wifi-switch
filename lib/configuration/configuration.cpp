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

bool Configuration::update(String json) {
  JsonDocument config;
  if (deserializeJson(config, json) != DeserializationError::Ok ||
      !config.is<JsonObject>()) {
    return false;
  }

  // TODO: patch existing config
  return preferences.putString(CONFIG_KEY, json) == json.length();
}
