#include "dmx.h"
#include "mqtt.h"
#include <stdbool.h>

#define ADDRESS "tcp://192.168.1.10:1883"
#define CLIENTID "mqtt2dmx"
#define TOPIC "dmx/channel/#"

void onReceived(MQTTMessage *message)
{
    bool valid_data = true;
    printf("Received %s on %s\n", message->payload, message->topic);

    int channel;
    sscanf(message->topic, "dmx/channel/%uhh", &channel);
    if (channel <= 0 || channel > 255)
    {
        valid_data = false;
        printf("Channel %d is invalid\n", channel);
    }

    int value;
    sscanf(message->payload, "%uhh", &value);
    if (value < 0 || value > 255)
    {
        valid_data = false;
        printf("Value %d is invalid\n", value);
    }

    if (valid_data)
    {
        printf("Setting DMX value %d on channel %d\n", value, channel);
        DMX_SetValueForChannel((unsigned char)channel, (unsigned char)value);
    }

    MQTT_free(message);
}

int main(int argc, char *argv[])
{
    DMX_Initialize();
    MQTT_Start(ADDRESS, CLIENTID, TOPIC, onReceived);

    for (;;)
    {
        DMX_Loop();
    }

    return EXIT_SUCCESS;
}