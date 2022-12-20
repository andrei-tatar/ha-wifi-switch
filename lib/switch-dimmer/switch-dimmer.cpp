#include "switch-dimmer.h"

SwitchDimmer::SwitchDimmer(Io &io) : _io(io) {}

bool SwitchDimmer::configure(const JsonVariantConst config) {
  int8_t zero = config["pins"]["zero"] | -1;
  int8_t triac = config["pins"]["triac"] | -1;

  bool needsReboot = _dimmer.usePins(zero, triac);
  if (!_initialized) {
    _dimmer.begin();

    _io.onTouchDown([this](int8_t key) {
      _wasOff = !_dimmer.isOn();
      if (!key || _wasOff) {
        _dimmer.toggle();
        _suspendedWhileTouchDown = false;
      } else {
        suspendStateChanges();
        _suspendedWhileTouchDown = true;
        _dimmer.changeBrightness(key == 1 ? 1 : -1);
      }
    });
    _io.onTouchPress([this](int8_t key) {
      if (key == 0 || _wasOff) {
        return;
      }
      _dimmer.changeBrightness(key == 1 ? -1 : 1);
    });
    _io.onTouchUp([this] {
      if (_suspendedWhileTouchDown) {
        resumeStateChanges();
      }
    });

    _dimmer.onStateChanged([this](bool on, uint8_t brightness) {
      updateLevels();
      raiseStateChanged();
    });
  }

  auto curveConfig = config["curve"].as<JsonArrayConst>();
  if (curveConfig.size() == 100) {
    uint16_t curve[100];
    for (uint8_t i = 0; i < 100; i++) {
      curve[i] = curveConfig[i];
    }
    _dimmer.setBrightnessCurve(curve);
  }

  uint8_t min = config["min"] | 1;
  uint8_t max = config["max"] | 100;
  _dimmer.setMinMax(min, max);

  auto onLevels = config["levels"]["on"];
  auto offLevels = config["levels"]["off"];

  _onBlueLevel = onLevels["blue"] | 20;
  _onBlueTouchLevel = onLevels["touch"] | 255;
  _onRedLevel = onLevels["red"] | 0;
  _offBlueLevel = offLevels["blue"] | 0;
  _offBlueTouchLevel = offLevels["touch"] | 255;
  _offRedLevel = offLevels["red"] | 20;
  updateLevels();

  _initialized = true;
  return needsReboot;
}

void SwitchDimmer::updateLevels() {
  if (_dimmer.isOn()) {
    _io.setLedLevels(_onBlueLevel, _onBlueLevel, _onBlueLevel,
                     _onBlueTouchLevel, _onRedLevel);
  } else {
    _io.setLedLevels(_offBlueLevel, _offBlueLevel, _offBlueLevel,
                     _offBlueTouchLevel, _offRedLevel);
  }
}

void SwitchDimmer::appendState(JsonVariant doc) const {
  if (_initialized) {
    doc["on"] = _dimmer.isOn();
    doc["brightness"] = _dimmer.getBrightness();
  }
}

void SwitchDimmer::updateState(JsonVariantConst state) {
  if (!_initialized) {
    return;
  }

  suspendStateChanges();

  auto stateOn = state["on"];
  if (stateOn.is<bool>()) {
    _dimmer.setOn(stateOn);
  }

  auto stateBrightness = state["brightness"];
  if (stateBrightness.is<uint8_t>()) {
    _dimmer.setBrightness(stateBrightness);
  }

  resumeStateChanges();
}
