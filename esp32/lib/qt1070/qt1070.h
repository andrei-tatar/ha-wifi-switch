#ifndef _QT_1070_H_
#define _QT_1070_H_

#include <Arduino.h>
#include <Wire.h>

#define CH_COUNT 3

class Qt1070 {
private:
  TwoWire &_wire;
  int8_t _sda = -1, _scl = -1;
  int8_t _channels[CH_COUNT];
  bool _initialized = false;

  void writeRegister(uint8_t address, uint8_t value) const;
  uint8_t readRegister(uint8_t address) const;
  uint16_t readRegisterU16(uint8_t address) const;

public:
  Qt1070(TwoWire &wire);

  bool usePins(int8_t sda, int8_t scl);
  void useChannels(int8_t ch1 = -1, int8_t ch2 = -1, int8_t ch3 = -1);
  void begin(bool oneKeyAtATime = true);
  void calibrate() const;
  uint8_t pressed() const;
  uint16_t signal(uint8_t index) const;
  uint16_t reference(uint8_t index) const;
};

#endif