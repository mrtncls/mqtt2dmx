#include "unity.h"
#include "command.h"
#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("stage/setups/016/set", "009", 16, 9)
TEST_CASE("/1/set", "0", 1, 0)
TEST_CASE("stage/255/set", "255", 255, 255)
void test_given_valid_byte_spans_when_decoded_then_returns_typed_command_without_mutation(
    const char *topic, const char *payload, int channel, int value)
{
    // Given (Arrange)
    char topic_copy[80], payload_copy[80];
    strcpy(topic_copy, topic);
    strcpy(payload_copy, payload);
    SetCommand command;
    // When (Act)
    CommandResult result = Command_Decode(topic_copy, strlen(topic), payload_copy,
                                           strlen(payload), &command);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(COMMAND_VALID, result);
    TEST_ASSERT_EQUAL_UINT16(channel, command.channel);
    TEST_ASSERT_EQUAL_UINT8(value, command.value);
    TEST_ASSERT_EQUAL_UINT(strlen(topic) - 4, command.confirmation_topic_length);
    TEST_ASSERT_EQUAL_STRING(topic, topic_copy);
    TEST_ASSERT_EQUAL_STRING(payload, payload_copy);
}

TEST_CASE("stage/0/set", "1")
TEST_CASE("stage/256/set", "1")
TEST_CASE("stage/512/set", "1")
TEST_CASE("stage/1/set", "256")
TEST_CASE("stage/1/set", "1x")
TEST_CASE("stage/1/set", "")
TEST_CASE("1/set", "1")
TEST_CASE("stage/1/set/more", "1")
TEST_CASE("stage/184467440737095516160/set", "1")
void test_given_invalid_command_when_decoded_then_does_not_modify_output(const char *topic,
                                                                         const char *payload)
{
    // Given (Arrange)
    SetCommand command = {7, 9, 11}, unchanged = command;
    // When (Act)
    CommandResult result = Command_Decode(topic, strlen(topic), payload, strlen(payload), &command);
    // Then (Assert)
    TEST_ASSERT_NOT_EQUAL(COMMAND_VALID, result);
    TEST_ASSERT_EQUAL_MEMORY(&unchanged, &command, sizeof(command));
}

void test_given_unterminated_spans_when_decoded_then_uses_only_explicit_bytes(void)
{
    // Given (Arrange)
    const char topic[] = {'/', '1', '/', 's', 'e', 't'};
    const char payload[] = {'9'};
    SetCommand command;
    // When (Act)
    CommandResult result = Command_Decode(topic, sizeof(topic), payload, sizeof(payload), &command);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(COMMAND_VALID, result);
    TEST_ASSERT_EQUAL_UINT8(9, command.value);
}

void test_given_embedded_nuls_when_decoded_then_rejects_the_entire_span(void)
{
    // Given (Arrange)
    SetCommand command;
    const char topic[] = "a\0/1/set", payload[] = "9\0x";
    // When (Act)
    CommandResult bad_topic = Command_Decode(topic, sizeof(topic) - 1, "9", 1, &command);
    CommandResult bad_payload = Command_Decode("/1/set", 6, payload, sizeof(payload) - 1, &command);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(COMMAND_INVALID_TOPIC, bad_topic);
    TEST_ASSERT_EQUAL_INT(COMMAND_INVALID_VALUE, bad_payload);
}

void test_given_every_byte_value_when_formatted_then_returns_canonical_decimal_and_exact_length(void)
{
    // Given (Arrange)
    char output[4], expected[4];
    for (unsigned int value = 0; value <= 255; value++)
    {
        snprintf(expected, sizeof(expected), "%u", value);
        // When (Act)
        size_t length = Command_FormatValue((DMXValue)value, output);
        // Then (Assert)
        TEST_ASSERT_EQUAL_STRING(expected, output);
        TEST_ASSERT_EQUAL_UINT(strlen(expected), length);
    }
}
