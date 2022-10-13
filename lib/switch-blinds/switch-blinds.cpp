#include "switch-blinds.h"

#define POLL_INTERVAL MSEC(50)
#define POSITION_UNKNOWN -1

SwitchBlinds::SwitchBlinds(Io &io) : _io(io) {}

bool SwitchBlinds::configure(const JsonVariantConst config) {
  if (!_initialized) {
    _pinOpen = config["pins"]["open"] | -1;
    _pinClose = config["pins"]["close"] | -1;

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

  return false;
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

  raiseStateChanged();
}

int SwitchBlinds::getCurrentPosition() const {
  if (_motorState == Motor_Off) {
    return _position;
  }

  int8_t signOfChange = _motorState == Motor_Opening ? -1 : 1;
  uint32_t now = millis();
  int currentPosition = _position + (now - _motorChange) * signOfChange;
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
  if (_position == POSITION_UNKNOWN) {
    // TODO: we need a calibration step
    _position = 0;
  }

  if (target < 0) {
    target = 0;
  }
  if (target > _maxPosition) {
    target = _maxPosition;
  }

  if (_targetPosition == target) {
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