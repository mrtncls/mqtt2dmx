#include "ha_topics.h"
#include <stdio.h>
#include <string.h>

static void light_topic(const GatewayConfig *config, const char *id,
                        const char *suffix, char out[HA_TOPIC_LIMIT])
{
    snprintf(out, HA_TOPIC_LIMIT, "%s/lights/%s/%s", config->prefix, id, suffix);
}

void HA_Topic(const GatewayConfig *config, size_t light, HATopic type, char out[HA_TOPIC_LIMIT])
{
    if (type == HA_DISCOVERY_TOPIC)
        snprintf(out, HA_TOPIC_LIMIT, "%s/light/mqtt2dmx_%s_%s/config",
            config->discovery_prefix, config->gateway_id, config->lights[light].id);
    else
        light_topic(config, config->lights[light].id,
            type == HA_COMMAND_TOPIC ? "set" : "state", out);
}

void HA_CommandFilter(const GatewayConfig *config, char out[HA_TOPIC_LIMIT])
{
    light_topic(config, "+", "set", out);
}

void HA_AvailabilityTopic(const GatewayConfig *config, char out[HA_TOPIC_LIMIT])
{
    snprintf(out, HA_TOPIC_LIMIT, "%s/availability", config->prefix);
}

int HA_Route(const GatewayConfig *config, const char *topic, size_t length)
{
    char prefix[HA_TOPIC_LIMIT];
    int prefix_length = snprintf(prefix, sizeof(prefix), "%s/lights/", config->prefix);
    if (length < (size_t)prefix_length || memcmp(prefix, topic, (size_t)prefix_length))
        return -2;
    if (memchr(topic, 0, length) || length <= (size_t)prefix_length + 4 ||
        memcmp(topic + length - 4, "/set", 4))
        return -1;
    return Config_Find(config, topic + prefix_length, length - prefix_length - 4);
}
