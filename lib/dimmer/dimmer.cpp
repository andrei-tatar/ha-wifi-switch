#include "dimmer.h"
#include <Wire.h>

Dimmer::Dimmer()
    : _pinZero(-1), _pinTriac(-1), _brightness(100), _currentBrightness(0),
      _minBrightness(1),
      _on(false), _curve{8998, 8991, 8980, 8965, 8945, 8921, 8893, 8860, 8823,
                         8782, 8737, 8688, 8634, 8576, 8515, 8450, 8380, 8307,
                         8231, 8150, 8066, 7979, 7888, 7794, 7697, 7596, 7493,
                         7387, 7277, 7166, 7051, 6934, 6815, 6694, 6570, 6445,
                         6317, 6188, 6057, 5925, 5792, 5657, 5521, 5384, 5246,
                         5108, 4969, 4829, 4690, 4550, 4410, 4271, 4131, 3992,
                         3854, 3716, 3579, 3443, 3308, 3175, 3043, 2912, 2783,
                         2655, 2530, 2406, 2285, 2166, 2049, 1934, 1823, 1713,
                         1607, 1504, 1403, 1306, 1212, 1121, 1034, 950,  869,
                         793,  720,  650,  585,  524,  466,  412,  363,  318,
                         277,  240,  207,  179,  155,  135,  120,  109,  102,
                         100} {}

bool Dimmer::usePins(int8_t pinZero, int8_t pinTriac) { return false; }

void Dimmer::begin() { _ticker.attach_ms(15, Dimmer::handle, this); }

void Dimmer::handle(Dimmer *instance) {
  Dimmer &dimmer = *instance;
  if (dimmer._minBrightnessUntil && millis() > dimmer._minBrightnessUntil) {
    dimmer._minBrightnessUntil = 0;
    dimmer._minBrightness = 1;
  }

  auto targetBrightness = dimmer._on
                              ? max(dimmer._minBrightness, dimmer._brightness)
                              : dimmer._minBrightness;
  if (dimmer._currentBrightness != targetBrightness ||
      dimmer._on != dimmer._currentOn) {
    if (dimmer._currentBrightness < targetBrightness)
      dimmer._currentBrightness++;
    else
      dimmer._currentBrightness--;

    dimmer._currentOn = dimmer._on;
    uint16_t ticks = dimmer._on || dimmer._minBrightnessUntil ||
                             dimmer._currentBrightness != targetBrightness
                         ? dimmer._curve[dimmer._currentBrightness - 1]
                         : 0xFFFF;

    Wire.beginTransmission(0x36);
    Wire.write(ticks >> 8);
    Wire.write(ticks & 0xFF);
    Wire.endTransmission();
  }
}

void Dimmer::toggle() { setOn(!_on); }

uint8_t Dimmer::getBrightness() const { return _brightness; }

bool Dimmer::isOn() const { return _on; }

void Dimmer::changeBrightness(int8_t delta) {
  int16_t newBrightness = max(0, _currentBrightness + delta);
  setBrightness(newBrightness);
}

void Dimmer::setBrightness(uint8_t brightness) {
  if (brightness < 1) {
    brightness = 1;
  }
  if (brightness > 100) {
    brightness = 100;
  }
  if (_brightness != brightness) {
    _brightness = brightness;
    raiseStateChanged();
  }
}

void Dimmer::setOn(bool on) {
  if (_on != on) {
    _on = on;
    raiseStateChanged();
  }
}

void Dimmer::setBrightnessCurve(const uint16_t *curve) {
  memcpy(_curve, curve, 100 * sizeof(uint16_t));
}

void Dimmer::onStateChanged(DimmerStateChangedHandler handler) {
  _handler = handler;
}

void Dimmer::raiseStateChanged() {
  if (_handler) {
    _handler(_on, _brightness);
  }
}

void Dimmer::setMinBrightnessFor(uint8_t brightness, uint16_t timeoutSec) {
  if (brightness < 1) {
    brightness = 1;
  }
  if (brightness > 100) {
    brightness = 100;
  }
  if (timeoutSec) {
    _minBrightness = brightness;
    _minBrightnessUntil = millis() + timeoutSec * 1000;
  } else {
    _minBrightness = 1;
    _minBrightnessUntil = 0;
  }
}
