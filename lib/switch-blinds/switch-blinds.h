#ifndef _SWITCH_BLINDS_H_
#define _SWITCH_BLINDS_H_

#include "io.h"
#include "switch-base.h"

class SwitchBlinds : public SwitchBase {
  Io &_io;
  bool _initialized;
  int8_t _pinUp, _pinDown;

public:
  SwitchBlinds(Io &io);

  bool configure(const JsonVariantConst config);
  void appendState(JsonVariant doc) const;
  void updateState(JsonVariantConst state) override;
};

#endif