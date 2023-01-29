#include "dimmer.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"
#include "ulp_common.h"

enum Labels {
  Label_Start = 1,
  Label_ReadZeroPinAndWaitHigh,
  Label_ReadZeroPinAndWaitLow,
  Label_SkipDelays,
  Label_LoadDelay,
};

enum Registers {
  Reg_PulseDelay = R1,
  Reg_DelayMemoryLocation = R2,
  Reg_Temp = R3
};

enum Memory {
  Mem_Delay = 0,
  Mem_Program,
};

#define TRIAC_OFF I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 0)
#define TRIAC_ON I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 1)
#define LOAD_DELAY I_MOVR(R0, Reg_PulseDelay)
#define LOAD_DELAY_C                                                           \
  LOAD_DELAY, I_MOVI(Reg_Temp, TICKS - TICKS_BEFORE_OFF),                      \
      I_SUBR(R0, Reg_Temp, R0)

#define TICKS 59247 // number of delay ticks per 10msec
#define TICKS_BEFORE_OFF (TICKS / 20) //.5 msec
#define OFF_TICKS 0xFFFF

#define Delay10k 0
#define Delay1k 1
#define Delay100 2
#define Delay10 3
#define DelayOver 4
#define I(index, l) (index * 10 + l)

#define WAIT(index)                                                            \
  M_LABEL(I(index, Delay10k)), M_BL(I(index, Delay1k), 10000), I_DELAY(15000), \
      I_SUBI(R0, R0, 10000), M_BX(I(index, Delay10k)),                         \
      M_LABEL(I(index, Delay1k)), M_BL(I(index, Delay100), 1000),              \
      I_DELAY(1500), I_SUBI(R0, R0, 1000), M_BX(I(index, Delay1k)),            \
      M_LABEL(I(index, Delay100)), M_BL(I(index, Delay10), 100), I_DELAY(150), \
      I_SUBI(R0, R0, 100), M_BX(I(index, Delay100)),                           \
      M_LABEL(I(index, Delay10)), M_BL(I(index, DelayOver), 10), I_DELAY(10),  \
      I_SUBI(R0, R0, 10), M_BX(I(index, Delay10)),                             \
      M_LABEL(I(index, DelayOver))

#define WAIT_FOR_HIGH                                                          \
  M_LABEL(Label_ReadZeroPinAndWaitHigh),                                       \
      I_RD_REG(RTC_GPIO_IN_REG, _zeroIo, _zeroIo),                             \
      M_BL(Label_ReadZeroPinAndWaitHigh, 1)

#define WAIT_FOR_LOW                                                           \
  M_LABEL(Label_ReadZeroPinAndWaitLow),                                        \
      I_RD_REG(RTC_GPIO_IN_REG, _zeroIo, _zeroIo),                             \
      M_BGE(Label_ReadZeroPinAndWaitLow, 1)

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

bool Dimmer::usePins(int8_t pinZero, int8_t pinTriac) {
  bool pinsChanged = _pinZero != pinZero || _pinTriac != pinTriac;
  if (rtc_gpio_is_valid_gpio((gpio_num_t)pinZero) &&
      rtc_gpio_is_valid_gpio((gpio_num_t)pinTriac)) {
    _pinZero = pinZero;
    _pinTriac = pinTriac;
  }
  return pinsChanged;
}

void Dimmer::begin() {
  if (_pinTriac == -1 || _pinZero == -1) {
    return;
  }

  rtc_gpio_init((gpio_num_t)_pinTriac);
  rtc_gpio_set_direction((gpio_num_t)_pinTriac, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level((gpio_num_t)_pinTriac, 0);

  rtc_gpio_init((gpio_num_t)_pinZero);
  rtc_gpio_set_direction((gpio_num_t)_pinZero, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)_pinZero);

  uint32_t _triacIo =
      RTC_GPIO_OUT_DATA_S + rtc_io_number_get((gpio_num_t)_pinTriac);
  uint32_t _zeroIo =
      RTC_GPIO_OUT_DATA_S + rtc_io_number_get((gpio_num_t)_pinZero);

  _ticker.attach_ms(25, Dimmer::handle, this);

  // const ulp_insn_t program[] = {
  //     TRIAC_OFF, I_MOVI(Reg_DelayMemoryLocation, Mem_Delay),

  //     M_LABEL(Label_Start),

  //     WAIT_FOR_HIGH,                      // wait zero cross to become 1
  //     LOAD_DELAY,                         // load delay into R0
  //     M_BGE(Label_SkipDelays, OFF_TICKS), // skip pulsing if off

  //     WAIT(1),      // wait
  //     TRIAC_ON,     // triac on
  //     LOAD_DELAY_C, // load (10msec-delay) into R0
  //     WAIT(2),      // wait
  //     TRIAC_OFF,    // triac off

  //     I_DELAY(TICKS_BEFORE_OFF), // wait until next half period

  //     LOAD_DELAY,   // load delay into R0
  //     WAIT(3),      // wait
  //     TRIAC_ON,     // triac on
  //     LOAD_DELAY_C, // load (10msec-delay) into R0
  //     WAIT(4),      // wait
  //     TRIAC_OFF,    // triac off

  //     M_BX(Label_LoadDelay),

  //     M_LABEL(Label_SkipDelays),
  //     WAIT_FOR_LOW, // wait for 0 (when dimmer is off)

  //     // load the delay time from memory
  //     M_LABEL(Label_LoadDelay),
  //     I_LD(Reg_PulseDelay, Reg_DelayMemoryLocation, 0),

  //     // start over again
  //     M_BX(Label_Start),

  //     I_HALT()};

  const ulp_insn_t program[] = {
      TRIAC_OFF,
      I_MOVI(Reg_DelayMemoryLocation, Mem_Delay),

      M_LABEL(Label_Start),

      I_LD(Reg_PulseDelay, Reg_DelayMemoryLocation, 0),
      LOAD_DELAY,
      M_BGE(Label_SkipDelays, OFF_TICKS), // skip pulsing if off
      TRIAC_ON,                           // triac on
      M_BX(Label_Start),

      M_LABEL(Label_SkipDelays),
      TRIAC_OFF,
      M_BX(Label_Start),

      I_HALT()};

  RTC_SLOW_MEM[Mem_Delay] = OFF_TICKS;
  size_t load_addr = Mem_Program;
  size_t size = sizeof(program) / sizeof(ulp_insn_t);
  ulp_process_macros_and_load(load_addr, program, &size);
  ulp_run(load_addr);
}

void Dimmer::handle(Dimmer *instance) {
  Dimmer &dimmer = *instance;
  if (dimmer._minBrightnessUntil && millis() > dimmer._minBrightnessUntil) {
    dimmer._minBrightnessUntil = 0;
    dimmer._minBrightness = 1;
  }

  auto targetBrightness = dimmer._on
                              ? max(dimmer._minBrightness, dimmer._brightness)
                              : dimmer._minBrightness;
  if (dimmer._currentBrightness != targetBrightness) {
    if (dimmer._currentBrightness < targetBrightness)
      dimmer._currentBrightness++;
    else
      dimmer._currentBrightness--;

    RTC_SLOW_MEM[Mem_Delay] =
        dimmer._on || dimmer._minBrightnessUntil ? 1 : OFF_TICKS;
    // RTC_SLOW_MEM[Mem_Delay] =
    //     dimmer._on || dimmer._minBrightnessUntil ||
    //             dimmer._currentBrightness != targetBrightness
    //         ? dimmer._curve[dimmer._currentBrightness - 1] * TICKS / 10000
    //         : OFF_TICKS;
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
