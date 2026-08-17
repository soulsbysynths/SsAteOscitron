//Millis.h  millisecond tick counter
//Ports the Arduino AVR core's Timer0 millis() implementation (cores/arduino/wiring.c)
//so timing behaves identically to what this code was originally built and tested against.

#ifndef __MILLIS_H__
#define __MILLIS_H__

void millisInit();
unsigned long millis();

#endif //__MILLIS_H__
