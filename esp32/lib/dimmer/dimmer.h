#ifndef _DIMMER_H_
#define _DIMMER_H_

#include <Arduino.h>
#include <Ticker.h>

typedef std::function<void(bool on, uint8_t brightness)>
    DimmerStateChangedHandler;

class Dimmer {
  int8_t _pinZero = -1, _pinTriac = -1;
  uint8_t _brightness = 100;
  uint8_t _currentBrightness = 0;
  uint8_t _minBrightness = 1;
  bool _on = false;
  bool _currentOn = 0xFF;
  Ticker _ticker;
  static void handle(Dimmer *instance);
  uint16_t _curve[100];
  DimmerStateChangedHandler _handler;
  uint32_t _minBrightnessUntil = 0;

  void raiseStateChanged();

public:
  Dimmer();
  bool usePins(int8_t pinZero, int8_t pinTriac);
  void begin();

  uint8_t getBrightness() const;
  bool isOn() const;

  void toggle();
  void changeBrightness(int8_t delta);
  void setBrightness(uint8_t brightness);
  void setOn(bool on);
  void setMinBrightnessFor(uint8_t brightness, uint16_t timeoutSec);
  void setBrightnessCurve(const uint16_t *curve);

  void onStateChanged(DimmerStateChangedHandler handler);
};

#endif