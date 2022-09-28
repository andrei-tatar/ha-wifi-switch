#ifndef _IO_H_
#define _IO_H_

#include "touch.h"
#include "util.h"
#include <Ticker.h>

#define IO_CNT 3
#define IO_DEBOUNCE MSEC(50)

typedef void (*TouchKeyHandler)(int8_t key);
typedef void (*TouchVoidHandler)(void);

class Io {
private:
  int8_t _ledPins[IO_CNT];
  int8_t _ledRed;
  int8_t _lastPressed = -1, _pressed = -1;
  uint32_t _lastChanged;
  bool _updated = false;
  Ticker _ticker;
  static void handle(Io *instance);

  TouchKeyHandler _touchDown, _touchPress;
  TouchVoidHandler _touchUp;

public:
  Io();

  Touch touch[IO_CNT];

  Io &useTouchPins(int8_t pin1, int8_t pin2 = -1, int8_t pin3 = -1);
  Io &useLedPins(int8_t pin1, int8_t pin2 = -1, int8_t pin3 = -1);
  Io &useRedLedPin(int8_t pin);
  Io &onTouchDown(TouchKeyHandler handler);
  Io &onTouchPress(TouchKeyHandler handler);
  Io &onTouchUp(TouchVoidHandler handler);
  void begin();
  void setLedRed(uint8_t level);
};

#endif