#ifndef _SWITCH_BLINDS_H_
#define _SWITCH_BLINDS_H_

#include "io.h"
#include "switch-base.h"
#include "util.h"
#include <Ticker.h>
#include <mutex>

enum MotorState {
  Motor_Off = 0,
  Motor_Opening,
  Motor_Closing,
};

class SwitchBlinds : public SwitchBase {
  Io &_io;
  bool _initialized = false;
  int8_t _pinOpen, _pinClose;
  MotorState _motorState = Motor_Off;
  MotorState _pendingMotor = Motor_Off; // waiting out the pause after a stop
  bool _restingAfterStop = false;
  bool _stateChanged = false;
  uint32_t _stoppedAt = 0;
  uint8_t _levelTouch, _levelRed, _levelChanging;
  int _position = 0;
  int _targetPosition = 0;
  int _maxPosition;
  int _openCloseDelta;
  uint32_t _motorChange;
  uint16_t _delayAfterOff;
  Ticker _ticker;
  // Guards all motor state: commands arrive on the MQTT task, touches and
  // update() on the timer task. Never publish state while holding it, because
  // the MQTT task holds the esp-mqtt API lock while waiting for this one.
  mutable std::mutex _lock;

  void updateLevels();
  void stopMotor();
  void haltMotor();
  void requestMotor(MotorState newState);
  void startPendingMotor();
  int getCurrentPosition(bool limit = false) const;
  void setTargetPosition(int target, bool addSafetyMargin);
  String getMotorStatus() const;

public:
  SwitchBlinds(Io &io);

  bool configure(const JsonVariantConst config);
  void appendState(JsonVariant doc) const;
  void updateState(JsonVariantConst state, bool isFromStoredState) override;

  void update();
};

#endif