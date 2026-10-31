#ifndef transition_h
#define transition_h

#include "config.h"

typedef struct
{
    bool has_state, on, has_brightness, has_duration;
    uint8_t brightness;
    uint32_t duration_ms;
} LightCommand;

typedef struct
{
    double start, current, target;
    uint64_t started;
    uint32_t duration_ms;
    uint8_t remembered;
    bool active, endpoint_pending, raw_override;
    DMXValue raw;
} Transition;

void Transition_Initialize(Transition *transition);
DMXValue Transition_Calibrate(double brightness, DMXValue minimum);
double Transition_Inverse(DMXValue value, DMXValue minimum);
double Transition_Value(const Transition *transition, Curve curve, uint64_t now);
void Transition_Apply(Transition *transition, const LightConfig *config,
                      const LightCommand *command, uint64_t now);
void Transition_Raw(Transition *transition, const LightConfig *config, DMXValue raw);
DMXValue Transition_Evaluate(Transition *transition, const LightConfig *config, uint64_t now);
void Transition_Submitted(Transition *transition);
uint8_t Transition_Target(const Transition *transition);

#endif
