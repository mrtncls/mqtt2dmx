#include "config.h"
#include "json_contract.h"
#include "ha_topics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool matches(const char *filter, const char *topic)
{
    for (;;)
    {
        if (!strcmp(filter, "#"))
            return true;
        size_t f = strcspn(filter, "/"), t = strcspn(topic, "/");
        if (!(f == 1 && filter[0] == '+') && (f != t || memcmp(filter, topic, f)))
            return false;
        if (!topic[t])
            return !filter[f] || !strcmp(filter + f, "/#");
        if (!filter[f])
            return false;
        filter += f + 1;
        topic += t + 1;
    }
}

static bool matches_output(const GatewayConfig *config, const char *filter)
{
    char output[HA_TOPIC_LIMIT];
    HA_AvailabilityTopic(config, output);
    if (matches(filter, output))
        return true;
    for (size_t i = 0; i < config->count; i++)
    {
        HA_Topic(config, i, HA_STATE_TOPIC, output);
        if (matches(filter, output))
            return true;
        HA_Topic(config, i, HA_DISCOVERY_TOPIC, output);
        if (config->discovery && matches(filter, output))
            return true;
    }
    for (size_t i = 0; i < config->removal_count; i++)
        if (matches(filter, config->removals[i].discovery) || matches(filter, config->removals[i].state))
            return true;
    return false;
}

bool Config_ValidateSubscriptions(const GatewayConfig *config, const char *legacy_filter)
{
    if (matches_output(config, legacy_filter) ||
        (config->discovery && matches(legacy_filter, config->birth_topic)))
    {
        fprintf(stderr, "Legacy MQTT filter overlaps HA publications or birth topic\n");
        return false;
    }
    return true;
}

static bool slug(const char *text)
{
    size_t length = strlen(text);
    if (!length || text[0] == '-' || text[length - 1] == '-')
        return false;
    for (size_t i = 0; i < length; i++)
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= '0' && text[i] <= '9') || text[i] == '-'))
            return false;
    return true;
}

static bool topic(const char *text)
{
    size_t length = strlen(text);
    if (!length || text[0] == '/' || text[length - 1] == '/' || strstr(text, "//"))
        return false;
    for (size_t i = 0; i < length; i++)
        if (!((text[i] >= 'a' && text[i] <= 'z') ||
              (text[i] >= 'A' && text[i] <= 'Z') ||
              (text[i] >= '0' && text[i] <= '9') ||
              text[i] == '-' || text[i] == '_' || text[i] == '/'))
            return false;
    return true;
}

static bool optional_string(json_t *object, const char *key, char *out, size_t size)
{
    json_t *value = json_object_get(object, key);
    return !value || Json_String(value, out, size);
}

static bool integer(json_t *object, const char *key, int min, int max, int *out)
{
    json_t *value = json_object_get(object, key);
    if (!json_is_integer(value) || json_integer_value(value) < min ||
        json_integer_value(value) > max)
        return false;
    *out = (int)json_integer_value(value);
    return true;
}

int Config_Find(const GatewayConfig *config, const char *id, size_t length)
{
    for (size_t i = 0; i < config->count; i++)
        if (strlen(config->lights[i].id) == length &&
            !memcmp(config->lights[i].id, id, length))
            return (int)i;
    return -1;
}

static bool invalid(const char *scope, const char *field, const char *reason)
{
    fprintf(stderr, "Invalid light configuration: %s%s%s: %s\n",
        scope, *scope && *field ? "." : "", field, reason);
    return false;
}

static bool parse_light(json_t *object, LightConfig *light, const char *scope)
{
    const char *const keys[] = {"id", "name", "channel", "minimum", "transition", "curve", NULL};
    int channel, minimum = 0;
    *light = (LightConfig){.duration_ms = 500, .curve = CURVE_SMOOTHSTEP};
    if (!Json_Keys(object, keys))
        return invalid(scope, "", "expected an object with supported fields");
    if (!Json_String(json_object_get(object, "id"), light->id, sizeof(light->id)) ||
        !slug(light->id))
        return invalid(scope, "id", "expected a valid light ID");
    if (!Json_String(json_object_get(object, "name"), light->name, sizeof(light->name)))
        return invalid(scope, "name", "expected a nonempty name within the length limit");
    if (!integer(object, "channel", 1, 255, &channel))
        return invalid(scope, "channel", "expected an integer from 1 through 255");
    if (json_object_get(object, "minimum") && !integer(object, "minimum", 0, 255, &minimum))
        return invalid(scope, "minimum", "expected an integer from 0 through 255");
    light->channel = (DMXChannel)channel;
    light->minimum = (DMXValue)minimum;
    json_t *duration = json_object_get(object, "transition");
    if (duration && !Json_Duration(duration, &light->duration_ms))
        return invalid(scope, "transition", "expected a finite duration from 0 through 3600 seconds");
    json_t *curve = json_object_get(object, "curve");
    if (curve)
    {
        const char *name = json_string_value(curve);
        if (!name || (strcmp(name, "linear") && strcmp(name, "smoothstep")))
            return invalid(scope, "curve", "expected linear or smoothstep");
        light->curve = !strcmp(name, "linear") ? CURVE_LINEAR : CURVE_SMOOTHSTEP;
    }
    return true;
}

static bool suffix(const char *text, const char *ending)
{
    size_t n = strlen(text), m = strlen(ending);
    return n > m && !strcmp(text + n - m, ending);
}

static bool parse_general(json_t *root, GatewayConfig *parsed)
{
    const char *const keys[] = {"gateway_id", "name", "prefix", "discovery_prefix",
        "birth_topic", "discovery", "lights", "removals", NULL};
    if (!Json_Keys(root, keys))
        return invalid("", "document", "expected an object with supported fields");
    if (!Json_String(json_object_get(root, "gateway_id"), parsed->gateway_id, sizeof(parsed->gateway_id)) ||
        !slug(parsed->gateway_id))
        return invalid("", "gateway_id", "expected a valid gateway ID");
    strcpy(parsed->name, parsed->gateway_id);
    strcpy(parsed->prefix, "dmx");
    strcpy(parsed->discovery_prefix, "homeassistant");
    strcpy(parsed->birth_topic, "homeassistant/status");
    parsed->discovery = true;
    if (!optional_string(root, "name", parsed->name, sizeof(parsed->name)))
        return invalid("", "name", "expected a nonempty name within the length limit");
    if (!optional_string(root, "prefix", parsed->prefix, sizeof(parsed->prefix)) || !topic(parsed->prefix))
        return invalid("", "prefix", "expected a valid topic prefix within the length limit");
    if (!optional_string(root, "discovery_prefix", parsed->discovery_prefix, sizeof(parsed->discovery_prefix)) ||
        !topic(parsed->discovery_prefix))
        return invalid("", "discovery_prefix", "expected a valid topic prefix within the length limit");
    if (!optional_string(root, "birth_topic", parsed->birth_topic, sizeof(parsed->birth_topic)) ||
        !topic(parsed->birth_topic))
        return invalid("", "birth_topic", "expected a valid topic within the length limit");
    json_t *discovery = json_object_get(root, "discovery");
    if (discovery && !json_is_boolean(discovery))
        return invalid("", "discovery", "expected a boolean");
    if (discovery)
        parsed->discovery = json_is_true(discovery);
    return true;
}

static bool parse_lights(json_t *lights, GatewayConfig *parsed)
{
    if (!json_is_array(lights) || json_array_size(lights) > LIGHT_LIMIT)
        return invalid("", "lights", "expected an array of at most 255 lights");
    parsed->count = json_array_size(lights);
    for (size_t i = 0; i < parsed->count; i++)
    {
        char scope[32];
        snprintf(scope, sizeof(scope), "lights[%zu]", i);
        if (!parse_light(json_array_get(lights, i), &parsed->lights[i], scope))
            return false;
        for (size_t j = 0; j < i; j++)
        {
            if (!strcmp(parsed->lights[i].id, parsed->lights[j].id))
                return invalid(scope, "id", "duplicate light ID");
            if (parsed->lights[i].channel == parsed->lights[j].channel)
                return invalid(scope, "channel", "duplicate DMX channel");
        }
    }
    return true;
}

static bool parse_removals(json_t *removals, GatewayConfig *parsed)
{
    if (removals && (!json_is_array(removals) || json_array_size(removals) > REMOVAL_LIMIT))
        return invalid("", "removals", "expected an array of at most 64 removals");
    parsed->removal_count = removals ? json_array_size(removals) : 0;
    const char *const removal_keys[] = {"discovery", "state", NULL};
    for (size_t i = 0; i < parsed->removal_count; i++)
    {
        json_t *entry = json_array_get(removals, i);
        Removal *r = &parsed->removals[i];
        char scope[32];
        snprintf(scope, sizeof(scope), "removals[%zu]", i);
        if (!Json_Keys(entry, removal_keys))
            return invalid(scope, "", "expected an object with discovery and state fields");
        if (!Json_String(json_object_get(entry, "discovery"), r->discovery, sizeof(r->discovery)) ||
            !topic(r->discovery) || !strstr(r->discovery, "/light/") || !suffix(r->discovery, "/config"))
            return invalid(scope, "discovery", "expected an exact discovery topic ending in /config");
        if (!Json_String(json_object_get(entry, "state"), r->state, sizeof(r->state)) || !topic(r->state) ||
            !strstr(r->state, "/lights/") || !suffix(r->state, "/state"))
            return invalid(scope, "state", "expected an exact light topic ending in /state");
        for (size_t j = 0; j < parsed->count; j++)
        {
            char current[HA_TOPIC_LIMIT];
            HA_Topic(parsed, j, HA_DISCOVERY_TOPIC, current);
            if (parsed->discovery && !strcmp(current, r->discovery))
                return invalid(scope, "discovery", "overlaps an active discovery topic");
        }
    }
    return true;
}

static bool validate_birth_topic(const GatewayConfig *config)
{
    if (!config->discovery)
        return true;
    char commands[HA_TOPIC_LIMIT];
    HA_CommandFilter(config, commands);
    if (matches_output(config, config->birth_topic) || matches(commands, config->birth_topic))
        return invalid("", "birth_topic", "overlaps light commands or publications");
    return true;
}

static bool parse_config(json_t *root, GatewayConfig *parsed)
{
    return parse_general(root, parsed) &&
        parse_lights(json_object_get(root, "lights"), parsed) &&
        parse_removals(json_object_get(root, "removals"), parsed) &&
        validate_birth_topic(parsed);
}

bool Config_Parse(const char *data, size_t length, GatewayConfig *config)
{
    json_t *root = Json_Parse(data, length, CONFIG_LIMIT);
    if (!root)
        return false;
    GatewayConfig parsed = {0};
    bool valid = parse_config(root, &parsed);
    json_decref(root);
    if (valid)
        *config = parsed;
    return valid;
}

bool Config_Load(const char *path, GatewayConfig *config)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        perror("Open light configuration");
        return false;
    }
    char *data = malloc(CONFIG_LIMIT + 1);
    if (!data)
    {
        fprintf(stderr, "Light configuration allocation failed\n");
        fclose(file);
        return false;
    }
    size_t length = fread(data, 1, CONFIG_LIMIT + 1, file);
    bool read_ok = !ferror(file);
    if (fclose(file))
        read_ok = false;
    if (!read_ok)
        fprintf(stderr, "Read light configuration failed\n");
    bool valid = read_ok && Config_Parse(data, length, config);
    free(data);
    return valid;
}
