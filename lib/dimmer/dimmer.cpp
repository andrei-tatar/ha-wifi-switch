#include "dimmer.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp32/ulp.h"
#include "ulp_common.h"

enum Labels {
  Label_Start = 1,
  Label_ReadZeroPinAndWaitForHigh,
  Label_ReadZeroPinAndWaitForLow,
  Label_LoadDelay,
  Label_Delay_10k,
  Label_Delay_1k,
  Label_Delay_100,
  Label_Delay_10,
  Label_Delay_Over,
  Label_PulseTriac,
  Label_PulseTriacLonger,
};

enum Registers { Reg_PulseDelay = R1, Reg_DelayMemoryLocation = R2 };

enum Memory {
  Mem_Delay = 0,
  Mem_Program,
};

#define TICKS 58600 // number of delay ticks per 10msec
#define OFF_TICKS 0xFFFF
#define TRIAC_PULSE_LENGTH 1000 // ~ 116 uS
#define TRIAC_LONG_PULSE_LENGTH (TICKS / 4)

Dimmer::Dimmer()
    : _pinZero(-1), _pinTriac(-1), _brightness(100), _currentBrightness(0),
      _minBrightness(1), _maxBrightness(100),
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

Dimmer &Dimmer::usePins(int8_t pinZero, int8_t pinTriac) {
  if (rtc_gpio_is_valid_gpio((gpio_num_t)pinZero) &&
      rtc_gpio_is_valid_gpio((gpio_num_t)pinTriac)) {
    _pinZero = pinZero;
    _pinTriac = pinTriac;
  }
  return *this;
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

  const ulp_insn_t program[] = {
      I_MOVI(Reg_DelayMemoryLocation, Mem_Delay),

      M_LABEL(Label_Start),

      // wait for 1
      M_LABEL(Label_ReadZeroPinAndWaitForHigh),
      I_RD_REG(RTC_GPIO_IN_REG, _zeroIo, _zeroIo),
      M_BL(Label_ReadZeroPinAndWaitForHigh, 1),

      // load delay in R0
      I_MOVR(R0, Reg_PulseDelay),

      // skip pulsing the triac if OFF
      M_BGE(Label_LoadDelay, OFF_TICKS),

      // wait for the specified delay
      M_LABEL(Label_Delay_10k), M_BL(Label_Delay_1k, 10000), I_DELAY(15000),
      I_SUBI(R0, R0, 10000), M_BX(Label_Delay_10k),

      M_LABEL(Label_Delay_1k), M_BL(Label_Delay_100, 1000), I_DELAY(1500),
      I_SUBI(R0, R0, 1000), M_BX(Label_Delay_1k),

      M_LABEL(Label_Delay_100), M_BL(Label_Delay_10, 100), I_DELAY(150),
      I_SUBI(R0, R0, 100), M_BX(Label_Delay_100),

      M_LABEL(Label_Delay_10), M_BL(Label_Delay_Over, 10), I_DELAY(10),
      I_SUBI(R0, R0, 10), M_BX(Label_Delay_10),

      M_LABEL(Label_Delay_Over),

      // load delay again
      I_MOVR(R0, Reg_PulseDelay),
      M_BL(Label_PulseTriacLonger, TRIAC_LONG_PULSE_LENGTH),

      // pulse the TRIAC
      M_LABEL(Label_PulseTriac),
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 1),
      I_DELAY(TRIAC_PULSE_LENGTH),
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 0),

      // delay so next pulse is in 10msec
      I_DELAY(65535), I_DELAY(20451),

      // pulse the TRIAC again
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 1),
      I_DELAY(TRIAC_PULSE_LENGTH),
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 0), M_BX(Label_LoadDelay),

      // pulse the TRIAC with longer hold time
      M_LABEL(Label_PulseTriacLonger),
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 1),
      I_DELAY(TRIAC_LONG_PULSE_LENGTH),
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 0),

      // delay so next pulse is in 10msec
      I_DELAY(65535 - TRIAC_LONG_PULSE_LENGTH + TRIAC_PULSE_LENGTH),
      I_DELAY(20451),

      // pulse the TRIAC again with longer hold time
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 1),
      I_DELAY(TRIAC_LONG_PULSE_LENGTH),
      I_WR_REG(RTC_GPIO_OUT_REG, _triacIo, _triacIo, 0),

      M_LABEL(Label_LoadDelay),

      // load the delay time from memory
      I_LD(Reg_PulseDelay, Reg_DelayMemoryLocation, 0),

      // wait for 0
      M_LABEL(Label_ReadZeroPinAndWaitForLow),
      I_RD_REG(RTC_GPIO_IN_REG, _zeroIo, _zeroIo),
      M_BGE(Label_ReadZeroPinAndWaitForLow, 1),

      // start over again
      M_BX(Label_Start),

      I_HALT()};

  RTC_SLOW_MEM[Mem_Delay] = OFF_TICKS;
  size_t load_addr = Mem_Program;
  size_t size = sizeof(program) / sizeof(ulp_insn_t);
  ulp_process_macros_and_load(load_addr, program, &size);
  ulp_run(load_addr);

  _initialized = true;
}

void Dimmer::handle(Dimmer *instance) {
  Dimmer &dimmer = *instance;
  auto targetBrightness =
      dimmer._on ? dimmer._brightness : dimmer._minBrightness - 1;
  if (dimmer._currentBrightness != targetBrightness) {
    if (dimmer._currentBrightness < targetBrightness)
      dimmer._currentBrightness++;
    else
      dimmer._currentBrightness--;

    RTC_SLOW_MEM[Mem_Delay] =
        dimmer._on || dimmer._currentBrightness != targetBrightness
            ? dimmer._curve[dimmer._currentBrightness - 1] * TICKS / 10000
            : OFF_TICKS;
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
  auto newBrightness = max(_minBrightness, min(_maxBrightness, brightness));
  if (_brightness != newBrightness) {
    _brightness = newBrightness;
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

void Dimmer::setMinMax(uint8_t min, uint8_t max) {
  if (min < 1)
    min = 1;
  if (max > 100)
    max = 100;

  _minBrightness = min;
  _maxBrightness = max;

  if (_brightness < _minBrightness) {
    _brightness = _minBrightness;
    raiseStateChanged();
  } else if (_brightness > _maxBrightness) {
    _brightness = _maxBrightness;
    raiseStateChanged();
  }
}

void Dimmer::onStateChanged(DimmerStateChangedHandler handler) {
  _handler = handler;
}

void Dimmer::raiseStateChanged() {
  if (_handler) {
    _handler(_on, _brightness);
  }
}

bool Dimmer::isInitialized() const { return _initialized; }