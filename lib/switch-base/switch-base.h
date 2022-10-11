#ifndef _SWITCH_BASE_H_
#define _SWITCH_BASE_H_

#include <ArduinoJson.h>
#include <Ticker.h>

typedef std::function<void()> StateChangedHandler;

class SwitchBase {
private:
  StateChangedHandler _handler;
  uint8_t _suspendStateChanges;
  bool _pendingChanges;
  Ticker _throttle;
  uint32_t _lastSend;

protected:
  void raiseStateChanged();
  void suspendStateChanges();
  void resumeStateChanges();

public:
  void onStateChanged(StateChangedHandler handler);
  virtual void updateState(JsonVariantConst state) = 0;
};

#endif