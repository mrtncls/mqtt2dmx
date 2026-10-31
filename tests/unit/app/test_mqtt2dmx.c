#include "unity.h"
#include "application_test.h"
#include "mock_mqtt.h"
#include "mock_dmx.h"
#include "mock_lights.h"
#include "mock_shutdown.h"
#include "test_helpers.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST_SOURCE_FILE("mqtt2dmx.c")
TEST_SOURCE_FILE("command.c")
TEST_SOURCE_FILE("ha.c")
TEST_SOURCE_FILE("ha_protocol.c")
TEST_SOURCE_FILE("config.c")
TEST_SOURCE_FILE("json_contract.c")
TEST_SOURCE_FILE("transition.c")
TEST_SOURCE_FILE("clock.c")

static const char *env_names[] = {"MQTT_ADDRESS", "MQTT_CLIENTID", "MQTT_TOPIC", "LIGHTS_CONFIG"};
static char *saved_env[4];
static void (*saved_int)(int);
static void (*saved_term)(int);
static MQTTMessage incoming;
static MQTTMessage confirmation;
static char topic[80];
static char payload[80];
static char original_topic[80];
static char expected_topic[80];
static char expected_payload[16];
static int updated;
static int loop_count;
static int confirmed;

void setUp(void)
{
    for (int i = 0; i < 4; i++)
    {
        char *value = getenv(env_names[i]);
        saved_env[i] = value ? strdup(value) : NULL;
        unsetenv(env_names[i]);
    }
    saved_int = signal(SIGINT, SIG_IGN);
    saved_term = signal(SIGTERM, SIG_IGN);
    Lights_Initialize_Ignore();
    Application_Reset();
    updated = 0;
    loop_count = 0;
    confirmed = 0;
}

void tearDown(void)
{
    signal(SIGINT, saved_int);
    signal(SIGTERM, saved_term);
    for (int i = 0; i < 4; i++)
    {
        if (saved_env[i])
            setenv(env_names[i], saved_env[i], 1);
        else
            unsetenv(env_names[i]);
        free(saved_env[i]);
    }
}

static bool check_update(LightState *state, DMXChannel channel, DMXValue value, int calls)
{
    TEST_ASSERT_EQUAL_INT(0, updated);
    TEST_ASSERT_EQUAL_INT(atoi(strrchr(expected_topic, '/') + 1), channel);
    TEST_ASSERT_EQUAL_INT(atoi(expected_payload), value);
    updated = 1;
    return true;
}

static MQTTMessage *check_confirmation(const char *name, int name_len, const char *data,
                                       int data_len, int calls)
{
    TEST_ASSERT_EQUAL_INT(1, updated);
    TEST_ASSERT_EQUAL_MEMORY(expected_topic, name, name_len);
    TEST_ASSERT_EQUAL_INT(strlen(expected_topic), name_len);
    TEST_ASSERT_EQUAL_STRING(expected_payload, data);
    TEST_ASSERT_EQUAL_INT(strlen(expected_payload), data_len);
    return &confirmation;
}

static void check_release(MQTTMessage *message, int calls)
{
    TEST_ASSERT_EQUAL_PTR(&incoming, message);
    TEST_ASSERT_EQUAL_STRING(original_topic, message->topic);
}

static void prepare_receive(const char *name, const char *data)
{
    strcpy(topic, name);
    strcpy(original_topic, name);
    strcpy(payload, data);
    incoming.topic = topic;
    incoming.payload = payload;
    incoming.topic_length = strlen(name);
    incoming.payload_length = strlen(data);
}

static void receive(const char *name, const char *data)
{
    prepare_receive(name, data);
    MQTT_Free_Expect(&incoming);
    MQTT_Free_AddCallback(check_release);
    onReceived(&incoming);
}

void test_command_sets_channel_then_publishes_confirmation_and_restores_topic(void)
{
    strcpy(expected_topic, "dmx/channels/16");
    strcpy(expected_payload, "199");
    Lights_Set_ExpectAndReturn(NULL, 16, 199, true);
    Lights_Set_IgnoreArg_state();
    Lights_Set_AddCallback(check_update);
    MQTT_CreateMessage_ExpectAndReturn(NULL, 15, NULL, 3, &confirmation);
    MQTT_CreateMessage_IgnoreArg_topic();
    MQTT_CreateMessage_IgnoreArg_payload();
    MQTT_CreateMessage_AddCallback(check_confirmation);
    MQTT_PublishAndFree_ExpectAndReturn(&confirmation, EXIT_SUCCESS);
    receive("dmx/channels/16/set", "199");
}

void test_supported_channel_and_value_boundaries(void)
{
    const unsigned char channels[] = {1, 255};
    const unsigned char values[] = {0, 255};
    for (int c = 0; c < 2; c++)
        for (int v = 0; v < 2; v++)
        {
            char command[96];
            sprintf(expected_topic, "dmx/channels/%u", channels[c]);
            sprintf(command, "%s/set", expected_topic);
            sprintf(expected_payload, "%u", values[v]);
            updated = 0;
            Lights_Set_ExpectAndReturn(NULL, channels[c], values[v], true);
            Lights_Set_IgnoreArg_state();
            Lights_Set_AddCallback(check_update);
            MQTT_CreateMessage_ExpectAndReturn(NULL, strlen(expected_topic),
                                               NULL, strlen(expected_payload), &confirmation);
            MQTT_CreateMessage_IgnoreArg_topic();
            MQTT_CreateMessage_IgnoreArg_payload();
            MQTT_CreateMessage_AddCallback(check_confirmation);
            MQTT_PublishAndFree_ExpectAndReturn(&confirmation, EXIT_SUCCESS);
            receive(command, expected_payload);
        }
}

void test_out_of_range_channels_are_released_without_output(void)
{
    receive("dmx/channels/0/set", "199");
    receive("dmx/channels/256/set", "199");
}

void test_out_of_range_values_are_released_without_output(void)
{
    receive("dmx/channels/16/set", "-1");
    receive("dmx/channels/16/set", "256");
}

void test_topic_without_channel_separator_is_released_without_output(void)
{
    receive("16/set", "199");
}

void test_missing_address_fails_before_starting_dependencies(void)
{
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, mqtt2dmx_main(0, NULL));
}

void test_given_missing_light_config_when_starting_then_fails_before_opening_hardware(void)
{
    // Given (Arrange)
    setenv("MQTT_ADDRESS", "tcp://test.invalid:1883", 1);
    setenv("LIGHTS_CONFIG", "/tmp/mqtt2dmx-missing-lights.json", 1);
    // When (Act)
    int status = mqtt2dmx_main(0, NULL);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    mock_dmx_Verify();
    mock_mqtt_Verify();
}

static int stop_after_two_frames(const DMXValue *frame, int calls)
{
    loop_count++;
    if (loop_count == 2)
        handle_signal(SIGTERM);
    return EXIT_SUCCESS;
}

static void expect_start(const char *id, const char *subscription)
{
    setenv("MQTT_ADDRESS", "tcp://test.invalid:1883", 1);
    Lights_Initialize_Ignore();
    Lights_Frame_Ignore();
    DMX_Initialize_ExpectAndReturn(EXIT_SUCCESS);
    MQTT_Start_ExpectAndReturn("tcp://test.invalid:1883", id,
                      subscription, onReceived, EXIT_SUCCESS);
    MQTT_Poll_ExpectAndReturn(EXIT_SUCCESS);
    DMX_WriteFrame_ExpectAndReturn(NULL, EXIT_SUCCESS);
    DMX_WriteFrame_IgnoreArg_frame();
    MQTT_Poll_ExpectAndReturn(EXIT_SUCCESS);
    DMX_WriteFrame_ExpectAndReturn(NULL, EXIT_SUCCESS);
    DMX_WriteFrame_IgnoreArg_frame();
    DMX_WriteFrame_AddCallback(stop_after_two_frames);
    Shutdown_Run_ExpectAndReturn(EXIT_SUCCESS, EXIT_SUCCESS);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, mqtt2dmx_main(0, NULL));
    TEST_ASSERT_EQUAL_INT(2, loop_count);
    TEST_ASSERT_TRUE(signal(SIGINT, SIG_IGN) == handle_signal);
    TEST_ASSERT_TRUE(signal(SIGTERM, SIG_IGN) == handle_signal);
}

void test_startup_uses_default_client_and_topic_and_runs_until_stopped(void)
{
    expect_start("mqtt2dmx", "dmx/channels/+/set");
}

void test_startup_forwards_optional_environment_overrides(void)
{
    setenv("MQTT_CLIENTID", "test-client", 1);
    setenv("MQTT_TOPIC", "stage/+/set", 1);
    expect_start("test-client", "stage/+/set");
}

void test_signal_handlers_request_stop(void)
{
    handle_signal(SIGINT);
    TEST_ASSERT_TRUE(Application_IsStopping());
    Application_Reset();
    handle_signal(SIGTERM);
    TEST_ASSERT_TRUE(Application_IsStopping());
}

static void verify_application_mocks(void)
{
    mock_dmx_Verify();
    mock_mqtt_Verify();
    mock_lights_Verify();
    mock_shutdown_Verify();
}

static void receive_in_child(void)
{
    onReceived(&incoming);
}

TEST_CASE("dmx/channels/16", "199")
TEST_CASE("", "199")
TEST_CASE("dmx/channels/16/set/extra", "199")
TEST_CASE("dmx/channels/16/settings", "199")
TEST_CASE("dmx/channels/16/set/set", "199")
TEST_CASE("dmx/channels//set", "199")
TEST_CASE("dmx/channels/text/set", "199")
TEST_CASE("dmx/channels/16x/set", "199")
TEST_CASE("dmx/channels/16hh/set", "199")
TEST_CASE("dmx/channels/+16/set", "199")
TEST_CASE("dmx/channels/-16/set", "199")
TEST_CASE("dmx/channels/ 16/set", "199")
TEST_CASE("dmx/channels/16 /set", "199")
TEST_CASE("dmx/channels/1.5/set", "199")
TEST_CASE("dmx/channels/4294967297/set", "199")
TEST_CASE("dmx/channels/184467440737095516160/set", "199")
TEST_CASE("dmx/channels/-4294967295/set", "199")
TEST_CASE("dmx/channels/16/set", "")
TEST_CASE("dmx/channels/16/set", "text")
TEST_CASE("dmx/channels/16/set", "199x")
TEST_CASE("dmx/channels/16/set", "199hh")
TEST_CASE("dmx/channels/16/set", "+199")
TEST_CASE("dmx/channels/16/set", "-0")
TEST_CASE("dmx/channels/16/set", " 199")
TEST_CASE("dmx/channels/16/set", "199 ")
TEST_CASE("dmx/channels/16/set", "199\n")
TEST_CASE("dmx/channels/16/set", "1.5")
TEST_CASE("dmx/channels/16/set", "4294967296")
TEST_CASE("dmx/channels/16/set", "184467440737095516160")
TEST_CASE("dmx/channels/16/set", "-4294967295")
void test_given_malformed_command_when_received_then_rejects_without_output_and_releases_intact_message(
    const char *name, const char *data)
{
    // Given (Arrange)
    prepare_receive(name, data);
    MQTT_Free_Expect(&incoming);
    MQTT_Free_AddCallback(check_release);
    verify_child_before_exit(verify_application_mocks);

    // When (Act)
    struct child_result result = run_child(receive_in_child);

    // Then (Assert)
    // Expectations live in the child; their unconsumed parent copies must be discarded.
    mock_mqtt_Destroy();
    mock_dmx_Destroy();
    assert_child_success(&result);
}

TEST_CASE("stage/setups/16/set", "199", "stage/setups/16", "199", 16, 199)
TEST_CASE("stage/016/set", "009", "stage/016", "9", 16, 9)
TEST_CASE("stage/16/set", "10", "stage/16", "10", 16, 10)
TEST_CASE("stage/16/set", "99", "stage/16", "99", 16, 99)
TEST_CASE("stage/16/set", "100", "stage/16", "100", 16, 100)
TEST_CASE("stage/254/set", "254", "stage/254", "254", 254, 254)
void test_given_valid_command_when_received_then_publishes_canonical_value_on_exact_parent_topic(
    const char *name, const char *data, const char *parent, const char *canonical,
    int channel, int value)
{
    // Given (Arrange)
    strcpy(expected_topic, parent);
    strcpy(expected_payload, canonical);
    prepare_receive(name, data);
    Lights_Set_ExpectAndReturn(NULL, channel, value, true);
    Lights_Set_IgnoreArg_state();
    Lights_Set_AddCallback(check_update);
    MQTT_CreateMessage_ExpectAndReturn(NULL, strlen(parent), NULL, strlen(canonical), &confirmation);
    MQTT_CreateMessage_IgnoreArg_topic();
    MQTT_CreateMessage_IgnoreArg_payload();
    MQTT_CreateMessage_AddCallback(check_confirmation);
    MQTT_PublishAndFree_ExpectAndReturn(&confirmation, EXIT_SUCCESS);
    MQTT_Free_Expect(&incoming);
    MQTT_Free_AddCallback(check_release);

    // When (Act)
    onReceived(&incoming);

    // Then (Assert)
    verify_application_mocks();
    TEST_ASSERT_EQUAL_STRING(data, incoming.payload);
}

static int requested_signal;

static int raise_stop_signal(const DMXValue *frame, int calls)
{
    TEST_ASSERT_EQUAL_INT(0, raise(requested_signal));
    return EXIT_SUCCESS;
}

static void run_application(void)
{
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, mqtt2dmx_main(0, NULL));
}

TEST_CASE(SIGINT)
TEST_CASE(SIGTERM)
void test_given_running_application_when_os_signal_arrives_then_stops_frames_and_disconnects(int sig)
{
    // Given (Arrange)
    requested_signal = sig;
    setenv("MQTT_ADDRESS", "tcp://test.invalid:1883", 1);
    Lights_Initialize_Ignore();
    Lights_Frame_Ignore();
    DMX_Initialize_ExpectAndReturn(EXIT_SUCCESS);
    MQTT_Start_ExpectAndReturn("tcp://test.invalid:1883", "mqtt2dmx", "dmx/channels/+/set",
                              onReceived, EXIT_SUCCESS);
    MQTT_Poll_ExpectAndReturn(EXIT_SUCCESS);
    DMX_WriteFrame_ExpectAndReturn(NULL, EXIT_SUCCESS);
    DMX_WriteFrame_IgnoreArg_frame();
    DMX_WriteFrame_AddCallback(raise_stop_signal);
    Shutdown_Run_ExpectAndReturn(EXIT_SUCCESS, EXIT_SUCCESS);
    verify_child_before_exit(verify_application_mocks);

    // When (Act)
    struct child_result result = run_child(run_application);

    // Then (Assert)
    mock_mqtt_Destroy();
    mock_dmx_Destroy();
    mock_lights_Destroy();
    mock_shutdown_Destroy();
    assert_child_success(&result);
}

TEST_CASE("test-client", NULL, "test-client", "dmx/channels/+/set")
TEST_CASE(NULL, "stage/+/set", "mqtt2dmx", "stage/+/set")
void test_given_one_environment_override_when_started_then_other_setting_uses_default(
    const char *id, const char *subscription, const char *expected_id, const char *expected_subscription)
{
    // Given (Arrange)
    setenv("MQTT_ADDRESS", "tcp://test.invalid:1883", 1);
    if (id)
        setenv("MQTT_CLIENTID", id, 1);
    if (subscription)
        setenv("MQTT_TOPIC", subscription, 1);
    requested_signal = SIGTERM;
    Lights_Initialize_Ignore();
    Lights_Frame_Ignore();
    DMX_Initialize_ExpectAndReturn(EXIT_SUCCESS);
    MQTT_Start_ExpectAndReturn("tcp://test.invalid:1883", expected_id,
                      expected_subscription, onReceived, EXIT_SUCCESS);
    MQTT_Poll_ExpectAndReturn(EXIT_SUCCESS);
    DMX_WriteFrame_ExpectAndReturn(NULL, EXIT_SUCCESS);
    DMX_WriteFrame_IgnoreArg_frame();
    DMX_WriteFrame_AddCallback(raise_stop_signal);
    Shutdown_Run_ExpectAndReturn(EXIT_SUCCESS, EXIT_SUCCESS);

    // When (Act)
    int result = mqtt2dmx_main(0, NULL);

    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, result);
    verify_application_mocks();
}

void test_given_ftdi_initialization_failure_when_started_then_closes_without_starting_mqtt_or_writing(void)
{
    // Given (Arrange)
    setenv("MQTT_ADDRESS", "tcp://test.invalid:1883", 1);
    DMX_Initialize_ExpectAndReturn(EXIT_FAILURE);
    Shutdown_Run_ExpectAndReturn(EXIT_FAILURE, EXIT_FAILURE);
    // When (Act)
    int status = mqtt2dmx_main(0, NULL);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    verify_application_mocks();
}

void test_given_mqtt_poll_failure_when_running_then_closes_without_sending_another_frame(void)
{
    // Given (Arrange)
    setenv("MQTT_ADDRESS", "tcp://test.invalid:1883", 1);
    DMX_Initialize_ExpectAndReturn(EXIT_SUCCESS);
    MQTT_Start_ExpectAndReturn("tcp://test.invalid:1883", "mqtt2dmx", "dmx/channels/+/set",
                              onReceived, EXIT_SUCCESS);
    MQTT_Poll_ExpectAndReturn(EXIT_FAILURE);
    Shutdown_Run_ExpectAndReturn(EXIT_FAILURE, EXIT_FAILURE);
    // When (Act)
    int status = mqtt2dmx_main(0, NULL);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    verify_application_mocks();
}

TEST_CASE(0)
TEST_CASE(1)
void test_given_confirmation_failure_when_command_applied_then_application_reports_failure(
    int creation_failed)
{
    // Given (Arrange)
    prepare_receive("dmx/channels/16/set", "199");
    Lights_Set_ExpectAndReturn(NULL, 16, 199, true);
    Lights_Set_IgnoreArg_state();
    MQTT_CreateMessage_ExpectAndReturn(topic, 15, NULL, 3,
                                       creation_failed ? NULL : &confirmation);
    MQTT_CreateMessage_IgnoreArg_payload();
    if (!creation_failed)
        MQTT_PublishAndFree_ExpectAndReturn(&confirmation, EXIT_FAILURE);
    MQTT_Free_Expect(&incoming);
    Lights_Frame_Ignore();
    DMX_WriteFrame_IgnoreAndReturn(EXIT_SUCCESS);
    // When (Act)
    onReceived(&incoming);
    int status = Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
}

static int record_confirmation(MQTTMessage *message, int calls)
{
    confirmed++;
    return EXIT_SUCCESS;
}

void test_given_stop_intent_when_queued_command_is_delivered_then_releases_without_applying_or_confirming(void)
{
    // Given (Arrange)
    prepare_receive("dmx/channels/16/set", "199");
    strcpy(expected_topic, "dmx/channels/16");
    strcpy(expected_payload, "199");
    Lights_Set_Stub(check_update);
    MQTT_CreateMessage_IgnoreAndReturn(&confirmation);
    MQTT_PublishAndFree_Stub(record_confirmation);
    MQTT_Free_Expect(&incoming);
    handle_signal(SIGTERM);
    // When (Act)
    onReceived(&incoming);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(0, updated);
    TEST_ASSERT_EQUAL_INT(0, confirmed);
}
