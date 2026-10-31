#ifndef dmx_h
#define dmx_h

#include "lights.h"

int DMX_Initialize(void);
int DMX_WriteFrame(const DMXValue frame[DMX_FRAME_SIZE]);
int DMX_Close(void);

#endif
