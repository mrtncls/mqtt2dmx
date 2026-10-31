#include "command.h"
#include "application.h"
#include "dmx.h"
#include "mqtt.h"
#include "shutdown.h"
#include "ha.h"
#include "clock.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_MQTT_CLIENTID "mqtt2dmx"
#define DEFAULT_MQTT_TOPIC "dmx/channels/+/set"

static LightState lights;
static bool command_failed;
static volatile sig_atomic_t stopping;
static HomeAssistant ha;
static GatewayConfig configuration;
static uint64_t frame_time;
static char light_filter[HA_TOPIC_LIMIT], availability[HA_TOPIC_LIMIT];

int Application_Configure(const GatewayConfig *config)
{
    HA_Initialize(&ha, config);
    snprintf(light_filter, sizeof(light_filter), "%s/lights/+/set", config->prefix);
    snprintf(availability, sizeof(availability), "%s/availability", config->prefix);
    return MQTT_SetInterface(light_filter, config->discovery ? config->birth_topic : NULL, availability);
}

int Application_Poll(void)
{
    if (stopping)
        return EXIT_SUCCESS;
    if (ha.config && Clock_Now(&frame_time) != EXIT_SUCCESS)
        return EXIT_FAILURE;
    int status = MQTT_Poll();
    if (status != EXIT_SUCCESS || command_failed)
        return EXIT_FAILURE;
    return stopping ? EXIT_SUCCESS : HA_Service(&ha);
}

void Application_Reset(void)
{
    stopping = 0;
    command_failed = false;
    Lights_Initialize(&lights);
    HA_Initialize(&ha, NULL);
    frame_time = 0;
}

bool Application_IsStopping(void)
{
    return stopping != 0;
}

int Application_WriteFrame(void)
{
    if (command_failed)
        return EXIT_FAILURE;
    if (stopping)
        return EXIT_SUCCESS;
    if (!HA_Render(&ha, &lights, frame_time))
        return EXIT_FAILURE;
    DMXValue frame[DMX_FRAME_SIZE];
    Lights_Frame(&lights, frame);
    int status = DMX_WriteFrame(frame);
    if (status == EXIT_SUCCESS)
        HA_Submitted(&ha);
    return status;
}
void handle_signal(int sig)
{
    stopping = 1;
}

void onReceived(MQTTMessage *message)
{
    if (Application_IsStopping() || command_failed)
    {
        MQTT_Free(message);
        return;
    }
    if (HA_Receive(&ha, message, frame_time))
    {
        MQTT_Free(message);
        return;
    }
    SetCommand command;
    CommandResult result = Command_Decode(message->topic, message->topic_length,
                                          message->payload, message->payload_length, &command);
    if (result != COMMAND_VALID)
        printf("%s\n", result == COMMAND_INVALID_TOPIC ?
               "Invalid command topic: expected a final /set" : "Invalid channel or value in command");
    else if (!Lights_Set(&lights, command.channel, command.value))
    {
        fprintf(stderr, "Invalid internal DMX channel\n");
        command_failed = true;
    }
    else
    {
        HA_Raw(&ha, command.channel, command.value);
        printf("Raw DMX channel %u: %u\n", command.channel, command.value);
        char value[4];
        size_t length = Command_FormatValue(command.value, value);
        MQTTMessage *confirmation = MQTT_CreateMessage(message->topic,
            (int)command.confirmation_topic_length, value, (int)length);
        if (!confirmation || MQTT_PublishAndFree(confirmation) != EXIT_SUCCESS)
            command_failed = true;
    }
    MQTT_Free(message);
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *address = getenv("MQTT_ADDRESS");
    if (!address)
    {
        printf("MQTT_ADDRESS env variable not set\n");
        return EXIT_FAILURE;
    }
    const char *client_id = getenv("MQTT_CLIENTID");
    const char *topic = getenv("MQTT_TOPIC");
    if (!client_id)
        client_id = DEFAULT_MQTT_CLIENTID;
    if (!topic)
        topic = DEFAULT_MQTT_TOPIC;
    Application_Reset();
    const char *config_path = getenv("LIGHTS_CONFIG");
    if (config_path && (!Config_Load(config_path, &configuration) ||
        !Config_ValidateSubscriptions(&configuration, topic) ||
        Application_Configure(&configuration) != EXIT_SUCCESS))
        return EXIT_FAILURE;
    int status = DMX_Initialize();
    if (status == EXIT_SUCCESS)
        status = MQTT_Start(address, client_id, topic, onReceived);
    if (signal(SIGINT, handle_signal) == SIG_ERR || signal(SIGTERM, handle_signal) == SIG_ERR)
    {
        perror("signal");
        status = EXIT_FAILURE;
    }
    while (status == EXIT_SUCCESS && !stopping)
    {
        status = Application_Poll();
        if (command_failed)
            status = EXIT_FAILURE;
        if (status != EXIT_SUCCESS || stopping)
            break;
        status = Application_WriteFrame();
    }
    return Shutdown_Run(status);
}
