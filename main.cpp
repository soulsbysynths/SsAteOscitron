// main.cpp
//
// Author: Paul Soulsby

#include "Millis.h"

#include <AteOsc.h>
#include <AteOscEngineBase.h>
#include <AteOscHardwareBase.h>
#include <AtmAudio/AtmAudio.h>
#include <Wavetable/Wavetable.h>
#include <AteOscOscillator.h>
#include <AteOscPatch.h>
#include <AteOscPitch.h>
#include <SsHelpers.h>
#include <BiquadFilterFM.h>
#include <Portamento.h>
#include <QuantizePitch.h>
#include <AtmPitch.h>
#include <Wavecrusher.h>
#include <Flanger.h>
#include <Pwm.h>
#include <LedRgb.h>
#include <LedCircular.h>
#include <CvInput.h>
#include <Switch.h>
#include <RotaryEncoder.h>
#include <FixedMath/FixedMath.h>

#include <avr/interrupt.h>

int main(void)
{
	millisInit();
	sei();

	AteOsc oscitron;
	oscitron.initialize();

	unsigned long lastMillis = 0;
	while (true)
	{
		unsigned char ticksPassed = millis() - lastMillis;
		lastMillis += ticksPassed;
		oscitron.poll(ticksPassed);
	}
}
