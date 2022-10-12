#include "touch.h"
#include "driver/touch_sensor.h"
#include "util.h"

#define TIME_BETWEEN_MEASUREMENTS MSEC(5)
#define FORCE_CALIBRATION_IF_PRESSED_MORE_THAN SECS(30)
#define CALIBRATION_SAMPLES 50
#define MAX_MARGIN_SUCCESS_CALIBRATION 10

void Touch::usePin(int8_t pin) {
  _gpio = pin;
  _channel = digitalPinToTouchChannel(_gpio);
}

void Touch::begin() {
  if (_channel == -1) {
    return;
  }

  static bool _generalInit = false;

  if (!_generalInit) {
    _generalInit = true;

    touch_pad_init();
    touch_pad_set_voltage(TOUCH_HVOLT_2V4, TOUCH_LVOLT_0V8,
                          TOUCH_HVOLT_ATTEN_1V5);

    touch_pad_set_meas_time(0x1000, 0x1000);

    // Touch Sensor Timer initiated
    touch_pad_filter_start(20);
  }

  touch_pad_config((touch_pad_t)_channel, SOC_TOUCH_PAD_THRESHOLD_MAX);
  touch_pad_set_cnt_mode((touch_pad_t)_channel, TOUCH_PAD_SLOPE_MAX,
                         TOUCH_PAD_TIE_OPT_HIGH);

  _forceCalibration = true;
  restartCalibration();

  delay(15);
}

touch_value_t Touch::getThreshold() const { return _threshold; }
touch_value_t Touch::getValue() const { return _value; }
bool Touch::isPressed() const { return _pressed; }

void Touch::handle() {
  if (_channel == -1) {
    return;
  }

  touch_pad_read_filtered((touch_pad_t)_channel, &_value);
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
      auto avg = _calibrationData / _samples;
      auto margin = avg * 2 / 100;
      if (diff <= margin) {
        _threshold = _calibrationData / _samples - margin;
        _forceCalibration = false;
      }

      restartCalibration();
    }
  } else {
    auto now = millis();
    if (!_pressStart) {
      _pressStart = now;
    }

    if (now - _pressStart > FORCE_CALIBRATION_IF_PRESSED_MORE_THAN) {
      _forceCalibration = true;
    } else {
      restartCalibration();
    }
  }
}

void Touch::restartCalibration() {
  if (_samples) {
    _calibrationData = 0;
    _samples = 0;
    _min = 0xFFFF;
    _max = 0;
  }
}