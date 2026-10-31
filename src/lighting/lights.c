#include "lights.h"
#include <string.h>

void Lights_Initialize(LightState *state)
{
    memset(state, 0, sizeof(*state));
}

bool Lights_Set(LightState *state, DMXChannel channel, DMXValue value)
{
    if (channel == 0 || channel >= DMX_FRAME_SIZE)
        return false;
    state->slots[channel] = value;
    return true;
}

void Lights_Frame(const LightState *state, DMXValue frame[DMX_FRAME_SIZE])
{
    memcpy(frame, state->slots, DMX_FRAME_SIZE);
    frame[0] = 0;
}
