
#include <gba.h>
#include <stdio.h>
#include <stdlib.h>

//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------
int main(void) {
//---------------------------------------------------------------------------------

	// only enable serial irq for SI_Transfer
	irqInit();
	irqEnable(IRQ_SERIAL);

	// disable this, needs power
	REG_DISPCNT = LCDC_OFF;
	SNDSTAT = 0;
	SNDBIAS = 0;

	// send over data until we turn it off
	while (1) {
		REG_JOYTR_L = REG_KEYINPUT;
		Halt();
	}
}
