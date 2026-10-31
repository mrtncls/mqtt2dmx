#ifndef lights_h
#define lights_h

#include <stdbool.h>
#include <stdint.h>

#define DMX_FRAME_SIZE 513
typedef uint16_t DMXChannel;
typedef uint8_t DMXValue;
typedef struct
{
    DMXValue slots[DMX_FRAME_SIZE];
} LightState;

void Lights_Initialize(LightState *state);
bool Lights_Set(LightState *state, DMXChannel channel, DMXValue value);
void Lights_Frame(const LightState *state, DMXValue frame[DMX_FRAME_SIZE]);

#endif
