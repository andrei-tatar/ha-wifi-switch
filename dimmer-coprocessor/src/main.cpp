#include <avr/interrupt.h>
#include <avr/io.h>
#include <util/delay.h> // needed for our delay

#define I2C_ADDR 0x36 // I2C address of the device
#define PIN_ZERO 6
#define PIN_TRIAC 7
// PA1 - SDA
// PA2 - SCL

#define TIMER_PRESCALER 2 // actual is 2^TIMER_PRESCALER, so 4
#define FREQ 50
#define TICKS_PER_PERIOD (F_CPU / TIMER_PRESCALER / TIMER_PRESCALER / FREQ)

// I2C slave command macros
#define I2C_complete() TWI0.SCTRLB = TWI_SCMD_COMPTRANS_gc
#define I2C_response() TWI0.SCTRLB = TWI_SCMD_RESPONSE_gc
#define I2C_sendACK() TWI0.SCTRLB = TWI_ACKACT_ACK_gc | TWI_SCMD_RESPONSE_gc
#define I2C_put(x) TWI0.SDATA = (x)
#define I2C_get() TWI0.SDATA

// Pin manipulation macros
#define pinInput(x) PORTA.DIRCLR = 1 << (x)  // set pin to INPUT
#define pinOutput(x) PORTA.DIRSET = 1 << (x) // set pin to OUTPUT
#define pinLow(x) PORTA.OUTCLR = 1 << (x)    // set pin to LOW
#define pinHigh(x) PORTA.OUTSET = 1 << (x)   // set pin to HIGH
#define pinIntEn(x, type) (&PORTA.PIN0CTRL)[x] |= type
#define pinIntClr(x) VPORTA.INTFLAGS = (1 << (x))

// I2C slave status macros
#define I2C_isAddr()                                                           \
  ((TWI0.SSTATUS & TWI_APIF_bm) && (TWI0.SSTATUS & TWI_AP_bm))
#define I2C_isData() (TWI0.SSTATUS & TWI_DIF_bm)
#define I2C_isStop()                                                           \
  ((TWI0.SSTATUS & TWI_APIF_bm) && (~TWI0.SSTATUS & TWI_AP_bm))
#define I2C_isOut() (TWI0.SSTATUS & TWI_DIR_bm)

// Timer macros
#define timer_IntDisable() TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm
#define timer_IntEnable()                                                      \
  TCA0.SINGLE.INTCTRL = TCA_SINGLE_CMP0_bm | TCA_SINGLE_CMP1_bm |              \
                        TCA_SINGLE_CMP2_bm | TCA_SINGLE_OVF_bm
#define timer_IntReset(x) TCA0.SINGLE.INTFLAGS = x

// I2C slave registers
uint8_t I2C_REG[2];          // register array
uint8_t I2C_REG_ptr;         // register pointer
uint8_t I2C_REG_changed = 0; // register change flag
uint8_t I2C_busy = 0;        // I2C busy flag
volatile uint8_t I2C_int = 0;
volatile uint8_t timerOutOfSync = 1;

// I2C slave init
void I2C_init(void) {
  TWI0.SADDR = I2C_ADDR << 1;    // set address (LSB is R/W bit)
  TWI0.SCTRLA = TWI_DIEN_bm      // data interrupt enable
                | TWI_APIEN_bm   // address or stop interrupt enable
                | TWI_PIEN_bm    // stop interrupt enable
                | TWI_ENABLE_bm; // enable I2C slave
}

void I2C_handle(void) {
  // Address match interrupt handler
  if (I2C_isAddr()) { // address match?
    I2C_sendACK();    // send ACK to master
    I2C_REG_ptr = 0;  // reset register pointer
    I2C_busy = 1;     // set I2C busy flag
    return;           // quit ISR
  }

  // Data interrupt handler
  if (I2C_isData()) {                   // data transmission?
    if (I2C_isOut()) {                  // slave writing to master?
      I2C_put(I2C_REG[I2C_REG_ptr]);    // send register value to master
      I2C_response();                   // no ACK needed here
    } else {                            // slave reading from master?
      I2C_REG[I2C_REG_ptr] = I2C_get(); // read register value from master
      I2C_sendACK();                    // send ACK to master
      I2C_REG_changed = 1;              // set register changed flag
    }
    if (++I2C_REG_ptr >= sizeof(I2C_REG)) // increase pointer...
      I2C_REG_ptr = 0;                    // ...or wrap around
    return;                               // quit ISR
  }

  // Stop condition interrupt handler
  if (I2C_isStop()) { // stop condition?
    I2C_complete();   // complete transaction
    I2C_busy = 0;     // clear I2C busy flag
  }
}

ISR(TCA0_CMP0_vect) {
  TCA0.SINGLE.CMP2 = TICKS_PER_PERIOD / 2 - TICKS_PER_PERIOD / 50;
  pinHigh(PIN_TRIAC);
  timer_IntReset(TCA_SINGLE_CMP0_bm);
}

ISR(TCA0_CMP1_vect) {
  TCA0.SINGLE.CMP2 = TICKS_PER_PERIOD - TICKS_PER_PERIOD / 50;
  pinHigh(PIN_TRIAC);
  timer_IntReset(TCA_SINGLE_CMP1_bm);
}

ISR(TCA0_CMP2_vect) {
  pinLow(PIN_TRIAC);
  timer_IntReset(TCA_SINGLE_CMP2_bm);
}

ISR(TCA0_OVF_vect) { timerOutOfSync = 1; }

void updateTimer(uint16_t delayMicroSec) {
  if (delayMicroSec == 0) {
    timer_IntDisable();
    pinHigh(PIN_TRIAC);
  } else if (delayMicroSec == 0xFFFF) {
    timer_IntDisable();
    pinLow(PIN_TRIAC);
  } else {
    timer_IntEnable();

    uint32_t comp = delayMicroSec;
    comp *= TICKS_PER_PERIOD;
    comp /= 20000;

    uint16_t cmp0 = comp;
    uint16_t cmp1 = cmp0 + TICKS_PER_PERIOD / 2;

    cli();
    TCA0.SINGLE.CMP0 = cmp0;
    TCA0.SINGLE.CMP1 = cmp1;
    sei();
  }
}

int main() {
  /* Set CPU clock to 10 MHz */
  _PROTECTED_WRITE(CLKCTRL_MCLKCTRLB, CLKCTRL_PDIV_2X_gc | CLKCTRL_PEN_bm);

  I2C_init();
  pinInput(PIN_ZERO);
  pinIntEn(PIN_ZERO, PORT_ISC_FALLING_gc);
  pinOutput(PIN_TRIAC);

  TCA0.SINGLE.PER = TICKS_PER_PERIOD * 1.1;
  TCA0.SINGLE.CTRLA =
      (TIMER_PRESCALER << 1) | TCA_SINGLE_ENABLE_bm; // divide clock by 4

  I2C_REG[0] = 0xFF;
  I2C_REG[1] = 0xFF;
  I2C_REG_changed = 1;

  sei();

  while (1) {
    if (I2C_int) {
      I2C_int = 0;
      I2C_handle();
    }
    if (I2C_REG_changed && !I2C_busy) {
      uint16_t delay = ((uint16_t)I2C_REG[0] << 8) | (uint16_t)I2C_REG[1];
      updateTimer(delay);
      I2C_REG_changed = 0;
    }
  }
}

// I2C slave interrupt service routine
ISR(TWI0_TWIS_vect) { I2C_int = 1; }

// Zero detection
ISR(PORTA_PORT_vect) {
  int32_t error = TCA0.SINGLE.CNT;
  error -= TICKS_PER_PERIOD;
  if (error < 0)
    error = -error;

  if (timerOutOfSync || error <= (TICKS_PER_PERIOD / 30)) {
    TCA0.SINGLE.CNT = 0; // reset timer
    timerOutOfSync = 0;
  }

  pinIntClr(PIN_ZERO); // clear interrupt flag
}