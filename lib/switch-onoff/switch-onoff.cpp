#include "switch-onoff.h"

#define POLL_INTERVAL MSEC(50)

SwitchOnOff::SwitchOnOff(Io &io) : _io(io), _pins{-1, -1, -1} {}

bool SwitchOnOff::configure(const JsonVariantConst config) {
  std::lock_guard<std::mutex> guard(_lock);
  bool needsReboot = false;

  for (uint8_t i = 0; i < IO_CNT; i++) {
    int8_t newPin = config["pins"][i] | -1;
    needsReboot |= _pins[i] != newPin;
    _pins[i] = newPin;
  }

  if (!_initialized) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_pins[i] != -1) {
        pinMode(_pins[i], OUTPUT);
      }
    }

    _ticker.attach_ms<SwitchOnOff *>(
        POLL_INTERVAL, [](SwitchOnOff *me) { me->checkResetTimers(); }, this);

    _io.onTouchDown([this](int8_t key) {
      {
        std::lock_guard<std::mutex> guard(_lock);
        _state[key] = !_state[key];
        if (_pins[key] != -1) {
          digitalWrite(_pins[key], _state[key] ? HIGH : LOW);
          setResetTimer(key, _state[key] ? _resetAfter : 0);
        }
        updateLevels();
      }
      raiseStateChanged();
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
  std::lock_guard<std::mutex> guard(_lock);
  if (_initialized) {
    auto state = doc["on"].to<JsonArray>();
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_pins[i] != -1) {
        state.add(_state[i]);
      }
    }

    // bitmask by pin index, read back by restoreState()
    if (_resetPinsMask) {
      doc["autoOff"] = _resetPinsMask;
    }
  }
}

void SwitchOnOff::checkResetTimers() {
  bool changed = false;
  {
    std::lock_guard<std::mutex> guard(_lock);
    uint32_t now = millis();

    for (uint8_t i = 0; i < IO_CNT; i++) {
      // the signed difference keeps this correct when millis() rolls over
      if ((_resetPinsMask & (1 << i)) && (int32_t)(now - _resetAt[i]) >= 0) {
        changed |= setOutput(i, false, 0);
      }
    }

    if (changed) {
      updateLevels();
    }
  }

  if (changed) {
    raiseStateChanged();
  }
}

void SwitchOnOff::setResetTimer(uint8_t index, uint16_t seconds) {
  if (seconds) {
    _resetAt[index] = millis() + seconds * 1000UL;
    _resetPinsMask |= 1 << index;
  } else {
    _resetPinsMask &= ~(1 << index);
  }
}

// Setting an output always replaces its timer, so an output that was turned off,
// or back on without a time, can't be switched off later by an old timer.
bool SwitchOnOff::setOutput(uint8_t index, bool newState, uint16_t offAfterSeconds) {
  if (_pins[index] == -1) {
    return false;
  }

  setResetTimer(index, newState ? offAfterSeconds : 0);

  if (newState == _state[index]) {
    return false;
  }

  _state[index] = newState;
  digitalWrite(_pins[index], newState ? HIGH : LOW);
  return true;
}

// Outputs on a timer stay off after a reboot: the timer doesn't survive it, and
// re-energizing a momentary output would fire it again (garage door, gate).
bool SwitchOnOff::restoreState(JsonVariantConst state) {
  auto on = state["on"].as<JsonArrayConst>();
  uint8_t autoOff = state["autoOff"] | 0;
  uint8_t onIndex = 0;
  bool changed = false;

  for (uint8_t i = 0; i < IO_CNT; i++) {
    if (_pins[i] != -1) {
      auto stored = on[onIndex++];
      if (stored.is<bool>()) {
        bool newState = stored;
        if (!newState || (!_resetAfter && !(autoOff & (1 << i)))) {
          changed |= setOutput(i, newState, 0);
        }
      }
    }
  }

  return changed;
}

bool SwitchOnOff::applyCommand(JsonVariantConst state) {
  auto on = state["on"];
  auto forSeconds = state["for"];
  uint16_t offAfterSeconds =
      forSeconds.is<uint16_t>() ? forSeconds.as<uint16_t>() : _resetAfter;
  bool changed = false;

  if (on.is<bool>()) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      changed |= setOutput(i, on.as<bool>(), offAfterSeconds);
    }
  } else {
    uint8_t onIndex = 0;
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_pins[i] != -1) {
        auto stateForPin = on[onIndex++];
        if (stateForPin.is<bool>()) {
          changed |= setOutput(i, stateForPin.as<bool>(), offAfterSeconds);
        }
      }
    }
  }

  return changed;
}

void SwitchOnOff::updateState(JsonVariantConst state, bool isFromStoredState) {
  bool changed;
  {
    std::lock_guard<std::mutex> guard(_lock);
    if (!_initialized) {
      return;
    }

    uint8_t timersBefore = _resetPinsMask;
    changed = isFromStoredState ? restoreState(state) : applyCommand(state);
    changed |= _resetPinsMask != timersBefore; // reported in state as autoOff

    if (changed) {
      updateLevels();
    }
  }

  if (changed) {
    raiseStateChanged();
  }
}
