#ifndef mqtt_h
#define mqtt_h

#include <stdbool.h>
#include <stddef.h>

#define MQTT_QUEUE_CAPACITY 64
#define MQTT_TOPIC_LIMIT 1024
#define MQTT_PAYLOAD_LIMIT 512
#define MQTT_DRAIN_BUDGET 16

typedef struct MQTTMessage
{
    char *topic;
    char *payload;
    size_t topic_length;
    size_t payload_length;
    bool retained;
    struct MQTTMessage *next;
} MQTTMessage;

typedef void MQTT_Received(MQTTMessage *message);

MQTTMessage *MQTT_CreateMessage(const char *topic, int topicLen,
                                 const char *payload, int payloadLen);
void MQTT_Free(MQTTMessage *message);
/* Lifecycle and publication calls belong to the main thread. Poll transfers owned input to the consumer. */
int MQTT_Start(const char *address, const char *client_id, const char *topic,
               MQTT_Received *received_callback);
int MQTT_Stop(void);
int MQTT_Poll(void);
bool MQTT_IsClosed(void);
bool MQTT_HasFailed(void);
void MQTT_Close(void);
int MQTT_PublishAndFree(MQTTMessage *message);
/* Interface topic storage must outlive the client. Configure only while closed. */
int MQTT_SetInterface(const char *light_filter, const char *birth_topic, const char *availability_topic);
bool MQTT_IsConnected(void);
unsigned int MQTT_ConnectionEpoch(void);
size_t MQTT_PublicationsPending(void);

#endif
