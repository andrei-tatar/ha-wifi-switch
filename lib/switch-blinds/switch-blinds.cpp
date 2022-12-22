#include "switch-blinds.h"

#define POLL_INTERVAL MSEC(50)
#define POSITION_UNKNOWN -1

SwitchBlinds::SwitchBlinds(Io &io) : _io(io) {}

bool SwitchBlinds::configure(const JsonVariantConst config) {
  bool needsReboot = false;

  int8_t pinOpen = config["pins"]["open"] | -1;
  int8_t pinClose = config["pins"]["close"] | -1;
  needsReboot = _pinOpen != pinOpen || _pinClose != pinClose;

  if (!_initialized) {
    _pinOpen = pinOpen;
    _pinClose = pinClose;

    if (_pinOpen != -1) {
      pinMode(_pinOpen, OUTPUT);
    }

    if (_pinClose != -1) {
      pinMode(_pinClose, OUTPUT);
    }

    _ticker.attach_ms<SwitchBlinds *>(
        POLL_INTERVAL, [](SwitchBlinds *me) { me->update(); }, this);

    _io.onTouchDown([this](int8_t key) {
      if (_motorState != Motor_Off) {
        changeMotor(Motor_Off);
      } else if (key == 1) {
        setTargetPosition(0);
      } else if (key == 2) {
        setTargetPosition(_maxPosition);
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

void SwitchBlinds::changeMotor(MotorState newState) {
  if (newState == _motorState) {
    return;
  }

  if (_motorState != Motor_Off) {
    digitalWrite(_pinOpen, LOW);
    digitalWrite(_pinClose, LOW);
    _position = getCurrentPosition();
    _motorState = Motor_Off;
    updateLevels();

    delay(_delayAfterOff);
  }

  _motorState = newState;

  if (_motorState == Motor_Off) {
    _targetPosition = _position;
  } else if (_motorState == Motor_Opening) {
    digitalWrite(_pinOpen, HIGH);
    _motorChange = millis();
  } else if (_motorState == Motor_Closing) {
    digitalWrite(_pinClose, HIGH);
    _motorChange = millis();
  }

  updateLevels();

  if (_pendingTarget == -1) {
    raiseStateChanged();
  }
}

int SwitchBlinds::getCurrentPosition() const {
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

  if (currentPosition < 0) {
    currentPosition = 0;
  }
  if (currentPosition > _maxPosition) {
    currentPosition = _maxPosition;
  }
  return currentPosition;
}

void SwitchBlinds::update() {
  if (_motorState == Motor_Off) {

    if (_pendingTarget != -1) {
      auto pending = _pendingTarget;
      _pendingTarget = -1;
      setTargetPosition(pending);
    }

    return;
  }

  if (_motorState == Motor_Opening) {
    if (getCurrentPosition() <= _targetPosition) {
      changeMotor(Motor_Off);
    }
  }

  if (_motorState == Motor_Closing) {
    if (getCurrentPosition() >= _targetPosition) {
      changeMotor(Motor_Off);
    }
  }
}

void SwitchBlinds::updateLevels() {
  _io.setLedLevels(0, _motorState == Motor_Opening ? _levelChanging : 0,
                   _motorState == Motor_Closing ? _levelChanging : 0,
                   _levelTouch, _levelRed);
}

void SwitchBlinds::setTargetPosition(int target) {
  if (target < 0) {
    target = 0;
  }
  if (target > _maxPosition) {
    target = _maxPosition;
  }

  if (_pendingTarget != -1) {
    _pendingTarget = target;
    return;
  }

  if (_position == POSITION_UNKNOWN) {
    auto half = _maxPosition / 2;
    _position = target < half ? _maxPosition : 0;
    _pendingTarget = target;
    target = target > half ? _maxPosition : 0;
  } else if (_targetPosition == target) {
    return;
  }

  _targetPosition = target;

  if (getCurrentPosition() < _targetPosition) {
    changeMotor(Motor_Closing);
  } else if (getCurrentPosition() > _targetPosition) {
    changeMotor(Motor_Opening);
  }
}

void SwitchBlinds::appendState(JsonVariant doc) const {
  if (_initialized) {
    doc["openPercent"] = (int)(100 - _targetPosition * 100 / _maxPosition);
    doc["target"] = _targetPosition;
    doc["current"] = getCurrentPosition();
    doc["motor"] = getMotorStatus();
    doc["pendingTarget"] = _pendingTarget;
  }
}

void SwitchBlinds::updateState(JsonVariantConst state) {
  if (_initialized) {
    auto stateOpenPercent = state["openPercent"];
    if (stateOpenPercent.is<int>()) {
      int openPercent = stateOpenPercent;
      setTargetPosition((100 - openPercent) * _maxPosition / 100);
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