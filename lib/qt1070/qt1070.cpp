#include "qt1070.h"

#define QT_ADDR 0x1B

#define REG_CHIP_ID 0
#define REG_KEY_STATUS 3
#define REG_SIGNAL 4
#define REG_REFERENCE 18
#define REG_NTHR 32
#define REG_AVE 39
#define REG_DI 46
#define REG_MAXCAL_GUARD 53
#define REG_LP_MODE 54
#define REG_MAX_ON 55
#define REG_CALIBRATE 56
#define REG_RESET 57

Qt1070::Qt1070(TwoWire &wire) : _wire(wire) {}

bool Qt1070::usePins(int8_t sda, int8_t scl) {
  bool pinsChanged = _sda != sda || _scl != scl;
  _sda = sda;
  _scl = scl;
  return pinsChanged;
}

void Qt1070::begin(bool oneKeyAtATime) {
  _wire.begin(_sda, _scl);

  writeRegister(REG_RESET, 0xFF); // reset
  delay(500);                     // wait for device to reset

  _initialized = readRegister(REG_CHIP_ID) == 0x2E;
  if (!_initialized) {
    return;
  }

  // disable guard channel
  uint8_t currentValue = readRegister(REG_MAXCAL_GUARD);
  writeRegister(REG_MAXCAL_GUARD, currentValue | 0x0F);
  writeRegister(REG_LP_MODE, 16); // 128 msec
  writeRegister(REG_MAX_ON, 38);  // ~10 sec

  for (uint8_t i = 0; i < 7; i++) {
    bool chEnabled = false;
    for (uint8_t j = 0; j < CH_COUNT; j++) {
      if (_channels[j] == i) {
        chEnabled = true;
        break;
      }
    }

    if (chEnabled) {
      // enable channel
      writeRegister(REG_AVE + i,
                    (32 << 2) | (oneKeyAtATime ? 1 : 0)); // samples + group
      writeRegister(REG_NTHR + i, 8);                     // threshold
    } else {
      // disable channel
      writeRegister(REG_AVE + i, 0);
    }
  }
}

void Qt1070::writeRegister(uint8_t address, uint8_t value) const {
  _wire.beginTransmission(QT_ADDR);
  _wire.write(address);
  _wire.write(value);
  _wire.endTransmission();
}

uint8_t Qt1070::readRegister(uint8_t address) const {
  _wire.beginTransmission(QT_ADDR);
  _wire.write(address);
  _wire.endTransmission();

  _wire.requestFrom(QT_ADDR, 1U);
  uint8_t value = _wire.read();
  return value;
}

uint16_t Qt1070::readRegisterU16(uint8_t address) const {
  _wire.beginTransmission(QT_ADDR);
  _wire.write(address);
  _wire.endTransmission();

  _wire.requestFrom(QT_ADDR, 2U);
  uint8_t value = _wire.read() << 8 | _wire.read();
  return value;
}

void Qt1070::useChannels(int8_t ch1, int8_t ch2, int8_t ch3) {
  _channels[0] = ch1;
  _channels[1] = ch2;
  _channels[2] = ch3;
}

void Qt1070::calibrate() const {
  if (!_initialized) {
    return;
  }

  writeRegister(REG_CALIBRATE, 0xFF);
}

uint8_t Qt1070::pressed() const {
  if (!_initialized) {
    return 0;
  }

  uint8_t status = readRegister(REG_KEY_STATUS);
  if (status == 0) {
    return 0;
  }

  uint8_t pressed = 0;
  for (uint8_t i = 0; i < CH_COUNT; i++) {
    if (_channels[i] != -1 && (status & (1 << _channels[i]))) {
      pressed |= 1 << i;
    }
  }

  return pressed;
}

uint16_t Qt1070::signal(uint8_t index) const {
  if (!_initialized) {
    return 0;
  }

  int8_t channel = _channels[index];
  if (channel == -1) {
    return 0;
  }
  return readRegisterU16(REG_SIGNAL + channel * 2);
}

uint16_t Qt1070::reference(uint8_t index) const {
  if (!_initialized) {
    return 0;
  }

  int8_t channel = _channels[index];
  if (channel == -1) {
    return 0;
  }
  return readRegisterU16(REG_REFERENCE + channel * 2);
}
