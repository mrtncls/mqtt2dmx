#include "dmx.h"
#include "mqtt.h"

#define ADDRESS "tcp://192.168.1.10:1883"
#define CLIENTID "mqtt2dmx"
#define TOPIC "dmx/channel/#"

void onReceived(MQTTMessage *message)
{
    // TODO map topic

    int value; 
    sscanf(message->payload, "%d", &value);
    
    DMX_SetValueForChannel(10, (unsigned char)value);

    MQTT_free(message);
}

int main(int argc, char *argv[])
{
    DMX_Initialize();
    MQTT_Start(ADDRESS, CLIENTID, TOPIC, onReceived);

    for(;;)
    {
        DMX_Loop();
    }

    return EXIT_SUCCESS;    
}