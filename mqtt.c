#include "mqtt.h"

#include "stdio.h"

#define ADDRESS "tcp://192.168.1.10:1883"
#define CLIENTID "ExampleClientPub"
#define TOPIC "MQTT Examples"
#define PAYLOAD "Hello World!"

MQTTAsync client;
char *mqtt_topic;
MQTT_Received *callback;

// volatile MQTTAsync_token deliveredtoken;

// int disc_finished = 0;
// int subscribed = 0;
// int finished = 0;

void onSubscribed(void *context, MQTTAsync_successData *response)
{
    printf("Subscribe succeeded\n");
}

void onSubscribeFailed(void *context, MQTTAsync_failureData *response)
{
    printf("Subscribe failed, rc %d\n", response ? response->code : 0);
    exit(EXIT_FAILURE);
}

void Subscribe()
{
    printf("Subscribing to topic %s\nfor client %s using QoS%d\n\n", mqtt_topic, CLIENTID, QOS);

    int rc;
    MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
    opts.onSuccess = onSubscribed;
    opts.onFailure = onSubscribeFailed;
    opts.context = client;
    if ((rc = MQTTAsync_subscribe(client, mqtt_topic, QOS, &opts)) != MQTTASYNC_SUCCESS)
    {
        printf("Failed to start subscribe, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
}

void onConnected(void *context, MQTTAsync_successData *response)
{
    MQTTAsync client = (MQTTAsync)context;

    printf("Successful connection\n");

    Subscribe();
}

void onConnectFailed(void *context, MQTTAsync_failureData *response)
{
    printf("Connect failed, rc %d\n", response ? response->code : 0);
    exit(EXIT_FAILURE);
}

int Connect()
{
    int rc;
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.onSuccess = onConnected;
    conn_opts.onFailure = onConnectFailed;
    conn_opts.context = client;
    if ((rc = MQTTAsync_connect(client, &conn_opts)) != MQTTASYNC_SUCCESS)
    {
        printf("Failed to start connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
}

void Destroy()
{
    printf("Destroying MQTT client\n");

    MQTTAsync_destroy(&client);
}

void onDisconnected(void *context, MQTTAsync_successData *response)
{
    printf("Successful disconnection\n");

    Destroy();
}

void onDisconnectFailed(void *context, MQTTAsync_failureData *response)
{
    printf("Disconnect failed, rc %d\n", response ? response->code : 0);

    Destroy();

    exit(EXIT_FAILURE);
}

int Disconnect()
{
    int rc;
    MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
    disc_opts.onSuccess = onDisconnected;
    disc_opts.onFailure = onDisconnectFailed;
    if ((rc = MQTTAsync_disconnect(client, &disc_opts)) != MQTTASYNC_SUCCESS)
    {
        printf("Failed to start disconnect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}

void onConnectionLost(void *context, char *cause)
{
    MQTTAsync client = (MQTTAsync)context;

    printf("\nConnection lost\n");
    if (cause)
        printf("     cause: %s\n", cause);

    printf("Reconnecting...\n");

    Connect();
}

int onMessageArrived(void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    printf("Message received on topic %s is %.*s.\n", topicName, message->payloadlen, (char *)(message->payload));

    MQTTMessage *result = malloc(sizeof(MQTTMessage));
    result->payload = malloc(message->payloadlen);
    memcpy(result->payload, message->payload, message->payloadlen);
    result->topic = malloc(topicLen);
    memcpy(result->topic, topicName, topicLen);

    (*callback)(result);

    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);

    return 1;
}

void MQTT_Start(char *address, char *client_id, char *topic, MQTT_Received *received_callback)
{
    callback = received_callback;
    mqtt_topic = topic;

    MQTTAsync_create(&client, address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);

    MQTTAsync_setCallbacks(client, client, onConnectionLost, onMessageArrived, NULL);

    Connect();
}

void MQTT_Stop()
{
    Disconnect();
}

void MQTT_free(MQTTMessage* message)
{
    free(message->payload);
    free(message->topic);
    free(message);
}
