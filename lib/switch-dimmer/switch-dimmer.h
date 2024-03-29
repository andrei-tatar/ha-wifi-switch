#ifndef _SWITCH_DIMMER_
#define _SWITCH_DIMMER_

#include "dimmer.h"
#include "io.h"
#include "switch-base.h"
#include <ArduinoJson.h>

class SwitchDimmer : public SwitchBase {
  Dimmer _dimmer;
  Io &_io;
  bool _wasOff;
  bool _initialized = false;
  bool _suspendedWhileTouchDown;
  uint8_t _onBlueLevel, _onBlueTouchLevel, _onRedLevel, _offBlueLevel,
      _offBlueTouchLevel, _offRedLevel;
  uint32_t _touchDown;

  void updateLevels();

public:
  SwitchDimmer(Io &io);
  bool configure(const JsonVariantConst config);
  void appendState(JsonVariant doc) const;
  void updateState(JsonVariantConst state, bool isFromStoredState) override;
};

#endif