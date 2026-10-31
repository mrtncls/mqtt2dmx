#include "ha_protocol.h"
#include "json_contract.h"
#include "mqtt.h"
#include <stdio.h>
#include <string.h>

bool HA_Decode(const char *payload, size_t length, LightCommand *command)
{
    json_t *root = Json_Parse(payload, length, MQTT_PAYLOAD_LIMIT);
    if (!root)
        return false;
    const char *const keys[] = {"state", "brightness", "transition", NULL};
    bool valid = Json_Keys(root, keys);
    LightCommand parsed = {0};
    json_t *state = json_object_get(root, "state");
    json_t *brightness = json_object_get(root, "brightness");
    json_t *duration = json_object_get(root, "transition");
    parsed.has_state = state != NULL;
    parsed.has_brightness = brightness != NULL;
    parsed.has_duration = duration != NULL;
    if (state)
    {
        const char *text = json_string_value(state);
        if (!text || (strcmp(text, "ON") && strcmp(text, "OFF")))
            valid = false;
        else
            parsed.on = !strcmp(text, "ON");
    }
    if (brightness)
    {
        if (!json_is_integer(brightness) || json_integer_value(brightness) < 0 ||
            json_integer_value(brightness) > 255)
            valid = false;
        else
            parsed.brightness = (uint8_t)json_integer_value(brightness);
    }
    if (duration && !Json_Duration(duration, &parsed.duration_ms))
        valid = false;
    valid = valid && (state || brightness);
    json_decref(root);
    if (valid)
        *command = parsed;
    else
        fprintf(stderr, "Invalid HA command fields or values\n");
    return valid;
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

void HA_Topic(const GatewayConfig *config, size_t light, HATopic type, char out[HA_TOPIC_LIMIT])
{
    if (type == HA_DISCOVERY_TOPIC)
        snprintf(out, HA_TOPIC_LIMIT, "%s/light/mqtt2dmx_%s_%s/config",
            config->discovery_prefix, config->gateway_id, config->lights[light].id);
    else
        snprintf(out, HA_TOPIC_LIMIT, "%s/lights/%s/%s", config->prefix,
            config->lights[light].id, type == HA_COMMAND_TOPIC ? "set" : "state");
}

bool HA_Discovery(const GatewayConfig *config, size_t light, char *out, size_t size)
{
    char command[HA_TOPIC_LIMIT], state[HA_TOPIC_LIMIT], availability[HA_TOPIC_LIMIT];
    char unique[2 * ID_LIMIT + 11], device[ID_LIMIT + 10];
    HA_Topic(config, light, HA_COMMAND_TOPIC, command);
    HA_Topic(config, light, HA_STATE_TOPIC, state);
    snprintf(availability, sizeof(availability), "%s/availability", config->prefix);
    snprintf(unique, sizeof(unique), "mqtt2dmx_%s_%s", config->gateway_id, config->lights[light].id);
    snprintf(device, sizeof(device), "mqtt2dmx_%s", config->gateway_id);
    json_t *root = json_pack("{s:s,s:s,s:s,s:s,s:s,s:s,s:s,s:s,s:b,s:[s],s:b,s:i,s:b,s:{s:[s],s:s},s:{s:s,s:s}}",
        "schema", "json", "name", config->lights[light].name, "unique_id", unique,
        "command_topic", command, "state_topic", state, "availability_topic", availability,
        "payload_available", "online", "payload_not_available", "offline",
        "brightness", 1, "supported_color_modes", "brightness", "transition", 1,
        "qos", 0, "retain", 0, "device", "identifiers", device, "name", config->name,
        "origin", "name", "mqtt2dmx", "support_url", "https://github.com/mrtncls/mqtt2dmx");
    if (!root)
    {
        fprintf(stderr, "Discovery JSON allocation failed\n");
        return false;
    }
    size_t length = json_dumpb(root, out, size, JSON_COMPACT);
    json_decref(root);
    if (!length || length >= size || length >= DISCOVERY_LIMIT)
    {
        fprintf(stderr, "Discovery JSON exceeds output buffer\n");
        return false;
    }
    out[length] = 0;
    return true;
}

void HA_State(const Transition *transition, char out[40])
{
    snprintf(out, 40, "{\"state\":\"%s\",\"brightness\":%u}",
        transition->target > 0 ? "ON" : "OFF", Transition_Target(transition));
}
