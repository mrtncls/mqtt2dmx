#ifndef ha_h
#define ha_h

#include "ha_protocol.h"
#include "mqtt.h"

typedef enum { HA_READY, HA_OFFLINE, HA_REMOVALS, HA_CONFIGS, HA_SNAPSHOTS } HARefresh;
typedef struct
{
    const GatewayConfig *config;
    Transition transitions[LIGHT_LIMIT];
    bool dirty[LIGHT_LIMIT];
    unsigned int epoch;
    HARefresh phase;
    size_t cursor, next_dirty;
} HomeAssistant;

void HA_Initialize(HomeAssistant *ha, const GatewayConfig *config);
bool HA_Receive(HomeAssistant *ha, const MQTTMessage *message, uint64_t now);
void HA_Raw(HomeAssistant *ha, DMXChannel channel, DMXValue value);
bool HA_Render(HomeAssistant *ha, LightState *lights, uint64_t now);
void HA_Submitted(HomeAssistant *ha);
int HA_Service(HomeAssistant *ha);

#endif
