#include "io.h"

#define IO_DEBOUNCE MSEC(50)

Io::Io() {
  for (uint8_t i = 0; i < IO_CNT; i++)
    _ledPins[i] = -1;
}

Io &Io::useTouchPins(int8_t pin1, int8_t pin2, int8_t pin3) {
  touch[0].usePin(pin1);
  touch[1].usePin(pin2);
  touch[2].usePin(pin3);
  return *this;
}

Io &Io::useLedPins(int8_t pin1, int8_t pin2, int8_t pin3) {
  _ledPins[0] = pin1;
  _ledPins[1] = pin2;
  _ledPins[2] = pin3;
  return *this;
}

Io &Io::useRedLedPin(int8_t pin, bool invert) {
  _ledRed = pin;
  _invertLedRed = invert;
  return *this;
}

void Io::begin() {
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

  for (uint8_t i = 0; i < IO_CNT; i++)
    touch[i].begin();

  _ticker.attach_ms(5, Io::handle, this);
}

void Io::handle(Io *instance) {
  Io &io = *instance;
  int8_t pressed = -1;
  auto now = millis();

  for (uint8_t i = 0; i < IO_CNT; i++) {
    io.touch[i].handle();
    if (pressed == -1 && io.touch[i].isPressed()) {
      pressed = i;
    }
  }

  if (pressed != io._lastPressed) {
    io._lastPressed = pressed;
    io._lastChanged = now;
    io._updated = false;
  } else if (now - io._lastChanged > IO_DEBOUNCE) {
    if (!io._updated) {
      io._pressed = pressed;
      io._updated = true;
      io._lastChanged = now;
      if (io._pressed == -1) {
        if (io._touchUp) {
          io._touchUp();
        }
      } else {
        if (io._touchDown) {
          io._touchDown(pressed);
        }
      }

      io.updateLeds();
    } else if (io._pressed != -1) {
      io._lastChanged = now;
      if (io._touchPress) {
        io._touchPress(io._pressed);
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