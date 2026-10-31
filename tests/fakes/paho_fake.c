#include "paho_fake.h"
#include "MQTTAsync.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MQTTAsync_messageArrived *arrived;
static atomic_size_t messages_released, topics_released;
static int handle_storage;

void PahoFake_Reset(void)
{
    arrived = NULL;
    atomic_store(&messages_released, 0);
    atomic_store(&topics_released, 0);
}

size_t PahoFake_ReleasedMessages(void) { return atomic_load(&messages_released); }
size_t PahoFake_ReleasedTopics(void) { return atomic_load(&topics_released); }

int PahoFake_Deliver(unsigned int number)
{
    char payload[4];
    int length = snprintf(payload, sizeof(payload), "%u", number);
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = payload;
    message.payloadlen = length;
    return arrived(NULL, strdup("/1/set"), 0, &message);
}

int MQTTAsync_create(MQTTAsync *handle, const char *address, const char *id,
                     int persistence, void *context)
{
    *handle = &handle_storage;
    return MQTTASYNC_SUCCESS;
}

int MQTTAsync_setCallbacks(MQTTAsync handle, void *context, MQTTAsync_connectionLost *cl,
                           MQTTAsync_messageArrived *ma, MQTTAsync_deliveryComplete *dc)
{
    arrived = ma;
    return MQTTASYNC_SUCCESS;
}

int MQTTAsync_setConnected(MQTTAsync handle, void *context, MQTTAsync_connected *co)
{
    return MQTTASYNC_SUCCESS;
}

int MQTTAsync_connect(MQTTAsync handle, const MQTTAsync_connectOptions *options)
{
    return MQTTASYNC_SUCCESS;
}

int MQTTAsync_subscribe(MQTTAsync handle, const char *topic, int qos, MQTTAsync_responseOptions *options)
{
    return MQTTASYNC_SUCCESS;
}

int MQTTAsync_disconnect(MQTTAsync handle, const MQTTAsync_disconnectOptions *options)
{
    options->onSuccess(options->context, NULL);
    return MQTTASYNC_SUCCESS;
}

int MQTTAsync_sendMessage(MQTTAsync handle, const char *topic, const MQTTAsync_message *message,
                          MQTTAsync_responseOptions *options)
{
    options->onSuccess(options->context, NULL);
    return MQTTASYNC_SUCCESS;
}

void MQTTAsync_freeMessage(MQTTAsync_message **message)
{
    atomic_fetch_add(&messages_released, 1);
}

void MQTTAsync_free(void *pointer)
{
    atomic_fetch_add(&topics_released, 1);
    free(pointer);
}

void MQTTAsync_destroy(MQTTAsync *handle)
{
    *handle = NULL;
}
