#include "touch.h"
#include "driver/touch_sensor.h"
#include "util.h"

#define TIME_BETWEEN_MEASUREMENTS MSEC(5)
#define FORCE_CALIBRATION_IF_PRESSED_MORE_THAN SECS(30)
#define CALIBRATION_SAMPLES 20
#define MAX_MARGIN_SUCCESS_CALIBRATION 10

void Touch::usePin(int8_t pin) {
  _gpio = pin;
  _channel = digitalPinToTouchChannel(_gpio);
}

void Touch::begin() {
  if (_channel == -1) {
    return;
  }

  touchRead(_gpio); // touch is inited only after 1st read

  touch_pad_set_cnt_mode((touch_pad_t)_channel, TOUCH_PAD_SLOPE_MAX,
                         TOUCH_PAD_TIE_OPT_HIGH);
  touch_pad_set_voltage(TOUCH_HVOLT_2V4, TOUCH_LVOLT_0V8,
                        TOUCH_HVOLT_ATTEN_1V5);

  restartCalibration();
}

bool Touch::handle() {
  if (_channel == -1) {
    return false;
  }

  auto now = millis();
  if (now >= _nextMeasurement) {
    _nextMeasurement = now + TIME_BETWEEN_MEASUREMENTS;
    touch_pad_read_raw_data((touch_pad_t)_channel, &_value);
  } else {
    return true;
  }

  _pressed = _value < _threshold;

  if (!_pressed || _forceCalibration) {
    _pressStart = 0;
    _calibrationData += _value;
    _samples++;
    if (_value > _max) {
      _max = _value;
    }
    if (_value < _min) {
      _min = _value;
    }

    if (_samples == CALIBRATION_SAMPLES) {
      auto diff = _max - _min;
      if (diff <= MAX_MARGIN_SUCCESS_CALIBRATION) {
        auto margin = max(10, diff) * 2;
        _threshold = _calibrationData / _samples - margin;
        _forceCalibration = false;
      }

      restartCalibration();
    }
  } else {
    if (!_pressStart) {
      _pressStart = now;
    }

    if (now - _pressStart > FORCE_CALIBRATION_IF_PRESSED_MORE_THAN) {
      _forceCalibration = true;
    } else {
      restartCalibration();
    }
  }

  return true;
}

void Touch::restartCalibration() {
  if (_samples) {
    _calibrationData = 0;
    _samples = 0;
    _min = 0xFFFF;
    _max = 0;
  }
}