#ifndef _SWITCH_ONOFF_
#define _SWITCH_ONOFF_

#include "io.h"
#include "switch-base.h"
#include <ArduinoJson.h>
#include <mutex>

class SwitchOnOff : public SwitchBase {
  Io &_io;
  bool _state[IO_CNT] = {false, false, false};
  int8_t _pins[IO_CNT];
  bool _initialized = false;
  uint8_t _onBlueLevel, _onRedLevel, _offBlueLevel, _offRedLevel,
      _blueTouchLevel;
  uint16_t _resetAfter;
  Ticker _ticker;
  uint8_t _resetPinsMask = 0;  // outputs with a running timer
  uint32_t _resetAt[IO_CNT];   // millis() when each of those turns off
  // Guards outputs and timers: commands arrive on the MQTT task, touches and
  // timers on the timer task. Never publish state while holding it, because the
  // MQTT task holds the esp-mqtt API lock while waiting for this one.
  mutable std::mutex _lock;

  void updateLevels();
  void checkResetTimers();
  void setResetTimer(uint8_t index, uint16_t seconds);
  bool setOutput(uint8_t index, bool newState, uint16_t offAfterSeconds);
  bool restoreState(JsonVariantConst state);
  bool applyCommand(JsonVariantConst state);

public:
  SwitchOnOff(Io &io);
  bool configure(const JsonVariantConst config);
  void appendState(JsonVariant doc) const;
  void updateState(JsonVariantConst state, bool isFromStoredState) override;
};

#endif