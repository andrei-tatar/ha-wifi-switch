#ifndef _IO_H_
#define _IO_H_

#include "qt1070.h"
#include "touch.h"
#include "util.h"
#include <ArduinoJson.h>
#include <Ticker.h>

#define IO_CNT 3

typedef std::function<void(int8_t key)> TouchKeyHandler;
typedef std::function<void(void)> TouchVoidHandler;

enum Use { UseNone, UseTouch, UseQt, UseIo };

class Io {
private:
  Qt1070 _qt;
  Use _use = UseNone;
  Touch _touch[IO_CNT];
  int8_t _inputs[IO_CNT];
  int8_t _ledPins[IO_CNT];
  int8_t _ledRed;
  bool _invertLedRed, _stableUpdated;
  int8_t _pressed = -1, _stablePressed = -1, _debounce;
  uint32_t _lastSentEvent, _lastStableChange, _ignoreEventsStart;
  Ticker _ticker;
  static void handle(Io *instance);
  uint8_t _levelBlue[IO_CNT], _levelBlueTouched, _levelRed;
  bool _initialized = false;

  TouchKeyHandler _touchDown, _touchPress;
  TouchVoidHandler _touchUp;
  void updateLeds();

public:
  Io();

  bool useLedPins(int8_t led1, int8_t led2, int8_t led3, int8_t redLed,
                  bool invertRedLed);

  bool useTouchPins(int8_t touch1, int8_t touch2, int8_t touch3);
  bool useQtTouch(int8_t sdaPin, int8_t sclPin, int8_t ch1, int8_t ch2,
                  int8_t ch3);
  bool useInputPins(int8_t i1, int8_t i2, int8_t i3);

  Io &setLedLevels(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t bTouch,
                   uint8_t red);
  Io &onTouchDown(TouchKeyHandler handler);
  Io &onTouchPress(TouchKeyHandler handler);
  Io &onTouchUp(TouchVoidHandler handler);
  void begin();
  void appendStatus(JsonVariant doc) const;
};

#endif