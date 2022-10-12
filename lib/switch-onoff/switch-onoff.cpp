#include "switch-onoff.h"

SwitchOnOff::SwitchOnOff(Io &io) : _io(io), _pins{-1, -1, -1} {}

bool SwitchOnOff::configure(const JsonVariantConst config) {
  if (!_initialized) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      _pins[i] = config["pins"].getElement(i) | -1;
      if (_pins[i] != -1) {
        pinMode(_pins[i], OUTPUT);
      }
    }
  }

  auto onLevels = config["levels"]["on"];
  auto offLevels = config["levels"]["off"];
  _blueTouchLevel = config["levels"]["touch"] | 255;
  _onBlueLevel = onLevels["blue"] | 20;
  _onRedLevel = onLevels["red"] | 0;
  _offBlueLevel = offLevels["blue"] | 0;
  _offRedLevel = offLevels["red"] | 20;

  updateLevels();

  _io.onTouchDown([this](int8_t key) {
    _state[key] = !_state[key];
    if (_pins[key] != -1) {
      digitalWrite(_pins[key], _state[key] ? HIGH : LOW);
    }
    updateLevels();
    raiseStateChanged();
  });

  _initialized = true;

  return false;
}

void SwitchOnOff::updateLevels() {
  _io.setLedLevels(_state[0] ? _onBlueLevel : _offBlueLevel,
                   _state[1] ? _onBlueLevel : _offBlueLevel,
                   _state[2] ? _onBlueLevel : _offBlueLevel, _blueTouchLevel,
                   _state[0] || _state[1] || _state[2] ? _onRedLevel
                                                       : _offRedLevel);
}

void SwitchOnOff::appendState(JsonVariant doc) const {
  if (_initialized) {
    auto state = doc.createNestedArray("on");
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_pins[i] != -1) {
        state.addElement().set(_state[i]);
      }
    }
  }
}

void SwitchOnOff::updateState(JsonVariantConst state) {
  if (_initialized) {
    auto on = state["on"];
    bool hasChanges = false;

    uint8_t onIndex = 0;
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_pins[i] != -1) {
        auto stateForPin = on.getElement(onIndex++);
        if (stateForPin.is<bool>()) {
          bool newState = stateForPin;
          if (newState != _state[i]) {
            _state[i] = newState;
            digitalWrite(_pins[i], _state[i] ? HIGH : LOW);
            hasChanges = true;
          }
        }
      }
    }

    if (hasChanges) {
      updateLevels();
      raiseStateChanged();
    }
  }
}
