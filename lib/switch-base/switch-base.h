#ifndef _SWITCH_BASE_H_
#define _SWITCH_BASE_H_

#include <ArduinoJson.h>

typedef std::function<void()> StateChangedHandler;

class SwitchBase {
private:
  StateChangedHandler _handler;

protected:
  void raiseStateChanged();

public:
  void onStateChanged(StateChangedHandler handler);
  virtual void updateState(JsonVariantConst state) = 0;
};

#endif