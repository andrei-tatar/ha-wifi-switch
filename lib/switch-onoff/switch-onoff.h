#ifndef _SWITCH_ONOFF_
#define _SWITCH_ONOFF_

#include "io.h"
#include "switch-base.h"
#include <ArduinoJson.h>

class SwitchOnOff : public SwitchBase {
  Io &_io;
  bool _state[IO_CNT] = {false, false, false};
  int8_t _pins[IO_CNT];
  bool _initialized = false;
  uint8_t _onBlueLevel, _onRedLevel, _offBlueLevel, _offRedLevel,
      _blueTouchLevel;
  Ticker _ticker;
  void updateLevels();
  void updatePin(uint8_t index, bool newState);

  static void resetPins(SwitchOnOff *instance);
  uint8_t _resetPinsMask;

public:
  SwitchOnOff(Io &io);
  bool configure(const JsonVariantConst config);
  void appendState(JsonVariant doc) const;
  void updateState(JsonVariantConst state, bool isFromStoredState) override;
};

#endif