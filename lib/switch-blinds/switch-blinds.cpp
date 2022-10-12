#include "switch-blinds.h"

SwitchBlinds::SwitchBlinds(Io &io) : _io(io) {}

bool SwitchBlinds::configure(const JsonVariantConst config) {
  if (!_initialized) {

    _pinUp = config["pins"]["up"] | -1;
    _pinDown = config["pins"]["down"] | -1;

    if (_pinUp != -1) {
      pinMode(_pinUp, OUTPUT);
    }

    if (_pinDown != -1) {
      pinMode(_pinDown, OUTPUT);
    }
  }

  _initialized = true;

  return false;
}
void SwitchBlinds::appendState(JsonVariant doc) const {}
void SwitchBlinds::updateState(JsonVariantConst state) {
  if (_initialized) {
    state["openPercent"] = 0;
  }
}