#include "switch-base.h"
#include "util.h"

void SwitchBase::raiseStateChanged() {
  if (_handler) {
    if (_suspendStateChanges) {
      _pendingChanges = true;
    } else {

      auto now = millis();
      if (now - _lastSend > MSEC(500)) {
        _lastSend = now;
        _handler();
        _pendingChanges = false;
      } else {
        _pendingChanges = true;
        _throttle.once_ms<SwitchBase *>(
            500, [](SwitchBase *arg) { arg->raiseStateChanged(); }, this);
      }
    }
  }
}

void SwitchBase::onStateChanged(StateChangedHandler handler) {
  _handler = handler;
}

void SwitchBase::suspendStateChanges() { _suspendStateChanges++; }

void SwitchBase::resumeStateChanges() {
  if (_suspendStateChanges) {
    _suspendStateChanges--;
    if (_suspendStateChanges == 0) {
      if (_pendingChanges) {
        raiseStateChanged();
      }
    }
  }
}