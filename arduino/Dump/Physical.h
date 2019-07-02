#include <Arduino.h>

#ifndef _PHYSICAL_H
#define _PHYSICAL_H

// Frame headers and footers (idle bit)
void _SendDataHeader(bool is_input_frame);
void _SendCtrlHeader();
void _SendIdleBit();

// Pin flipping
void _DataWrite(uint8_t val);
void _DataMode(uint8_t mode);
void _Cycle(byte cycles);

// Physical frames
void _Null();
void _Control(byte data);
byte _DataI();
void _DataO(byte data);

#endif
