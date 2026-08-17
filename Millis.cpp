//Millis.cpp  millisecond tick counter
//Ports the Arduino AVR core's Timer0 millis() implementation (cores/arduino/wiring.c) so
//timing behaves identically to what this code was originally built and tested against.
//Timer0 is used because that is what Arduino reserves for millis(); the rest of this
//codebase (AtmAudio, AteOscHardware) already assumes Timer0 is free and uses Timer1/
//Timer2/ADC/TWI instead, exactly as it did when built against the Arduino core.

#include "Millis.h"

#include <avr/io.h>
#include <avr/interrupt.h>

#if F_CPU != 16000000UL
	#error "Millis.cpp's prescaler/fraction constants are only correct for F_CPU == 16000000"
#endif

namespace
{
	//64 prescaler, 8-bit timer: one overflow every 64 * 256 / 16 = 1024us.
	//That is 1ms plus a 24us remainder; the remainder is accumulated in eighths
	//(1000/8 = 125, 24/8 = 3) and folded into the millisecond count once it reaches
	//a whole millisecond, avoiding drift from truncating the 1024us to 1ms.
	const unsigned char MILLIS_INC = 1;
	const unsigned char FRACT_INC = 3;
	const unsigned char FRACT_MAX = 125;

	volatile unsigned long millis_ = 0;
	volatile unsigned char fract_ = 0;
}

ISR(TIMER0_OVF_vect)
{
	unsigned long m = millis_;
	unsigned char f = fract_;

	m += MILLIS_INC;
	f += FRACT_INC;
	if (f >= FRACT_MAX)
	{
		f -= FRACT_MAX;
		++m;
	}

	fract_ = f;
	millis_ = m;
}

void millisInit()
{
	TCCR0A |= (1 << WGM01) | (1 << WGM00);
	TCCR0B |= (1 << CS01) | (1 << CS00);
	TIMSK0 |= (1 << TOIE0);
}

unsigned long millis()
{
	unsigned char oldSREG = SREG;
	cli();
	unsigned long m = millis_;
	SREG = oldSREG;

	return m;
}
