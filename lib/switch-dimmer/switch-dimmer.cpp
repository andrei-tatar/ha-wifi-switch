#include "switch-dimmer.h"

SwitchDimmer::SwitchDimmer(Io &io) : _io(io) {}

bool SwitchDimmer::configure(const JsonVariantConst config) {
  if (!_dimmer.isInitialized()) {
    int8_t zero = config["pins"]["zero"] | -1;
    int8_t triac = config["pins"]["triac"] | -1;

    _dimmer.usePins(zero, triac);
    _dimmer.begin();

    _io.onTouchDown([this](int8_t key) {
      _wasOff = !_dimmer.isOn();
      if (!key || _wasOff) {
        _dimmer.toggle();
      } else {
        _dimmer.changeBrightness(key == 1 ? 1 : -1);
      }
    });
    _io.onTouchPress([this](int8_t key) {
      if (key == 0 || _wasOff) {
        return;
      }
      _dimmer.changeBrightness(key == 1 ? 1 : -1);
    });

    _dimmer.onStateChanged([this](bool on, uint8_t brightness) {
      updateLevels();
      raiseStateChanged();
    });
  }

  if (config.containsKey("curve")) {
    uint16_t curve[100];
    for (uint8_t i = 0; i < 100; i++) {
      curve[i] = config["curve"].getElement(i);
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

  return false;
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

void SwitchDimmer::appendStatus(JsonVariant doc) const {
  if (_dimmer.isInitialized()) {
    JsonVariant state =
        doc.createNestedObject("dimmer").createNestedObject("state");
    appendState(state);
  }
}

void SwitchDimmer::appendState(JsonVariant doc) const {
  if (_dimmer.isInitialized()) {
    doc["on"] = _dimmer.isOn();
    doc["brightness"] = _dimmer.getBrightness();
  }
}

void SwitchDimmer::updateState(JsonVariantConst state) {
  if (!_dimmer.isInitialized()) {
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
