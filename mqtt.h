#ifndef mqtt_h
#define mqtt_h

#include "MQTTAsync.h"
#include <assert.h>
#include "stdlib.h"
#include "string.h"

#define QOS 0
#define TIMEOUT 10000L

typedef struct
{
    char *topic;
    char *payload;
} MQTTMessage;

typedef void MQTT_Received(MQTTMessage* message);

MQTTMessage* MQTT_CreateMessage(char *topic, int topicLen, char *payload, int payloadLen);
void MQTT_Free(MQTTMessage* message);

void MQTT_Start(char *address, char *client_id, char *topic, MQTT_Received * received_callback);
void MQTT_Stop();
void MQTT_PublishAndFree(MQTTMessage *message);

#endif
