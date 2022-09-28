#ifndef _TOUCH_H_
#define _TOUCH_H_

#include <Arduino.h>

class Touch {
private:
  touch_value_t _threshold, _value, _min, _max;
  int8_t _gpio, _channel = -1;
  uint32_t _calibrationData;
  uint8_t _samples;
  bool _pressed, _forceCalibration;
  uint32_t _nextMeasurement, _pressStart;
  void restartCalibration();

public:
  void usePin(int8_t pin);

  inline bool pressed() { return _pressed; }
  inline touch_value_t getThreshold() { return _threshold; }
  inline touch_value_t getValue() { return _value; }

  void begin();
  bool handle();
};

#endif