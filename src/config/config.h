#ifndef config_h
#define config_h

#include "lights.h"
#include <stddef.h>

#define LIGHT_LIMIT 255
#define ID_LIMIT 48
#define NAME_LIMIT 96
#define PREFIX_LIMIT 128
#define CONFIG_LIMIT 131072
#define REMOVAL_LIMIT 64
#define HA_TOPIC_LIMIT 256
#define DISCOVERY_LIMIT 4096
#define TRANSITION_LIMIT_MS 3600000

typedef enum { CURVE_LINEAR, CURVE_SMOOTHSTEP } Curve;
typedef struct
{
    char id[ID_LIMIT + 1], name[NAME_LIMIT + 1];
    DMXChannel channel;
    DMXValue minimum;
    uint32_t duration_ms;
    Curve curve;
} LightConfig;

typedef struct
{
    char discovery[HA_TOPIC_LIMIT], state[HA_TOPIC_LIMIT];
} Removal;

typedef struct
{
    char gateway_id[ID_LIMIT + 1], name[NAME_LIMIT + 1];
    char prefix[PREFIX_LIMIT + 1], discovery_prefix[PREFIX_LIMIT + 1];
    char birth_topic[PREFIX_LIMIT + 1];
    bool discovery;
    size_t count, removal_count;
    LightConfig lights[LIGHT_LIMIT];
    Removal removals[REMOVAL_LIMIT];
} GatewayConfig;

bool Config_Parse(const char *data, size_t length, GatewayConfig *config);
bool Config_Load(const char *path, GatewayConfig *config);
int Config_Find(const GatewayConfig *config, const char *id, size_t length);
bool Config_ValidateSubscriptions(const GatewayConfig *config, const char *legacy_filter);

#endif
