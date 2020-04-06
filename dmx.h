#ifndef dmx_h
#define dmx_h

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ftdi.h>
#include <unistd.h>

void DMX_Initialize();
void DMX_Loop();
void DMX_SetValueForChannel(unsigned char channel, unsigned char value);

#endif