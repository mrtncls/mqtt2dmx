#ifndef mqtt_h
#define mqtt_h

#include "MQTTAsync.h"
#include <assert.h>
#include "stdlib.h"
#include "string.h"

#define QOS 1
#define TIMEOUT 10000L

typedef struct
{
    char *topic;
    char *payload;
} MQTTMessage;

typedef void MQTT_Received(MQTTMessage* message);

void MQTT_Start(char *address, char *client_id, char *topic, MQTT_Received * received_callback);
void MQTT_Stop();
void MQTT_free(MQTTMessage* message);

#endif
