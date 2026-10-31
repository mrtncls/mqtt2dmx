#include "transition.h"

DMXValue Transition_Calibrate(double brightness, DMXValue minimum)
{
    if (brightness <= 0)
        return 0;
    if (brightness < 1)
        return minimum;
    if (brightness >= 255)
        return 255;
    return (DMXValue)(minimum + (brightness - 1) * (255 - minimum) / 254 + 0.5);
}

double Transition_Inverse(DMXValue value, DMXValue minimum)
{
    if (!value)
        return 0;
    if (minimum == 255)
        return 255;
    if (value <= minimum)
        return 1;
    return 1 + (double)(value - minimum) * 254 / (255 - minimum);
}

void Transition_Initialize(Transition *transition)
{
    *transition = (Transition){.remembered = 255};
}

double Transition_Value(const Transition *transition, Curve curve, uint64_t now)
{
    if (!transition->active)
        return transition->current;
    uint64_t elapsed = now >= transition->started ? now - transition->started : 0;
    if (elapsed >= transition->duration_ms)
        return transition->target;
    double t = (double)elapsed / transition->duration_ms;
    if (curve == CURVE_SMOOTHSTEP)
        t = t * t * (3 - 2 * t);
    return transition->start + (transition->target - transition->start) * t;
}

void Transition_Apply(Transition *transition, const LightConfig *config,
                      const LightCommand *command, uint64_t now)
{
    transition->start = Transition_Value(transition, config->curve, now);
    transition->current = transition->start;
    if (command->has_brightness && command->brightness)
        transition->remembered = command->brightness;
    bool on = command->has_state ? command->on : true;
    transition->target = !on ? 0 : command->has_brightness ?
        command->brightness : transition->remembered;
    transition->duration_ms = command->has_duration ? command->duration_ms : config->duration_ms;
    transition->started = now;
    transition->raw_override = false;
    transition->active = transition->duration_ms && transition->target != transition->start;
    transition->endpoint_pending = !transition->active;
    if (!transition->active)
        transition->current = transition->target;
}

void Transition_Raw(Transition *transition, const LightConfig *config, DMXValue raw)
{
    transition->current = transition->start = transition->target = Transition_Inverse(raw, config->minimum);
    transition->active = false;
    transition->endpoint_pending = true;
    transition->raw_override = true;
    transition->raw = raw;
    if (raw)
        transition->remembered = Transition_Target(transition);
}

DMXValue Transition_Evaluate(Transition *transition, const LightConfig *config, uint64_t now)
{
    if (transition->raw_override)
        return transition->raw;
    transition->current = Transition_Value(transition, config->curve, now);
    if (transition->active && now >= transition->started &&
        now - transition->started >= transition->duration_ms)
        transition->endpoint_pending = true;
    return Transition_Calibrate(transition->current, config->minimum);
}

void Transition_Submitted(Transition *transition)
{
    if (transition->endpoint_pending)
    {
        transition->active = false;
        transition->endpoint_pending = false;
    }
}

uint8_t Transition_Target(const Transition *transition)
{
    return (uint8_t)(transition->target + 0.5);
}
