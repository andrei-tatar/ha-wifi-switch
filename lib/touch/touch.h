#ifndef _TOUCH_H_
#define _TOUCH_H_

#include <Arduino.h>

class Touch {
private:
  touch_value_t _threshold, _value, _min, _max;
  int8_t _gpio, _channel = -1;
  uint32_t _calibrationData;
  uint8_t _samples;
  bool _forceCalibration, _pressed;
  uint32_t _pressStart;
  void restartCalibration();

public:
  bool usePin(int8_t pin);

  touch_value_t getThreshold() const;
  touch_value_t getValue() const;
  bool isPressed() const;

  void begin();
  void handle();
};

#endif