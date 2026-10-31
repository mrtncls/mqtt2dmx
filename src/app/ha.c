#include "ha.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void refresh(HomeAssistant *ha)
{
    ha->phase = HA_OFFLINE;
    ha->cursor = ha->next_dirty = 0;
    for (size_t i = 0; i < ha->config->count; i++)
        ha->dirty[i] = true;
}

void HA_Initialize(HomeAssistant *ha, const GatewayConfig *config)
{
    *ha = (HomeAssistant){.config = config};
    for (size_t i = 0; config && i < config->count; i++)
        Transition_Initialize(&ha->transitions[i]);
}

bool HA_Receive(HomeAssistant *ha, const MQTTMessage *message, uint64_t now)
{
    if (!ha->config)
        return false;
    if (ha->config->discovery && message->topic_length == strlen(ha->config->birth_topic) &&
        !memcmp(message->topic, ha->config->birth_topic, message->topic_length))
    {
        if (message->payload_length == 6 && !memcmp(message->payload, "online", 6) &&
            ha->phase == HA_READY)
            refresh(ha);
        return true;
    }
    int light = HA_Route(ha->config, message->topic, message->topic_length);
    if (light == -2)
        return false;
    if (light < 0)
    {
        fprintf(stderr, "Unknown light ID or invalid light topic\n");
        return true;
    }
    if (message->retained)
    {
        fprintf(stderr, "Retained HA light command rejected; commands must not be retained\n");
        return true;
    }
    LightCommand command;
    if (HA_Decode(message->payload, message->payload_length, &command))
    {
        Transition_Apply(&ha->transitions[light], &ha->config->lights[light], &command, now);
        ha->dirty[light] = true;
        printf("Light %s: target %u, transition %u ms\n", ha->config->lights[light].id,
            Transition_Target(&ha->transitions[light]), ha->transitions[light].duration_ms);
    }
    return true;
}

void HA_Raw(HomeAssistant *ha, DMXChannel channel, DMXValue value)
{
    for (size_t i = 0; ha->config && i < ha->config->count; i++)
        if (ha->config->lights[i].channel == channel)
        {
            Transition_Raw(&ha->transitions[i], &ha->config->lights[i], value);
            ha->dirty[i] = true;
            return;
        }
}

bool HA_Render(HomeAssistant *ha, LightState *lights, uint64_t now)
{
    for (size_t i = 0; ha->config && i < ha->config->count; i++)
    {
        DMXValue value = Transition_Evaluate(&ha->transitions[i], &ha->config->lights[i], now);
        if (!Lights_Set(lights, ha->config->lights[i].channel, value))
        {
            fprintf(stderr, "Invalid configured DMX channel\n");
            return false;
        }
    }
    return true;
}

void HA_Submitted(HomeAssistant *ha)
{
    for (size_t i = 0; ha->config && i < ha->config->count; i++)
        Transition_Submitted(&ha->transitions[i]);
}

static int publish(const char *topic, const char *payload)
{
    MQTTMessage *message = MQTT_CreateMessage(topic, (int)strlen(topic), payload, (int)strlen(payload));
    if (!message)
        return EXIT_FAILURE;
    message->retained = true;
    return MQTT_PublishAndFree(message);
}

int HA_Service(HomeAssistant *ha)
{
    if (!ha->config || !MQTT_IsConnected())
        return EXIT_SUCCESS;
    unsigned int epoch = MQTT_ConnectionEpoch();
    if (ha->epoch != epoch)
    {
        ha->epoch = epoch;
        refresh(ha);
    }
    if (MQTT_PublicationsPending())
        return EXIT_SUCCESS;
    const GatewayConfig *config = ha->config;
    char topic[HA_TOPIC_LIMIT], payload[DISCOVERY_LIMIT];
    if (ha->phase == HA_OFFLINE)
    {
        ha->phase = HA_REMOVALS;
        HA_AvailabilityTopic(config, topic);
        return publish(topic, "offline");
    }
    if (ha->phase == HA_REMOVALS)
    {
        if (ha->cursor < config->removal_count * 2)
        {
            const Removal *removal = &config->removals[ha->cursor / 2];
            const char *old_topic = ha->cursor % 2 ? removal->state : removal->discovery;
            ha->cursor++;
            return publish(old_topic, "");
        }
        if (config->removal_count)
            printf("Discovery removal manifest completed (%zu entries)\n", config->removal_count);
        ha->phase = HA_CONFIGS;
        ha->cursor = 0;
    }
    if (ha->phase == HA_CONFIGS)
    {
        if (config->discovery && ha->cursor < config->count)
        {
            size_t light = ha->cursor++;
            HA_Topic(config, light, HA_DISCOVERY_TOPIC, topic);
            if (!HA_Discovery(config, light, payload, sizeof(payload)))
                return EXIT_FAILURE;
            return publish(topic, payload);
        }
        ha->phase = HA_SNAPSHOTS;
    }
    for (size_t n = 0; n < config->count; n++)
    {
        size_t light = (ha->next_dirty + n) % config->count;
        if (!ha->dirty[light])
            continue;
        HA_Topic(config, light, HA_STATE_TOPIC, topic);
        HA_State(&ha->transitions[light], payload);
        ha->dirty[light] = false;
        ha->next_dirty = (light + 1) % config->count;
        return publish(topic, payload);
    }
    if (ha->phase == HA_SNAPSHOTS)
    {
        ha->phase = HA_READY;
        HA_AvailabilityTopic(config, topic);
        return publish(topic, "online");
    }
    return EXIT_SUCCESS;
}
