#include "switch-blinds.h"

#define POLL_INTERVAL MSEC(50)
#define SAFETY_DELTA SECS(1)

SwitchBlinds::SwitchBlinds(Io &io) : _io(io) {}

bool SwitchBlinds::configure(const JsonVariantConst config) {
  std::lock_guard<std::mutex> guard(_lock);
  bool needsReboot = false;

  int8_t pinOpen = config["pins"]["open"] | -1;
  int8_t pinClose = config["pins"]["close"] | -1;
  needsReboot = _pinOpen != pinOpen || _pinClose != pinClose;

  if (!_initialized) {
    _pinOpen = pinOpen;
    _pinClose = pinClose;

    if (_pinOpen != -1) {
      pinMode(_pinOpen, OUTPUT);
      digitalWrite(_pinOpen, LOW);
    }

    if (_pinClose != -1) {
      pinMode(_pinClose, OUTPUT);
      digitalWrite(_pinClose, LOW);
    }

    _ticker.attach_ms<SwitchBlinds *>(
        POLL_INTERVAL, [](SwitchBlinds *me) { me->update(); }, this);

    _io.onTouchDown([this](int8_t key) {
      std::lock_guard<std::mutex> guard(_lock);
      if (_motorState != Motor_Off || _pendingMotor != Motor_Off) {
        haltMotor();
      } else if (key == 1) {
        setTargetPosition(0, true);
      } else if (key == 2) {
        setTargetPosition(_maxPosition, true);
      }
    });
    _initialized = true;
  }

  _delayAfterOff = config["delay"] | 500;

  auto levels = config["levels"];
  _levelTouch = levels["touch"] | 255;
  _levelRed = levels["red"] | 20;
  _levelChanging = levels["changing"] | 80;
  updateLevels();

  _maxPosition = config["travel"] | SECS(40);
  _openCloseDelta = config["openCloseDelta"] | MSEC(1500);

  return needsReboot;
}

void SwitchBlinds::stopMotor() {
  if (_motorState == Motor_Off) {
    return;
  }

  digitalWrite(_pinOpen, LOW);
  digitalWrite(_pinClose, LOW);
  _position = getCurrentPosition(true);
  _motorState = Motor_Off;
  _stoppedAt = millis();
  _restingAfterStop = true;
  _stateChanged = true;
  updateLevels();
}

// Stops moving and stays put, also cancelling a move still waiting to start.
void SwitchBlinds::haltMotor() {
  stopMotor();
  _pendingMotor = Motor_Off;
  _targetPosition = _position;
  _stateChanged = true;
}

void SwitchBlinds::requestMotor(MotorState newState) {
  if (newState == _motorState) {
    _pendingMotor = Motor_Off;
    return;
  }

  stopMotor();
  _pendingMotor = newState;
  startPendingMotor();
}

// Starts the pending move once the pause after the last stop has passed. If it
// hasn't yet, update() calls this again until it has.
void SwitchBlinds::startPendingMotor() {
  if (_pendingMotor == Motor_Off) {
    return;
  }

  if (_restingAfterStop) {
    if (millis() - _stoppedAt < _delayAfterOff) {
      return;
    }
    _restingAfterStop = false;
  }

  _motorState = _pendingMotor;
  _pendingMotor = Motor_Off;

  bool opening = _motorState == Motor_Opening;
  digitalWrite(opening ? _pinClose : _pinOpen, LOW);
  digitalWrite(opening ? _pinOpen : _pinClose, HIGH);
  _motorChange = millis();
  _stateChanged = true;
  updateLevels();
}

int SwitchBlinds::getCurrentPosition(bool limit) const {
  if (_motorState == Motor_Off) {
    return _position;
  }

  uint32_t now = millis();

  int positionChange = now - _motorChange;
  int8_t signOfChange;
  if (_motorState == Motor_Closing) {
    positionChange =
        positionChange * _maxPosition / (_maxPosition - _openCloseDelta);
    signOfChange = 1;
  } else {
    signOfChange = -1;
  }

  int currentPosition = _position + positionChange * signOfChange;
  if (limit) {
    if (currentPosition < 0) {
      currentPosition = 0;
    } else if (currentPosition > _maxPosition) {
      currentPosition = _maxPosition;
    }
  }
  return currentPosition;
}

void SwitchBlinds::update() {
  bool notify;
  {
    std::lock_guard<std::mutex> guard(_lock);

    if ((_motorState == Motor_Opening &&
         getCurrentPosition() <= _targetPosition) ||
        (_motorState == Motor_Closing &&
         getCurrentPosition() >= _targetPosition)) {
      haltMotor();
    }

    startPendingMotor();

    notify = _stateChanged || _motorState != Motor_Off;
    _stateChanged = false;
  }

  if (notify) {
    raiseStateChanged();
  }
}

void SwitchBlinds::updateLevels() {
  _io.setLedLevels(0, _motorState == Motor_Opening ? _levelChanging : 0,
                   _motorState == Motor_Closing ? _levelChanging : 0,
                   _levelTouch, _levelRed);
}

void SwitchBlinds::setTargetPosition(int target, bool addSafetyMargin) {
  if (target <= 0) {
    target = addSafetyMargin ? -SAFETY_DELTA : 0;
  }
  if (target >= _maxPosition) {
    target = _maxPosition + (addSafetyMargin ? SAFETY_DELTA : 0);
  }

  if (_targetPosition == target) {
    return;
  }

  _targetPosition = target;

  int current = getCurrentPosition();
  if (current < _targetPosition) {
    requestMotor(Motor_Closing);
  } else if (current > _targetPosition) {
    requestMotor(Motor_Opening);
  } else {
    _pendingMotor = Motor_Off;
  }
}

void SwitchBlinds::appendState(JsonVariant doc) const {
  std::lock_guard<std::mutex> guard(_lock);
  if (_initialized) {
    int openPercent = (int)(100 - _targetPosition * 100 / _maxPosition);
    if (openPercent < 0) {
      openPercent = 0;
    } else if (openPercent > 100) {
      openPercent = 100;
    }

    doc["openPercent"] = openPercent;
    doc["target"] = _targetPosition;
    doc["current"] = getCurrentPosition(true);
    doc["motor"] = getMotorStatus();
  }
}

void SwitchBlinds::updateState(JsonVariantConst state, bool isFromStoredState) {
  std::lock_guard<std::mutex> guard(_lock);
  if (!_initialized)
    return;

  if (isFromStoredState) {
    if (_motorState != Motor_Off || _pendingMotor != Motor_Off)
      return;

    // restore state from JSON (most probably motor position hasn't changed)
    _position = state["current"];
    int targetPosition = state["target"];
    setTargetPosition(targetPosition, false);
  } else {
    auto stateOpenPercent = state["openPercent"];
    if (stateOpenPercent.is<int>()) {
      int openPercent = stateOpenPercent;
      setTargetPosition((100 - openPercent) * _maxPosition / 100, true);
    }
  }
}

String SwitchBlinds::getMotorStatus() const {
  switch (_motorState) {
  case Motor_Off:
    return "off";
  case Motor_Opening:
    return "open";
  case Motor_Closing:
    return "close";
  default:
    return "unknown";
  }
}