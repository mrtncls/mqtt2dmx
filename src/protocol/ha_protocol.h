#ifndef ha_protocol_h
#define ha_protocol_h

#include "transition.h"

typedef enum { HA_COMMAND_TOPIC, HA_STATE_TOPIC, HA_DISCOVERY_TOPIC } HATopic;

bool HA_Decode(const char *payload, size_t length, LightCommand *command);
int HA_Route(const GatewayConfig *config, const char *topic, size_t length);
void HA_Topic(const GatewayConfig *config, size_t light, HATopic type, char out[HA_TOPIC_LIMIT]);
bool HA_Discovery(const GatewayConfig *config, size_t light, char *out, size_t size);
void HA_State(const Transition *transition, char out[40]);

#endif
