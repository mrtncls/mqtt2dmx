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
static atomic_bool online;
static atomic_uint connection_epoch;
static const char *light_topic, *birth_topic, *availability_topic;

int MQTT_SetInterface(const char *light_filter, const char *birth_filter, const char *availability)
{
    if (client || !light_filter || !availability ||
        strlen(light_filter) > MQTT_TOPIC_LIMIT || strlen(availability) > MQTT_TOPIC_LIMIT ||
        (birth_filter && strlen(birth_filter) > MQTT_TOPIC_LIMIT))
    {
        fprintf(stderr, "Invalid MQTT light interface or client already started\n");
        return EXIT_FAILURE;
    }
    light_topic = light_filter;
    /* Retain caller-owned topic storage until Close, like the legacy subscription. */
    birth_topic = birth_filter;
    availability_topic = availability;
    return EXIT_SUCCESS;
}

bool MQTT_IsConnected(void) { return atomic_load(&online); }
unsigned int MQTT_ConnectionEpoch(void) { return atomic_load(&connection_epoch); }
size_t MQTT_PublicationsPending(void)
{
    size_t count = 0;
    pthread_mutex_lock(&lock);
    for (MQTTMessage *message = outgoing; message; message = message->next)
        count++;
    pthread_mutex_unlock(&lock);
    return count;
}

static void fail(const char *operation, int rc)
{
    fprintf(stderr, "MQTT %s failed, return code %d\n", operation, rc);
    atomic_store(&failure_pending, true);
}

static void onSubscribed(void *context, MQTTAsync_successData *response)
{
    printf("MQTT subscription completed\n");
}

static void onSubscribeFailed(void *context, MQTTAsync_failureData *response)
{
    fail("subscribe", response ? response->code : 0);
}

static void onConnected(void *context, char *cause)
{
    atomic_store(&connection_established, true);
    atomic_fetch_add(&connection_epoch, 1);
    atomic_store(&online, true);
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
    atomic_store(&online, false);
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
                copy->retained = message->retained != 0;
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
    atomic_store(&online, false);
    light_topic = birth_topic = availability_topic = NULL;
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
    atomic_store(&online, false);
    atomic_store(&connection_epoch, 0);
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
    MQTTAsync_willOptions will = MQTTAsync_willOptions_initializer;
    if (availability_topic)
    {
        will.topicName = availability_topic;
        will.message = "offline";
        will.retained = 1;
        will.qos = 0;
        options.will = &will;
    }
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
    {
        if (availability_topic && MQTT_IsConnected() && !MQTT_HasFailed())
        {
            MQTTMessage *offline = MQTT_CreateMessage(availability_topic,
                (int)strlen(availability_topic), "offline", 7);
            if (!offline)
                return EXIT_FAILURE;
            offline->retained = true;
            if (MQTT_PublishAndFree(offline) != EXIT_SUCCESS)
                return EXIT_FAILURE;
        }
        lifecycle = DRAINING;
    }
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
            if (atomic_load(&connection_established) && !MQTT_IsConnected())
            {
                MQTT_Close();
                return MQTT_HasFailed() ? EXIT_FAILURE : EXIT_SUCCESS;
            }
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
    if (MQTT_IsConnected() && atomic_exchange(&subscribe_pending, false))
    {
        const char *filters[] = {mqtt_topic, light_topic, birth_topic};
        for (size_t i = 0; i < 3 && !MQTT_HasFailed(); i++)
        {
            if (!filters[i])
                continue;
            MQTTAsync_responseOptions options = MQTTAsync_responseOptions_initializer;
            options.onSuccess = onSubscribed;
            options.onFailure = onSubscribeFailed;
            int rc = MQTTAsync_subscribe(client, filters[i], 0, &options);
            if (rc != MQTTASYNC_SUCCESS)
                fail("subscribe", rc);
        }
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
    pubmsg.retained = message->retained;
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
