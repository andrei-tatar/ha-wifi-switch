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
  uint8_t _onBlueLevel, _onBlueTouchLevel, _onRedLevel, _offBlueLevel,
      _offBlueTouchLevel, _offRedLevel;

  void updateLevels();

public:
  SwitchDimmer(Io &io);
  bool configure(const JsonVariantConst config);
  void appendStatus(JsonVariant doc) const;
  void appendState(JsonVariant doc) const;
  void updateState(JsonVariantConst state) override;
};

#endif