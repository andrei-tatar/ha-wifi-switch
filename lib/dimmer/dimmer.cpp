#include "dimmer.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"
#include "ulp_common.h"

/*

HW changes to the off-the-shelf dimmer:
- add bridge rectifier for zero detection (simpler code to handle each zero cross, instead of half of them)
- change bridge resistor to 200k (from 150k) for better sensitivity

*/

enum Labels {
  Label_Start = 1,
  Label_ReadZeroPinAndWaitHigh,
  Label_ReadZeroPinAndWaitLow,
  Label_SkipDelays,
  Label_ReloadDelayLow,
  Label_ReloadDelayHigh,
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

#define TRIAC_PULSE_LENGTH 1000 // ~ 116 uS

#define TICKS 59247                   // number of delay ticks per 10msec
#define TICKS_BEFORE_OFF (TICKS / 20) //.5 msec
#define OFF_TICKS 0xFFFF

enum DelayLabel {
  Label_Delay10k = 0,
  Label_Delay1k,
  Label_Delay100,
  Label_DelayOver,
};
#define I(l) (10 + l)

#define WAIT_TRIAC_TRIGGER M_LABEL(I(Label_Delay10k)),    \
                           M_BL(I(Label_Delay1k), 10000), \
                           I_DELAY(15000),                \
                           I_SUBI(R0, R0, 10000),         \
                           M_BX(I(Label_Delay10k)),       \
                           M_LABEL(I(Label_Delay1k)),     \
                           M_BL(I(Label_Delay100), 1000), \
                           I_DELAY(1500),                 \
                           I_SUBI(R0, R0, 1000),          \
                           M_BX(I(Label_Delay1k)),        \
                           M_LABEL(I(Label_Delay100)),    \
                           M_BL(I(Label_DelayOver), 100), \
                           I_DELAY(150),                  \
                           I_SUBI(R0, R0, 100),           \
                           M_BX(I(Label_Delay100)),       \
                           M_LABEL(I(Label_DelayOver)),   \
                           I_DELAY(50)

#define WAIT_FOR(debounce, BASE, BRANCH)           \
  M_LABEL(Label_ReloadDelay##BASE),                \
      I_MOVI(Reg_Temp, debounce),                  \
      M_LABEL(Label_ReadZeroPinAndWait##BASE),     \
      I_RD_REG(RTC_GPIO_IN_REG, _zeroIo, _zeroIo), \
      BRANCH(Label_ReloadDelay##BASE, 1),          \
      I_SUBI(Reg_Temp, Reg_Temp, 1),               \
      I_MOVR(R0, Reg_Temp),                        \
      M_BGE(Label_ReadZeroPinAndWait##BASE, 1)

#define WAIT_FOR_HIGH(debounce) WAIT_FOR(debounce, High, M_BL)
#define WAIT_FOR_LOW(debounce) WAIT_FOR(debounce, Low, M_BGE)

// #define WAIT_FOR_HIGH M_LABEL(Label_ReadZeroPinAndWaitHigh),       \
//                       I_RD_REG(RTC_GPIO_IN_REG, _zeroIo, _zeroIo), \
//                       M_BL(Label_ReadZeroPinAndWaitHigh, 1)

// #define WAIT_FOR_LOW M_LABEL(Label_ReadZeroPinAndWaitLow),        \
//                      I_RD_REG(RTC_GPIO_IN_REG, _zeroIo, _zeroIo), \
//                      M_BGE(Label_ReadZeroPinAndWaitLow, 1)

Dimmer::Dimmer() : _pinZero(-1),
                   _pinTriac(-1), _brightness(100), _currentBrightness(0),
                   _minBrightness(1),
                   _on(false), _curve{6500, 6480, 6460, 6440, 6420, 6400, 6380, 6360, 6340,
                                      6320, 6300, 6280, 6260, 6240, 6220, 6200, 6180, 6160,
                                      6140, 6120, 6100, 6080, 6060, 6040, 6020, 6000, 5980,
                                      5960, 5940, 5920, 5900, 5880, 5860, 5840, 5820, 5800,
                                      5780, 5760, 5740, 5720, 5700, 5680, 5660, 5640, 5620,
                                      5595, 5543, 5491, 5436, 5380, 5322, 5262, 5200, 5137,
                                      5072, 5005, 4936, 4865, 4792, 4718, 4641, 4563, 4483,
                                      4400, 4316, 4230, 4141, 4051, 3958, 3864, 3767, 3669,
                                      3568, 3465, 3360, 3253, 3144, 3032, 2919, 2803, 2685,
                                      2564, 2442, 2317, 2190, 2060, 1928, 1794, 1658, 1519,
                                      1378, 1235, 1089, 941, 790, 637, 481, 323, 163, 0} {
}

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

  _ticker.attach_ms(10, Dimmer::handle, this);

  pinMode(_pinTriac, OUTPUT);

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

  const ulp_insn_t program[] = {
      TRIAC_OFF,                                        // start with triac off
      I_MOVI(Reg_DelayMemoryLocation, Mem_Delay),       // load delay location
      M_LABEL(Label_Start),                             // ---- start:
      WAIT_FOR_LOW(10),                                 // wait zero cross
      I_MOVR(R0, Reg_PulseDelay),                       // load delay into R0
      M_BGE(Label_SkipDelays, OFF_TICKS),               // skip pulsing if off
      WAIT_TRIAC_TRIGGER,                               // wait for delay before turning on triac
      TRIAC_ON,                                         // triac on
      I_DELAY(TRIAC_PULSE_LENGTH),                      // wait for pulse length
      M_LABEL(Label_SkipDelays),                        // skip here, when off
      TRIAC_OFF,                                        // triac off
      I_LD(Reg_PulseDelay, Reg_DelayMemoryLocation, 0), // load the delay time from memory
      WAIT_FOR_HIGH(20),                                // wait until period ends
      M_BX(Label_Start)                                 // ---- back to start ^
  };

  RTC_SLOW_MEM[Mem_Delay] = OFF_TICKS;
  size_t size = sizeof(program) / sizeof(ulp_insn_t);
  ulp_process_macros_and_load(Mem_Program, program, &size);
  ulp_run(Mem_Program);
}

void Dimmer::handle(Dimmer *instance) {
  Dimmer &dimmer = *instance;
  if (dimmer._minBrightnessUntil && millis() > dimmer._minBrightnessUntil) {
    dimmer._minBrightnessUntil = 0;
    dimmer._minBrightness = 1;
  }

  auto targetBrightness = dimmer._on
                              ? max(dimmer._minBrightness,
                                    dimmer._brightness)
                              : dimmer._minBrightness;

  if (dimmer._currentBrightness != targetBrightness) {
    if (dimmer._currentBrightness < targetBrightness)
      dimmer._currentBrightness++;
    else
      dimmer._currentBrightness--;
  }

  uint16_t desiredTicks = dimmer._on || dimmer._minBrightnessUntil ||
                                  dimmer._currentBrightness != targetBrightness
                              ? dimmer._curve[dimmer._currentBrightness - 1] * TICKS / 10000
                              : OFF_TICKS;
  static uint16_t lastTicks = 0;

  if (desiredTicks != lastTicks) {
    lastTicks = desiredTicks;
    RTC_SLOW_MEM[Mem_Delay] = desiredTicks;
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
