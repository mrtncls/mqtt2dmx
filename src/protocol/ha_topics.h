#ifndef ha_topics_h
#define ha_topics_h

#include "config.h"

typedef enum { HA_COMMAND_TOPIC, HA_STATE_TOPIC, HA_DISCOVERY_TOPIC } HATopic;

/* Builders require validated configuration and HA_TOPIC_LIMIT-byte destinations. */
void HA_Topic(const GatewayConfig *config, size_t light, HATopic type, char out[HA_TOPIC_LIMIT]);
void HA_CommandFilter(const GatewayConfig *config, char out[HA_TOPIC_LIMIT]);
void HA_AvailabilityTopic(const GatewayConfig *config, char out[HA_TOPIC_LIMIT]);
int HA_Route(const GatewayConfig *config, const char *topic, size_t length);

#endif
