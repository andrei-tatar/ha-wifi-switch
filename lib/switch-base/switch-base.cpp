#include "switch-base.h"

void SwitchBase::raiseStateChanged() {
  if (_handler)
    _handler();
}

void SwitchBase::onStateChanged(StateChangedHandler handler) {
  _handler = handler;
}