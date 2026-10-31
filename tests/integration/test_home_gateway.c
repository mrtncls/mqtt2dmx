#include "unity.h"
#include "application.h"
#include "ha.h"
#include "shutdown.h"
#include "mock_paho_api.h"
#include "mock_dmx.h"
#include "mock_clock.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

TEST_SOURCE_FILE("mqtt2dmx.c")
TEST_SOURCE_FILE("mqtt.c")
TEST_SOURCE_FILE("shutdown.c")
TEST_SOURCE_FILE("command.c")
TEST_SOURCE_FILE("lights.c")
TEST_SOURCE_FILE("config.c")
TEST_SOURCE_FILE("json_contract.c")
TEST_SOURCE_FILE("transition.c")
TEST_SOURCE_FILE("ha_protocol.c")
TEST_SOURCE_FILE("ha.c")

static GatewayConfig config;
static int handle_storage;
static MQTTAsync handle;
static MQTTAsync_connected *connected;
static MQTTAsync_connectionLost *lost;
static MQTTAsync_messageArrived *arrived;
static uint64_t now;
static DMXValue frame[DMX_FRAME_SIZE];
static char topics[32][HA_TOPIC_LIMIT], payloads[32][DISCOVERY_LIMIT];
static int retained[32];
static size_t sends, frames, subscriptions, clock_calls;
static int write_status;

static int callbacks(MQTTAsync client, void *context, MQTTAsync_connectionLost *cl,
    MQTTAsync_messageArrived *ma, MQTTAsync_deliveryComplete *dc, int calls)
{
    lost = cl;
    arrived = ma;
    return 0;
}

static int connection(MQTTAsync client, void *context, MQTTAsync_connected *co, int calls)
{
    connected = co;
    return 0;
}

static int subscribe(MQTTAsync client, const char *topic, int qos,
    MQTTAsync_responseOptions *options, int calls)
{
    subscriptions++;
    options->onSuccess(options->context, NULL);
    return 0;
}

static int send(MQTTAsync client, const char *topic, const MQTTAsync_message *message,
    MQTTAsync_responseOptions *options, int calls)
{
    TEST_ASSERT_LESS_THAN(32, sends);
    strcpy(topics[sends], topic);
    memcpy(payloads[sends], message->payload, message->payloadlen);
    payloads[sends][message->payloadlen] = 0;
    retained[sends] = message->retained;
    sends++;
    options->onSuccess(options->context, NULL);
    return 0;
}

static int clock_now(uint64_t *milliseconds, int calls)
{
    clock_calls++;
    *milliseconds = now;
    return EXIT_SUCCESS;
}

static int write_frame(const DMXValue *bytes, int calls)
{
    memcpy(frame, bytes, sizeof(frame));
    frames++;
    return write_status;
}

void setUp(void)
{
    memset(&config, 0, sizeof(config));
    strcpy(config.gateway_id, "gateway");
    strcpy(config.name, "Gateway");
    strcpy(config.prefix, "house");
    strcpy(config.discovery_prefix, "ha");
    strcpy(config.birth_topic, "ha/status");
    config.discovery = true;
    config.count = 1;
    config.lights[0] = (LightConfig){.id = "lamp", .name = "Lamp", .channel = 10,
        .minimum = 66, .duration_ms = 1000, .curve = CURVE_LINEAR};
    Application_Reset();
    handle = &handle_storage;
    sends = frames = subscriptions = clock_calls = 0;
    now = 0;
    write_status = EXIT_SUCCESS;
    Clock_Now_Stub(clock_now);
    DMX_WriteFrame_Stub(write_frame);
    MQTTAsync_create_ExpectAndReturn(NULL, "test", "gateway", MQTTCLIENT_PERSISTENCE_NONE, NULL, 0);
    MQTTAsync_create_IgnoreArg_handle();
    MQTTAsync_create_ReturnThruPtr_handle(&handle);
    MQTTAsync_setCallbacks_Stub(callbacks);
    MQTTAsync_setConnected_Stub(connection);
    MQTTAsync_connect_IgnoreAndReturn(0);
    MQTTAsync_subscribe_Stub(subscribe);
    MQTTAsync_sendMessage_Stub(send);
    MQTTAsync_freeMessage_Ignore();
    MQTTAsync_free_Ignore();
    Application_Configure(&config);
    MQTT_Start("test", "gateway", "house/channels/+/set", onReceived);
    connected(NULL, NULL);
}

void tearDown(void)
{
    MQTTAsync_destroy_Ignore();
    MQTT_Close();
}

static void queue(const char *topic, const char *payload)
{
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = (void *)payload;
    message.payloadlen = (int)strlen(payload);
    arrived(NULL, (char *)topic, 0, &message);
}

void test_given_queued_json_when_owner_polls_then_starts_at_application_time_and_renders_calibrated_frames(void)
{
    // Given (Arrange)
    queue("house/lights/lamp/set", "{\"state\":\"ON\"}");
    now = 100;
    // When (Act)
    Application_Poll();
    Application_WriteFrame();
    DMXValue start = frame[10];
    now = 600;
    Application_Poll();
    Application_WriteFrame();
    DMXValue midpoint = frame[10];
    now = 1100;
    Application_Poll();
    Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(0, start);
    TEST_ASSERT_EQUAL_UINT8(160, midpoint);
    TEST_ASSERT_EQUAL_UINT8(255, frame[10]);
    TEST_ASSERT_EQUAL_UINT8(0, frame[0]);
    TEST_ASSERT_EQUAL_UINT(3, frames);
    TEST_ASSERT_EQUAL_UINT(3, clock_calls);
    TEST_ASSERT_EQUAL_UINT(3, subscriptions);
}

void test_given_active_fade_when_raw_override_received_then_confirms_exact_byte_and_reconciles_json(void)
{
    // Given (Arrange)
    queue("house/lights/lamp/set", "{\"brightness\":255}");
    Application_Poll();
    Application_WriteFrame();
    queue("house/channels/10/set", "10");
    // When (Act)
    now = 500;
    Application_Poll();
    Application_WriteFrame();
    DMXValue overridden = frame[10];
    for (int i = 0; i < 5; i++)
    {
        now += 500;
        Application_Poll();
        Application_WriteFrame();
    }
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(10, overridden);
    TEST_ASSERT_EQUAL_UINT8(10, frame[10]);
    TEST_ASSERT_EQUAL_STRING("house/channels/10", topics[1]);
    TEST_ASSERT_EQUAL_STRING("10", payloads[1]);
    TEST_ASSERT_EQUAL_INT(0, retained[1]);
    TEST_ASSERT_EQUAL_STRING("house/lights/lamp/state", topics[3]);
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"ON\",\"brightness\":1}", payloads[3]);
    TEST_ASSERT_EQUAL_INT(1, retained[3]);
}

void test_given_connection_loss_when_time_advances_then_local_fade_finishes_without_publications(void)
{
    // Given (Arrange)
    queue("house/lights/lamp/set", "{\"state\":\"ON\"}");
    Application_Poll();
    lost(NULL, "test");
    size_t before = sends;
    // When (Act)
    now = 1000;
    Application_Poll();
    Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(255, frame[10]);
    TEST_ASSERT_EQUAL_UINT(before, sends);
}

void test_given_stop_intent_when_command_is_queued_then_no_new_light_target_is_applied(void)
{
    // Given (Arrange)
    queue("house/lights/lamp/set", "{\"state\":\"ON\",\"transition\":0}");
    handle_signal(15);
    // When (Act)
    Application_Poll();
    // Then (Assert)
    TEST_ASSERT_TRUE(Application_IsStopping());
    TEST_ASSERT_EQUAL_UINT(0, frames);
    TEST_ASSERT_EQUAL_UINT(0, sends);
}

void test_given_seventeen_commands_when_polled_then_writes_between_sixteen_command_batches(void)
{
    // Given (Arrange)
    for (int brightness = 1; brightness <= 17; brightness++)
    {
        char payload[64];
        snprintf(payload, sizeof(payload), "{\"brightness\":%d,\"transition\":0}", brightness);
        queue("house/lights/lamp/set", payload);
    }
    // When (Act)
    Application_Poll();
    Application_WriteFrame();
    DMXValue first = frame[10];
    Application_Poll();
    Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(77, first);
    TEST_ASSERT_EQUAL_UINT8(78, frame[10]);
    TEST_ASSERT_EQUAL_UINT(2, frames);
    TEST_ASSERT_EQUAL_UINT(2, sends);
}

void test_given_retained_json_delivery_when_polled_then_does_not_restore_output(void)
{
    // Given (Arrange)
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = "{\"state\":\"ON\",\"transition\":0}";
    message.payloadlen = (int)strlen(message.payload);
    message.retained = 1;
    arrived(NULL, "house/lights/lamp/set", 0, &message);
    // When (Act)
    Application_Poll();
    Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(0, frame[10]);
    TEST_ASSERT_EQUAL_UINT(1, sends);
    TEST_ASSERT_EQUAL_STRING("offline", payloads[0]);
}

void test_given_discovery_send_failure_when_polled_then_propagates_unsuccessful_status(void)
{
    // Given (Arrange)
    MQTTAsync_sendMessage_Stub(NULL);
    MQTTAsync_sendMessage_ExpectAndReturn(handle, "house/availability", NULL, NULL, -3);
    MQTTAsync_sendMessage_IgnoreArg_msg();
    MQTTAsync_sendMessage_IgnoreArg_response();
    // When (Act)
    int status = Application_Poll();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    TEST_ASSERT_TRUE(MQTT_HasFailed());
    TEST_ASSERT_EQUAL_UINT(0, frames);
}

void test_given_frame_failure_when_endpoint_written_then_returns_failure_without_extra_frames(void)
{
    // Given (Arrange)
    queue("house/lights/lamp/set", "{\"state\":\"ON\",\"transition\":0}");
    Application_Poll();
    write_status = EXIT_FAILURE;
    // When (Act)
    int status = Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    TEST_ASSERT_EQUAL_UINT8(255, frame[10]);
    TEST_ASSERT_EQUAL_UINT(1, frames);
}

void test_given_clock_failure_when_polled_then_does_not_deliver_commands_or_publish(void)
{
    // Given (Arrange)
    queue("house/lights/lamp/set", "{\"state\":\"ON\"}");
    Clock_Now_Stub(NULL);
    Clock_Now_ExpectAndReturn(NULL, EXIT_FAILURE);
    Clock_Now_IgnoreArg_milliseconds();
    // When (Act)
    int status = Application_Poll();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    TEST_ASSERT_EQUAL_UINT(0, sends);
    TEST_ASSERT_EQUAL_UINT(0, frames);
}

static int disconnect_now(MQTTAsync client, const MQTTAsync_disconnectOptions *options, int calls)
{
    options->onSuccess(options->context, NULL);
    return 0;
}

void test_given_online_gateway_when_shutdown_runs_then_publishes_offline_without_final_dmx_frame(void)
{
    // Given (Arrange)
    for (int i = 0; i < 4; i++)
    {
        Application_Poll();
        Application_WriteFrame();
    }
    MQTTAsync_disconnect_Stub(disconnect_now);
    MQTTAsync_destroy_Ignore();
    DMX_Close_ExpectAndReturn(EXIT_SUCCESS);
    handle_signal(15);
    // When (Act)
    int status = Shutdown_Run(EXIT_SUCCESS);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    TEST_ASSERT_TRUE(MQTT_IsClosed());
    TEST_ASSERT_EQUAL_UINT(4, frames);
    TEST_ASSERT_EQUAL_UINT(5, sends);
    TEST_ASSERT_EQUAL_STRING("house/availability", topics[4]);
    TEST_ASSERT_EQUAL_STRING("offline", payloads[4]);
    TEST_ASSERT_EQUAL_INT(1, retained[4]);
}
