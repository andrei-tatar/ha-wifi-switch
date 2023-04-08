#ifndef _SWITCH_BLINDS_H_
#define _SWITCH_BLINDS_H_

#include "io.h"
#include "switch-base.h"
#include "util.h"
#include <Ticker.h>

enum MotorState {
  Motor_Off = 0,
  Motor_Opening,
  Motor_Closing,
};

class SwitchBlinds : public SwitchBase {
  Io &_io;
  bool _initialized;
  int8_t _pinOpen, _pinClose;
  MotorState _motorState = Motor_Off;
  uint8_t _levelTouch, _levelRed, _levelChanging;
  int _position = 0;
  int _targetPosition = 0;
  int _maxPosition;
  int _openCloseDelta;
  uint32_t _motorChange;
  uint16_t _delayAfterOff;
  Ticker _ticker;

  void updateLevels();
  void changeMotor(MotorState newState);
  int getCurrentPosition(bool limit = false) const;
  void setTargetPosition(int target);
  String getMotorStatus() const;

public:
  SwitchBlinds(Io &io);

  bool configure(const JsonVariantConst config);
  void appendState(JsonVariant doc) const;
  void updateState(JsonVariantConst state, bool isFromStoredState) override;

  void update();
};

#endif