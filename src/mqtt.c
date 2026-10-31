#include "mqtt.h"
#include "MQTTAsync.h"
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MQTTAsync client;
static const char *mqtt_topic;
static MQTT_Received *callback;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static MQTTMessage *queue[MQTT_QUEUE_CAPACITY];
static size_t queue_head, queue_count;
static MQTTMessage *outgoing;
static bool accepting;
static enum { CLOSED, RUNNING, DRAINING, DISCONNECTING } lifecycle = CLOSED;
static atomic_bool failure_pending;
static atomic_bool disconnect_finished;
static atomic_bool subscribe_pending;
static atomic_bool connection_established;

static void fail(const char *operation, int rc)
{
    fprintf(stderr, "MQTT %s failed, return code %d\n", operation, rc);
    atomic_store(&failure_pending, true);
}

static void onSubscribed(void *context, MQTTAsync_successData *response)
{
    printf("MQTT subscribed to topic %s\n", mqtt_topic);
}

static void onSubscribeFailed(void *context, MQTTAsync_failureData *response)
{
    fail("subscribe", response ? response->code : 0);
}

static void onConnected(void *context, char *cause)
{
    atomic_store(&connection_established, true);
    atomic_store(&subscribe_pending, true);
}

static void onConnectFailed(void *context, MQTTAsync_failureData *response)
{
    int rc = response ? response->code : 0;
    if (atomic_load(&connection_established))
        fprintf(stderr, "MQTT reconnect attempt failed, return code %d; Paho will retry\n", rc);
    else
        fail("connect", rc);
}

static void onConnectionLost(void *context, char *cause)
{
    printf("MQTT connection lost%s%s; Paho will reconnect\n",
           cause ? ": " : "", cause ? cause : "");
}

static void onDisconnected(void *context, MQTTAsync_successData *response)
{
    atomic_store(&disconnect_finished, true);
}

static void onDisconnectFailed(void *context, MQTTAsync_failureData *response)
{
    fail("disconnect", response ? response->code : 0);
}

static int onMessageArrived(void *context, char *topicName, int topicLen,
                            MQTTAsync_message *message)
{
    /* Serialize producers, including allocation, so receive memory is bounded. */
    pthread_mutex_lock(&lock);
    if (accepting)
    {
        size_t length = topicLen == 0 ? strnlen(topicName, MQTT_TOPIC_LIMIT + 1) :
                                      (size_t)topicLen;
        if (topicLen < 0 || length > MQTT_TOPIC_LIMIT || message->payloadlen < 0 ||
            message->payloadlen > MQTT_PAYLOAD_LIMIT)
            fail("incoming message limits", -1);
        else if (queue_count == MQTT_QUEUE_CAPACITY)
            fail("incoming queue overflow", -1);
        else
        {
            MQTTMessage *copy = MQTT_CreateMessage(topicName, (int)length,
                                                   message->payload, message->payloadlen);
            if (copy)
            {
                queue[(queue_head + queue_count) % MQTT_QUEUE_CAPACITY] = copy;
                queue_count++;
            }
        }
    }
    pthread_mutex_unlock(&lock);
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

static void release_publication(MQTTMessage *message)
{
    pthread_mutex_lock(&lock);
    MQTTMessage **entry = &outgoing;
    while (*entry && *entry != message)
        entry = &(*entry)->next;
    if (*entry)
        *entry = message->next;
    pthread_mutex_unlock(&lock);
    MQTT_Free(message);
}

static void onSent(void *context, MQTTAsync_successData *response)
{
    release_publication(context);
}

static void onSendFailed(void *context, MQTTAsync_failureData *response)
{
    fail("send", response ? response->code : 0);
    release_publication(context);
}

bool MQTT_HasFailed(void)
{
    return atomic_load(&failure_pending);
}

bool MQTT_IsClosed(void)
{
    return client == NULL;
}

void MQTT_Close(void)
{
    pthread_mutex_lock(&lock);
    accepting = false;
    pthread_mutex_unlock(&lock);
    /* Pinned Paho serializes callbacks with destroy and may invoke failure callbacks here. */
    if (client)
    {
        MQTTAsync_destroy(&client);
        client = NULL;
    }
    lifecycle = CLOSED;
    pthread_mutex_lock(&lock);
    while (queue_count)
    {
        MQTT_Free(queue[queue_head]);
        queue_head = (queue_head + 1) % MQTT_QUEUE_CAPACITY;
        queue_count--;
    }
    while (outgoing)
    {
        MQTTMessage *message = outgoing;
        outgoing = message->next;
        MQTT_Free(message);
    }
    pthread_mutex_unlock(&lock);
}

int MQTT_Start(const char *address, const char *client_id, const char *topic,
               MQTT_Received *received_callback)
{
    if (client)
    {
        fail("start of an already open client", -1);
        return EXIT_FAILURE;
    }
    mqtt_topic = topic;
    callback = received_callback;
    queue_head = queue_count = 0;
    outgoing = NULL;
    lifecycle = RUNNING;
    atomic_store(&failure_pending, false);
    atomic_store(&disconnect_finished, false);
    atomic_store(&subscribe_pending, false);
    atomic_store(&connection_established, false);
    pthread_mutex_lock(&lock);
    accepting = true;
    pthread_mutex_unlock(&lock);

    const char *operation = "create";
    int rc = MQTTAsync_create(&client, address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTASYNC_SUCCESS)
        goto failed;
    operation = "callback registration";
    rc = MQTTAsync_setCallbacks(client, NULL, onConnectionLost, onMessageArrived, NULL);
    if (rc != MQTTASYNC_SUCCESS)
        goto failed;
    operation = "connected callback registration";
    rc = MQTTAsync_setConnected(client, NULL, onConnected);
    if (rc != MQTTASYNC_SUCCESS)
        goto failed;
    MQTTAsync_connectOptions options = MQTTAsync_connectOptions_initializer;
    options.automaticReconnect = 1;
    options.keepAliveInterval = 20;
    options.cleansession = 1;
    options.onFailure = onConnectFailed;
    operation = "connect";
    rc = MQTTAsync_connect(client, &options);
    if (rc != MQTTASYNC_SUCCESS)
        goto failed;
    return EXIT_SUCCESS;

failed:
    fail(operation, rc);
    return EXIT_FAILURE;
}

int MQTT_Stop(void)
{
    pthread_mutex_lock(&lock);
    accepting = false;
    while (queue_count)
    {
        MQTT_Free(queue[queue_head]);
        queue_head = (queue_head + 1) % MQTT_QUEUE_CAPACITY;
        queue_count--;
    }
    pthread_mutex_unlock(&lock);
    if (lifecycle == RUNNING)
        lifecycle = DRAINING;
    return MQTT_Poll();
}

int MQTT_Poll(void)
{
    if (MQTT_HasFailed())
        return EXIT_FAILURE;
    if (!client)
        return EXIT_SUCCESS;
    if (lifecycle == DRAINING || lifecycle == DISCONNECTING)
    {
        pthread_mutex_lock(&lock);
        bool drained = outgoing == NULL;
        pthread_mutex_unlock(&lock);
        if (drained && lifecycle == DRAINING)
        {
            lifecycle = DISCONNECTING;
            MQTTAsync_disconnectOptions options = MQTTAsync_disconnectOptions_initializer;
            options.onSuccess = onDisconnected;
            options.onFailure = onDisconnectFailed;
            int rc = MQTTAsync_disconnect(client, &options);
            if (rc != MQTTASYNC_SUCCESS)
                fail("disconnect", rc);
        }
        if (atomic_load(&disconnect_finished))
            MQTT_Close();
        return MQTT_HasFailed() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    if (atomic_exchange(&subscribe_pending, false))
    {
        MQTTAsync_responseOptions options = MQTTAsync_responseOptions_initializer;
        options.onSuccess = onSubscribed;
        options.onFailure = onSubscribeFailed;
        int rc = MQTTAsync_subscribe(client, mqtt_topic, 0, &options);
        if (rc != MQTTASYNC_SUCCESS)
            fail("subscribe", rc);
    }
    for (size_t i = 0; i < MQTT_DRAIN_BUDGET && !MQTT_HasFailed(); i++)
    {
        pthread_mutex_lock(&lock);
        MQTTMessage *message = NULL;
        if (queue_count)
        {
            message = queue[queue_head];
            queue_head = (queue_head + 1) % MQTT_QUEUE_CAPACITY;
            queue_count--;
        }
        pthread_mutex_unlock(&lock);
        if (!message)
            break;
        callback(message);
    }
    return MQTT_HasFailed() ? EXIT_FAILURE : EXIT_SUCCESS;
}

MQTTMessage *MQTT_CreateMessage(const char *topic, int topicLen,
                                 const char *payload, int payloadLen)
{
    if (topicLen < 0 || payloadLen < 0 || (!topic && topicLen) || (!payload && payloadLen))
    {
        fail("message lengths or buffers", -1);
        return NULL;
    }
    MQTTMessage *result = malloc(sizeof(*result));
    if (!result)
        goto allocation_failed;
    *result = (MQTTMessage){.topic_length = (size_t)topicLen, .payload_length = (size_t)payloadLen};
    result->topic = malloc((size_t)topicLen + 1);
    if (!result->topic)
        goto allocation_failed;
    if (topicLen)
        memcpy(result->topic, topic, (size_t)topicLen);
    result->topic[topicLen] = '\0';
    result->payload = malloc((size_t)payloadLen + 1);
    if (!result->payload)
        goto allocation_failed;
    if (payloadLen)
        memcpy(result->payload, payload, (size_t)payloadLen);
    result->payload[payloadLen] = '\0';
    return result;

allocation_failed:
    MQTT_Free(result);
    fail("message allocation", -1);
    return NULL;
}

void MQTT_Free(MQTTMessage *message)
{
    if (!message)
        return;
    free(message->payload);
    free(message->topic);
    free(message);
}

int MQTT_PublishAndFree(MQTTMessage *message)
{
    if (!message || !client || lifecycle != RUNNING || MQTT_HasFailed() ||
        message->payload_length > INT_MAX)
    {
        MQTT_Free(message);
        fail("publication outside running lifecycle", -1);
        return EXIT_FAILURE;
    }
    MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
    pubmsg.payload = message->payload;
    pubmsg.payloadlen = (int)message->payload_length;
    pubmsg.qos = 0;
    pubmsg.retained = 0;
    MQTTAsync_responseOptions options = MQTTAsync_responseOptions_initializer;
    options.onSuccess = onSent;
    options.onFailure = onSendFailed;
    options.context = message;
    pthread_mutex_lock(&lock);
    message->next = outgoing;
    outgoing = message;
    pthread_mutex_unlock(&lock);
    int rc = MQTTAsync_sendMessage(client, message->topic, &pubmsg, &options);
    if (rc != MQTTASYNC_SUCCESS)
    {
        release_publication(message);
        fail("sendMessage", rc);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
