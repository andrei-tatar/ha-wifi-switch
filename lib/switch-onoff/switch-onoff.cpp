#include "switch-onoff.h"

SwitchOnOff::SwitchOnOff(Io &io) : _io(io), _pins{-1, -1, -1} {}

bool SwitchOnOff::configure(const JsonVariantConst config) {
  bool needsReboot = false;

  for (uint8_t i = 0; i < IO_CNT; i++) {
    int8_t newPin = config["pins"].getElement(i) | -1;
    needsReboot |= _pins[i] != newPin;
    _pins[i] = newPin;
  }

  if (!_initialized) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_pins[i] != -1) {
        pinMode(_pins[i], OUTPUT);
      }
    }

    _io.onTouchDown([this](int8_t key) {
      _state[key] = !_state[key];
      if (_pins[key] != -1) {
        digitalWrite(_pins[key], _state[key] ? HIGH : LOW);
      }
      updateLevels();
      raiseStateChanged();

      if (_resetAfter) {
        _resetPinsMask |= 1 << key;
        _ticker.once_ms(_resetAfter * 1000, SwitchOnOff::resetPins, this);
      }
    });
  }

  auto onLevels = config["levels"]["on"];
  auto offLevels = config["levels"]["off"];
  _blueTouchLevel = config["levels"]["touch"] | 255;
  _onBlueLevel = onLevels["blue"] | 20;
  _onRedLevel = onLevels["red"] | 0;
  _offBlueLevel = offLevels["blue"] | 0;
  _offRedLevel = offLevels["red"] | 20;
  _resetAfter = config["resetAfter"] | 0;
  updateLevels();

  _initialized = true;
  return needsReboot;
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

void SwitchOnOff::resetPins(SwitchOnOff *instance) {
  SwitchOnOff &me = *instance;

  me.suspendStateChanges();
  for (uint8_t i = 0; i < IO_CNT; i++) {
    if (me._resetPinsMask & (1 << i)) {
      me.updatePin(i, false);
    }
  }

  me._resetPinsMask = 0;

  if (me.hasPendingChanges()) {
    me.updateLevels();
  }

  me.resumeStateChanges();
}

void SwitchOnOff::updatePin(uint8_t index, bool newState) {
  if (_pins[index] != -1 && newState != _state[index]) {
    _state[index] = newState;
    digitalWrite(_pins[index], _state[index] ? HIGH : LOW);
    raiseStateChanged();
  }
}

void SwitchOnOff::updateState(JsonVariantConst state, bool isFromStoredState) {
  if (!_initialized) {
    return;
  }

  auto on = state["on"];
  auto forSeconds = state["for"];

  bool resetAfterTime = false;
  uint8_t resetPinsMask = 0;

  suspendStateChanges();

  if (on.is<bool>()) {
    bool newState = on.as<bool>();
    resetAfterTime = forSeconds.is<uint16_t>() && newState;
    for (uint8_t i = 0; i < IO_CNT; i++) {
      resetPinsMask |= 1 << i;
      updatePin(i, newState);
    }
  } else {
    uint8_t onIndex = 0;
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_pins[i] != -1) {
        auto stateForPin = on.getElement(onIndex++);
        if (stateForPin.is<bool>()) {
          bool newState = stateForPin;
          if (stateForPin) {
            resetAfterTime |= forSeconds.is<uint16_t>();
            resetPinsMask |= 1 << i;
          }
          updatePin(i, stateForPin);
        }
      }
    }
  }

  if (_resetAfter > 0) {
    resetAfterTime = true;
  }

  if (resetAfterTime) {
    auto afterSeconds = forSeconds | _resetAfter;
    if (afterSeconds) {
      _resetPinsMask |= resetPinsMask;
      _ticker.once_ms(afterSeconds * 1000, SwitchOnOff::resetPins, this);
    } else {
      _ticker.detach();
    }
  }

  if (hasPendingChanges()) {
    updateLevels();
  }

  resumeStateChanges();
}