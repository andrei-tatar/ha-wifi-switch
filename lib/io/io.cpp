#include "io.h"

#define IO_PRESS_REPEAT MSEC(25)

Io::Io() : _qt(Wire) {
  for (uint8_t i = 0; i < IO_CNT; i++)
    _ledPins[i] = -1;
}

bool Io::useLedPins(int8_t led1, int8_t led2, int8_t led3, int8_t redLed,
                    bool invertRedLed) {
  bool pinsChanged = _ledPins[0] != led1 || _ledPins[1] != led2 ||
                     _ledPins[2] != led3 || _ledRed != redLed;

  _ledPins[0] = led1;
  _ledPins[1] = led2;
  _ledPins[2] = led3;
  _ledRed = redLed;
  _invertLedRed = invertRedLed;

  return pinsChanged;
}

bool Io::useQtTouch(int8_t sdaPin, int8_t sclPin, int8_t ch1, int8_t ch2,
                    int8_t ch3) {
  bool pinsChanged = _qt.usePins(sdaPin, sclPin) || _use != UseQt;
  _qt.useChannels(ch1, ch2, ch3);
  _use = UseQt;
  return pinsChanged;
}

bool Io::useInputPins(int8_t i1, int8_t i2, int8_t i3) {
  bool pinsChanged =
      _inputs[0] != i1 || _inputs[1] != i2 || _inputs[2] != i3 || _use != UseIo;

  _inputs[0] = i1;
  _inputs[1] = i2;
  _inputs[2] = i3;
  _use = UseIo;
  return pinsChanged;
}

void Io::begin(uint32_t ignorePeriodAfterTouchUp, bool oneKeyAtATime) {
  if (_initialized) {
    return;
  }

  _ignorePeriodAfterTouchUp = ignorePeriodAfterTouchUp;

  for (uint8_t i = 0; i < IO_CNT; i++) {
    if (_ledPins[i] != -1) {
      ledcSetup(i, 5000, 8);
      ledcAttachPin(_ledPins[i], i);
    }
  }

  if (_ledRed != -1) {
    ledcSetup(IO_CNT, 5000, 8);
    ledcAttachPin(_ledRed, IO_CNT);
  }

  switch (_use) {
  case UseQt:
    _qt.begin(oneKeyAtATime);
    break;
  case UseIo:
    for (uint8_t i = 0; i < IO_CNT; i++)
      if (_inputs[i] != -1)
        pinMode(_inputs[i], INPUT | PULLUP);
    break;
  }

  _ticker.attach_ms(5, Io::handle, this);

  updateLeds();
  _initialized = true;
}

void Io::handle(Io *instance) {
  Io &io = *instance;
  uint8_t pressed = 0;
  auto now = millis();

  if (io._ignoreEventsStart) {
    if (now <= io._ignoreEventsStart + io._ignorePeriodAfterTouchUp)
      return;
  } else {
    io._ignoreEventsStart = 0;
  }

  if (io._use == UseQt) {
    pressed = io._qt.pressed();

    if (pressed != io._stablePressed) {
      io._stablePressed = pressed;
      io._lastStableChange = now;
      io._stableUpdated = false;
      io._debounce = MSEC(70);
    }
  } else if (io._use == UseIo) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (io._inputs[i] != -1 && digitalRead(io._inputs[i]) == 0) {
        pressed |= (1 << i);
        break;
      }
    }

    if (pressed != io._stablePressed) {
      io._stablePressed = pressed;
      io._lastStableChange = now;
      io._stableUpdated = false;
      io._debounce = MSEC(70);
    }
  }

  if (!io._stableUpdated && now - io._lastStableChange >= io._debounce) {
    io._stableUpdated = true;

    if (io._stablePressed != io._pressed) {

      if (io._touchUp) {
        for (uint8_t i = 0; i < IO_CNT; i++) {
          uint8_t mask = 1 << i;
          if ((io._pressed & mask) && !(io._stablePressed & mask)) {
            // was pressed, but not anymore
            io._touchUp(i);
          }
        }
      }

      if (io._stablePressed == 0) {
        io._ignoreEventsStart = now;
      }

      auto lastPressed = io._pressed;
      io._pressed = io._stablePressed;
      io.updateLeds();

      if (io._touchDown) {
        for (uint8_t i = 0; i < IO_CNT; i++) {
          uint8_t mask = 1 << i;
          if (!(lastPressed & mask) && (io._pressed & mask)) {
            // is pressed now, but was not before
            io._touchDown(i);
          }
        }
      }

      io._lastSentEvent = now;
    }
  }

  if (io._touchPress && io._pressed &&
      now - io._lastSentEvent >= IO_PRESS_REPEAT) {
    io._lastSentEvent = now;
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (io._pressed & (1 << i)) {
        io._touchPress(i);
      }
    }
  }
}

Io &Io::onTouchDown(TouchKeyHandler handler) {
  _touchDown = handler;
  return *this;
}

Io &Io::onTouchPress(TouchKeyHandler handler) {
  _touchPress = handler;
  return *this;
}

Io &Io::onTouchUp(TouchKeyHandler handler) {
  _touchUp = handler;
  return *this;
}

Io &Io::setLedLevels(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t bTouch,
                     uint8_t red) {
  _levelBlue[0] = b1;
  _levelBlue[1] = b2;
  _levelBlue[2] = b3;
  _levelRed = red;
  _levelBlueTouched = bTouch;
  updateLeds();
  return *this;
}

void Io::updateLeds() {
  if (_ledRed != -1) {
    ledcWrite(IO_CNT, _invertLedRed ? (255 - _levelRed) : _levelRed);
  }
  for (uint8_t i = 0; i < IO_CNT; i++) {
    if (_ledPins[i] != -1) {
      ledcWrite(
          i, 255 - (_pressed & (1 << i) ? _levelBlueTouched : _levelBlue[i]));
    }
  }
}

void Io::appendStatus(JsonVariant doc) const {
  auto ioStatus = doc.createNestedArray("io");
  if (_use == UseQt) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      auto value = _qt.signal(i);
      if (value == 0)
        continue;
      auto parent = ioStatus.createNestedObject();
      parent["value"] = value;
      parent["threshold"] = _qt.reference(i);
    }
  } else if (_use == UseIo) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      if (_inputs[i] == -1) {
        continue;
      }
      auto parent = ioStatus.createNestedObject();
      parent["value"] = digitalRead(_inputs[i]);
    }
  }
}