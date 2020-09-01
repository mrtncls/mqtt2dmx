#include "dmx.h"
#include "mqtt.h"
#include <stdbool.h>
#include <signal.h>
#include <string.h>

#define DEFAULT_MQTT_CLIENTID "mqtt2dmx"
#define DEFAULT_MQTT_TOPIC "dmx/channels/+/set"
#define MQTT_SET_PATH "/set"

char *mqtt_topic;

volatile sig_atomic_t stopping = false;

void handle_signal(int sig)
{
    printf("Caught signal %d\n", sig);
    stopping = true;
}

void subscribe_signals()
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
}

void publish(char *topic, int value)
{
    char value_str[4];
    sprintf(value_str, "%d", value);

    MQTTMessage *to_send = MQTT_CreateMessage(topic, strlen(topic), value_str, strlen(value_str));
    MQTT_PublishAndFree(to_send);
}

void onReceived(MQTTMessage *message)
{
    bool valid_data = true;
    printf("Received %s on %s\n", message->payload, message->topic);

    char *set_path = strstr(message->topic, MQTT_SET_PATH);
    if (set_path == NULL)
    {
        printf("Topic doesn't end with %s\n", MQTT_SET_PATH);
        valid_data = false;
    }
    else
    {
        *set_path = '\0'; // break topic string
    }

    char *channel_string = strrchr(message->topic, '/');
    if (channel_string == NULL)
    {
        valid_data = false;
    }
    else
    {
        channel_string++;
        if (channel_string == NULL)
            valid_data = false;
    }

    int channel;
    if (valid_data)
    {
        sscanf(channel_string, "%uhh", &channel);
        if (channel <= 0 || channel > 255)
        {
            valid_data = false;
            printf("Channel %d is invalid\n", channel);
        }
    }

    int value;
    if (valid_data)
    {
        sscanf(message->payload, "%uhh", &value);
        if (value < 0 || value > 255)
        {
            valid_data = false;
            printf("Value %d is invalid\n", value);
        }
    }

    if (valid_data)
    {
        DMX_SetValueForChannel((unsigned char)channel, (unsigned char)value);

        publish(message->topic, value);
    }

    *set_path = '/'; // fix topic string

    MQTT_Free(message);
}

int main(int argc, char *argv[])
{
    // Disable stdout buffering
    setvbuf(stdout, NULL, _IONBF, 0);

    char *mqtt_address = getenv("MQTT_ADDRESS");
    if (mqtt_address == NULL)
    {
        printf("MQTT_ADDRESS env variable not set\n");
        return EXIT_FAILURE;
    }

    char *mqtt_clientid = getenv("MQTT_CLIENTID");
    if (mqtt_clientid == NULL)
        mqtt_clientid = DEFAULT_MQTT_CLIENTID;

    mqtt_topic = getenv("MQTT_TOPIC");
    if (mqtt_topic == NULL)
        mqtt_topic = DEFAULT_MQTT_TOPIC;

    DMX_Initialize();
    MQTT_Start(mqtt_address, mqtt_clientid, mqtt_topic, onReceived);

    subscribe_signals();

    while (!stopping)
    {
        DMX_Loop();
    }

    return EXIT_SUCCESS;
}