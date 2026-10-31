#ifndef command_h
#define command_h

#include <stddef.h>
#include "lights.h"

typedef struct
{
    DMXChannel channel;
    DMXValue value;
    size_t confirmation_topic_length;
} SetCommand;

typedef enum
{
    COMMAND_VALID,
    COMMAND_INVALID_TOPIC,
    COMMAND_INVALID_VALUE
} CommandResult;

CommandResult Command_Decode(const char *topic, size_t topic_length,
                             const char *payload, size_t payload_length,
                             SetCommand *command);
size_t Command_FormatValue(DMXValue value, char output[4]);

#endif
