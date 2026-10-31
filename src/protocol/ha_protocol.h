#ifndef ha_protocol_h
#define ha_protocol_h

#include "transition.h"
#include "ha_topics.h"

bool HA_Decode(const char *payload, size_t length, LightCommand *command);
bool HA_Discovery(const GatewayConfig *config, size_t light, char *out, size_t size);
void HA_State(const Transition *transition, char out[40]);

#endif
