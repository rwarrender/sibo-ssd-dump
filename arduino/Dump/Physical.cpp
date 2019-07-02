#include "Physical.h"

static uint8_t bit = 0b00001000; // Set to Pin 3, (PORTD)

// Headers and footers (idle bit) for frames

// _SendDataHeader()
// Send start bit, high ctl bit, then the idle bit
void _SendDataHeader(bool is_input_frame) {
  _DataMode(OUTPUT);
  _DataWrite(HIGH);
  _Cycle(2);

  // As per Psion spec, if switching mode for input frame then
  // controller sets input at end of cycle 2
  if (is_input_frame) {
    _DataMode(INPUT); 
  }
    
  _SendIdleBit();
}

// Send start bit, low ctl bit, then the idle bit
void _SendCtrlHeader() {
  _DataMode(OUTPUT);
  _DataWrite(HIGH);
  _Cycle(1);
  
  _DataWrite(LOW);
  _Cycle(1);
  
  _SendIdleBit();
}

void _SendIdleBit() {
  _DataWrite(LOW);
  _Cycle(1);
}

// Pin flipping utility methods

void _DataWrite(uint8_t val) {
    if (val == LOW) {
        PORTD &= ~bit;
    } else {
        PORTD |= bit;
    }
}

void _DataMode(uint8_t mode) {
  if (mode == INPUT) {
    DDRD &= ~bit;
  } else if (mode == OUTPUT) {
    DDRD |= bit;
  }
}

// Move Clock Pin HIGH and then LOW to indicate a cycle
void _Cycle(byte cycles) {
  for (byte _cx = 0; _cx < cycles; _cx++) {
    PORTD |= B100;
    PORTD &= ~B100;
  }
}

// Frame functions

/* Frame structure
   Bit  0   1   2   3   4   5   6   7   8   9   10  11
   ST  CTL I1  D0  D1  D2  D3  D4  D5  D6  D7  I2
   - 
   ST   = Start Bit
   CTL  = Control Bit
   I1   = Idle bit 
   D0-7 = Data bits
   I2   = Idle bit
*/

// Transmitted by controller to ensure all slaves are synchronised
void _Null() {
  _DataMode(INPUT);
  _Cycle(12);
}

// Transmitted by controller. Output the whole frame
void _Control(byte data) {
  _SendCtrlHeader();
  
  for (byte _dx = 0; _dx < 8; _dx++) {
    _DataWrite(((data & (0b00000001 << _dx)) == 0) ? LOW : HIGH);
    _Cycle(1);
  }

  _SendIdleBit();
}

// Received by controller from slave, input during header
byte _DataI() {
  _SendDataHeader(true);

  _DataMode(INPUT);
  int input = 0;
  for (byte _dx = 0; _dx < 8; _dx++) {
    PORTD |= B100;
    input |= (((PIND & 0b00001000) == 0b00001000) << _dx);
    PORTD ^= B100;
  }

  _SendIdleBit();
  return input;
}


// Transmitted from the controller to slave
void _DataO(byte data) { 
  _SendDataHeader(false);
  
  int output = 0;
  for (byte _dx = 0; _dx < 8; _dx++) {
    output = ((data & (0b00000001 << _dx)) == 0) ? LOW : HIGH;
    _DataWrite(output);
    _Cycle(1);
  }
  
  _SendIdleBit();
}
