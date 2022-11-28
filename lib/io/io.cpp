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

bool Io::useTouchPins(int8_t touch1, int8_t touch2, int8_t touch3) {
  bool pinsChanged = _useQt != false;
  pinsChanged |= _touch[0].usePin(touch1);
  pinsChanged |= _touch[1].usePin(touch2);
  pinsChanged |= _touch[2].usePin(touch3);
  _useQt = false;
  return pinsChanged;
}

bool Io::useQtTouch(int8_t sdaPin, int8_t sclPin, int8_t ch1, int8_t ch2,
                    int8_t ch3) {
  bool pinsChanged = _qt.usePins(sdaPin, sclPin) || _useQt != true;
  _qt.useChannels(ch1, ch2, ch3);
  _useQt = true;
  return pinsChanged;
}

void Io::begin() {
  if (_initialized) {
    return;
  }

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

  if (_useQt) {
    _qt.begin();
  } else {
    for (uint8_t i = 0; i < IO_CNT; i++)
      _touch[i].begin();
  }

  _ticker.attach_ms(5, Io::handle, this);

  updateLeds();
  _initialized = true;
}

void Io::handle(Io *instance) {
  Io &io = *instance;
  int8_t pressed = -1;
  auto now = millis();

  if (io._useQt) {
    pressed = io._qt.pressed();

    io._stablePressed = pressed;
    io._lastStableChange = now;
    io._stableUpdated = false;
    io._debounce = 0; // no need to debounce, qt does it internally
  } else {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      io._touch[i].handle();
      if (pressed == -1 && io._touch[i].isPressed()) {
        pressed = i;
      }
    }

    if (pressed != io._stablePressed) {
      if (io._stablePressed == -1 || pressed == -1) {
        io._debounce = MSEC(50);
      } else {
        io._debounce = MSEC(100);
      }

      io._stablePressed = pressed;
      io._lastStableChange = now;
      io._stableUpdated = false;
    }
  }

  if (!io._stableUpdated && now - io._lastStableChange >= io._debounce) {
    io._stableUpdated = true;

    if (io._stablePressed != io._pressed) {
      if (io._pressed != -1) {
        if (io._touchUp) {
          io._touchUp();
        }
      }

      io._pressed = io._stablePressed;
      io.updateLeds();

      if (io._pressed != -1) {
        if (io._touchDown) {
          io._touchDown(io._pressed);
        }
      }

      io._lastSentEvent = now;
    }
  }

  if (io._pressed != -1 && now - io._lastSentEvent >= IO_PRESS_REPEAT) {
    io._lastSentEvent = now;
    if (io._touchPress) {
      io._touchPress(io._pressed);
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

Io &Io::onTouchUp(TouchVoidHandler handler) {
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
      ledcWrite(i, 255 - (_pressed == i ? _levelBlueTouched : _levelBlue[i]));
    }
  }
}

void Io::appendStatus(JsonVariant doc) const {
  auto touchStatus = doc.createNestedArray("touch");
  if (_useQt) {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      auto value = _qt.signal(i);
      if (value == 0)
        continue;
      auto parent = touchStatus.createNestedObject();
      parent["value"] = value;
      parent["threshold"] = _qt.reference(i);
    }
  } else {
    for (uint8_t i = 0; i < IO_CNT; i++) {
      auto value = _touch[i].getValue();
      if (value == 0)
        continue;
      auto parent = touchStatus.createNestedObject();
      parent["value"] = value;
      parent["threshold"] = _touch[i].getThreshold();
    }
  }
}