#include "command.h"
#include <stdio.h>
#include <string.h>

static bool parse_byte(const char *text, size_t length, unsigned char *value)
{
    if (!text || length == 0)
        return false;
    unsigned int parsed = 0;
    for (size_t i = 0; i < length; i++)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
        parsed = parsed * 10 + (unsigned int)(text[i] - '0');
        if (parsed > 255)
            return false;
    }
    *value = (unsigned char)parsed;
    return true;
}

CommandResult Command_Decode(const char *topic, size_t topic_length,
                             const char *payload, size_t payload_length,
                             SetCommand *command)
{
    if (!topic || topic_length < 4 || memchr(topic, '\0', topic_length) ||
        memcmp(topic + topic_length - 4, "/set", 4) != 0)
        return COMMAND_INVALID_TOPIC;
    size_t parent_length = topic_length - 4;
    size_t channel_start = parent_length;
    while (channel_start > 0 && topic[channel_start - 1] != '/')
        channel_start--;
    unsigned char channel, value;
    if (channel_start == 0 ||
        !parse_byte(topic + channel_start, parent_length - channel_start, &channel) ||
        channel == 0 || !parse_byte(payload, payload_length, &value))
        return COMMAND_INVALID_VALUE;
    *command = (SetCommand){channel, value, parent_length};
    return COMMAND_VALID;
}

size_t Command_FormatValue(DMXValue value, char output[4])
{
    return (size_t)snprintf(output, 4, "%u", value);
}
