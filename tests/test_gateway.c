#include "unity.h"
#include "application_test.h"
#include "dmx.h"
#include "shutdown.h"
#include "mock_paho_api.h"
#include "mock_ftdi_api.h"
#include "mock_delay.h"
#include "mock_clock.h"
#include "test_helpers.h"
#include <stdlib.h>
#include <string.h>

TEST_SOURCE_FILE("mqtt2dmx.c")
TEST_SOURCE_FILE("mqtt.c")
TEST_SOURCE_FILE("dmx.c")
TEST_SOURCE_FILE("shutdown.c")
TEST_SOURCE_FILE("command.c")
TEST_SOURCE_FILE("lights.c")

static MQTTAsync client;
static struct ftdi_context *ftdi;

static int handle_storage;
static MQTTAsync handle;
static MQTTAsync_messageArrived *arrived;
static MQTTAsync_connectOptions connection;
static MQTTAsync_connected *connected;
static MQTTAsync_responseOptions publication;
static MQTTAsync_disconnectOptions disconnection;
static MQTTAsync_message incoming;
static char topic[128];
static char payload[128];
static char original_topic[128];
static char original_payload[128];
static char confirmation_topic[128];
static unsigned char expected_frame[513];
static int topic_length;
static int valid_command;
static int channel;
static int value;
static unsigned int allocation_failure;
static int client_destroyed;
static int device_closed;
static int context_released;
static bool inside_callback;

void setUp(void)
{
    memset(expected_frame, 0, sizeof(expected_frame));
    memset(&connection, 0, sizeof(connection));
    memset(&publication, 0, sizeof(publication));
    memset(&disconnection, 0, sizeof(disconnection));
    Application_Reset();
    client = NULL;
    ftdi = NULL;
    handle = &handle_storage;
    arrived = NULL;
    client_destroyed = 0;
    device_closed = 0;
    context_released = 0;
    inside_callback = false;
    watch_frees(NULL, 0);
}

void tearDown(void)
{
    watch_frees(NULL, 0);
    mock_paho_api_Verify();
    mock_ftdi_api_Verify();
    mock_delay_Verify();
    MQTTAsync_destroy_Ignore();
    ftdi_usb_close_IgnoreAndReturn(0);
    ftdi_deinit_Ignore();
    MQTT_Close();
    DMX_Close();
}

static int capture_callbacks(MQTTAsync actual, void *context, MQTTAsync_connectionLost *lost,
                             MQTTAsync_messageArrived *message, MQTTAsync_deliveryComplete *delivered,
                             int calls)
{
    TEST_ASSERT_NOT_NULL(lost);
    TEST_ASSERT_NOT_NULL(message);
    arrived = message;
    return MQTTASYNC_SUCCESS;
}

static int capture_connect(MQTTAsync actual, const MQTTAsync_connectOptions *options, int calls)
{
    connection = *options;
    return MQTTASYNC_SUCCESS;
}

static int capture_connected(MQTTAsync actual, void *context, MQTTAsync_connected *co, int calls)
{
    connected = co;
    return 0;
}

static int capture_ftdi(struct ftdi_context *actual, int calls)
{
    ftdi = actual;
    return 0;
}

static int capture_publication(MQTTAsync actual, const char *name, const MQTTAsync_message *message,
                               MQTTAsync_responseOptions *options, int calls)
{
    TEST_ASSERT_EQUAL_STRING(confirmation_topic, name);
    TEST_ASSERT_EQUAL_INT(incoming.payloadlen, message->payloadlen);
    TEST_ASSERT_EQUAL_MEMORY(original_payload, message->payload, incoming.payloadlen);
    TEST_ASSERT_EQUAL_INT(0, message->qos);
    TEST_ASSERT_EQUAL_INT(0, message->retained);
    TEST_ASSERT_NOT_NULL(options->onSuccess);
    MQTTMessage *owned = options->context;
    TEST_ASSERT_NOT_NULL(owned);
    void *pointers[] = {owned->topic, owned->payload, owned};
    watch_frees(pointers, 3);
    publication = *options;
    return MQTTASYNC_SUCCESS;
}

static int capture_frame(struct ftdi_context *context, const unsigned char *bytes, int size, int calls)
{
    TEST_ASSERT_EQUAL_INT(513, size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_frame, bytes, size);
    return size;
}

static int capture_disconnect(MQTTAsync actual, const MQTTAsync_disconnectOptions *options, int calls)
{
    disconnection = *options;
    return MQTTASYNC_SUCCESS;
}

static void verify_gateway_mocks(void)
{
    mock_paho_api_Verify();
    mock_ftdi_api_Verify();
    mock_delay_Verify();
}

static void verify_destroy_outside_callback(MQTTAsync *actual, int calls)
{
    TEST_ASSERT_FALSE_MESSAGE(inside_callback, "Client destruction must wait until the Paho callback returns");
}

static void expect_gateway_start(void)
{
    ftdi_init_ExpectAndReturn(NULL, 0);
    ftdi_init_IgnoreArg_ftdi();
    ftdi_init_AddCallback(capture_ftdi);
    ftdi_usb_open_desc_ExpectAndReturn(NULL, 0x0403, 0x6001, NULL, NULL, 0);
    ftdi_usb_open_desc_IgnoreArg_ftdi();
    ftdi_usb_reset_ExpectAndReturn(NULL, 0);
    ftdi_usb_reset_IgnoreArg_ftdi();
    ftdi_set_interface_ExpectAndReturn(NULL, INTERFACE_ANY, 0);
    ftdi_set_interface_IgnoreArg_ftdi();
    ftdi_set_baudrate_ExpectAndReturn(NULL, 250000, 0);
    ftdi_set_baudrate_IgnoreArg_ftdi();
    ftdi_set_line_property_ExpectAndReturn(NULL, BITS_8, STOP_BIT_2, NONE, 0);
    ftdi_set_line_property_IgnoreArg_ftdi();
    ftdi_setflowctrl_ExpectAndReturn(NULL, SIO_DISABLE_FLOW_CTRL, 0);
    ftdi_setflowctrl_IgnoreArg_ftdi();
    MQTTAsync_create_ExpectAndReturn(&client, "tcp://test.invalid:1883", "gateway-test",
                                     MQTTCLIENT_PERSISTENCE_NONE, NULL, 0);
    MQTTAsync_create_IgnoreArg_handle();
    MQTTAsync_create_ReturnThruPtr_handle(&handle);
    MQTTAsync_setCallbacks_ExpectAndReturn(handle, NULL, NULL, NULL, NULL, 0);
    MQTTAsync_setCallbacks_IgnoreArg_cl();
    MQTTAsync_setCallbacks_IgnoreArg_ma();
    MQTTAsync_setCallbacks_AddCallback(capture_callbacks);
    MQTTAsync_setConnected_ExpectAndReturn(handle, NULL, NULL, 0);
    MQTTAsync_setConnected_IgnoreArg_co();
    MQTTAsync_setConnected_AddCallback(capture_connected);
    MQTTAsync_connect_ExpectAndReturn(handle, NULL, 0);
    MQTTAsync_connect_IgnoreArg_options();
    MQTTAsync_connect_AddCallback(capture_connect);
    MQTTAsync_subscribe_ExpectAndReturn(handle, "dmx/channels/+/set", 0, NULL, 0);
    MQTTAsync_subscribe_IgnoreArg_response();
}

static void expect_gateway_flow(void)
{
    expect_gateway_start();
    MQTTAsync_freeMessage_Expect(NULL);
    MQTTAsync_freeMessage_IgnoreArg_msg();
    MQTTAsync_free_Expect(topic);
    if (valid_command)
    {
        MQTTAsync_sendMessage_ExpectAndReturn(handle, confirmation_topic, NULL, NULL, 0);
        MQTTAsync_sendMessage_IgnoreArg_msg();
        MQTTAsync_sendMessage_IgnoreArg_response();
        MQTTAsync_sendMessage_AddCallback(capture_publication);
    }
    ftdi_set_line_property2_ExpectAndReturn(NULL, BITS_8, STOP_BIT_2, NONE, BREAK_ON, 0);
    ftdi_set_line_property2_IgnoreArg_ftdi();
    usleep_ExpectAndReturn(120, 0);
    ftdi_set_line_property2_ExpectAndReturn(NULL, BITS_8, STOP_BIT_2, NONE, BREAK_OFF, 0);
    ftdi_set_line_property2_IgnoreArg_ftdi();
    usleep_ExpectAndReturn(16, 0);
    ftdi_write_data_ExpectAndReturn(NULL, NULL, 513, 513);
    ftdi_write_data_IgnoreArg_ftdi();
    ftdi_write_data_IgnoreArg_buf();
    ftdi_write_data_AddCallback(capture_frame);
    usleep_ExpectAndReturn(12000, 0);
    MQTTAsync_disconnect_ExpectAndReturn(handle, NULL, 0);
    MQTTAsync_disconnect_IgnoreArg_options();
    MQTTAsync_disconnect_AddCallback(capture_disconnect);
    MQTTAsync_destroy_Expect(&client);
    MQTTAsync_destroy_IgnoreArg_handle();
    MQTTAsync_destroy_AddCallback(verify_destroy_outside_callback);
    ftdi_usb_close_ExpectAndReturn(NULL, 0);
    ftdi_usb_close_IgnoreArg_ftdi();
    ftdi_deinit_Expect(NULL);
    ftdi_deinit_IgnoreArg_ftdi();
}

static void start_gateway(void)
{
    DMX_Initialize();
    MQTT_Start("tcp://test.invalid:1883", "gateway-test", "dmx/channels/+/set", onReceived);
    TEST_ASSERT_NOT_NULL(connected);
    connected(NULL, NULL);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    TEST_ASSERT_NOT_NULL(arrived);
}

static void run_gateway_flow(void)
{
    start_gateway();
    TEST_ASSERT_EQUAL_INT(1, arrived(handle, topic, topic_length, &incoming));
    TEST_ASSERT_EQUAL_STRING(original_topic, topic);
    TEST_ASSERT_EQUAL_MEMORY(original_payload, payload, incoming.payloadlen);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    if (valid_command)
    {
        assert_no_watched_frees();
        TEST_ASSERT_NOT_NULL(publication.onSuccess);
        publication.onSuccess(publication.context, NULL);
        assert_watched_frees();
    }
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, Application_WriteFrame());
    MQTT_Stop();
    TEST_ASSERT_NOT_NULL(disconnection.onSuccess);
    inside_callback = true;
    disconnection.onSuccess(disconnection.context, NULL);
    inside_callback = false;
    MQTT_Poll();
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, Shutdown_Run(EXIT_SUCCESS));
}

static void prepare_command(const char *name, const char *data, int data_length,
                            int explicit_length, int valid)
{
    strcpy(topic, name);
    strcpy(original_topic, name);
    memcpy(original_payload, data, data_length);
    memset(payload, 'X', sizeof(payload));
    memcpy(payload, data, data_length);
    incoming = (MQTTAsync_message)MQTTAsync_message_initializer;
    incoming.payload = payload;
    incoming.payloadlen = data_length;
    topic_length = explicit_length ? strlen(name) : 0;
    valid_command = valid;
    if (valid)
    {
        strcpy(confirmation_topic, name);
        confirmation_topic[strlen(name) - 4] = '\0';
        channel = atoi(strrchr(confirmation_topic, '/') + 1);
        value = atoi(data);
        expected_frame[channel] = value;
    }
}

TEST_CASE("dmx/channels/16/set", "199", 0, 1)
TEST_CASE("dmx/channels/16/set", "199", 1, 1)
TEST_CASE("dmx/channels/1/set", "0", 1, 1)
TEST_CASE("dmx/channels/255/set", "255", 1, 1)
TEST_CASE("dmx/channels/0/set", "199", 1, 0)
TEST_CASE("dmx/channels/256/set", "199", 1, 0)
TEST_CASE("dmx/channels/16/set", "256", 1, 0)
TEST_CASE("dmx/channels/16/set", "199x", 1, 0)
TEST_CASE("dmx/channels/16", "199", 1, 0)
void test_given_broker_command_when_gateway_processes_it_then_frame_confirmation_and_ownership_match(
    const char *name, const char *data, int explicit_length, int valid)
{
    // Given (Arrange)
    prepare_command(name, data, strlen(data), explicit_length, valid);
    expect_gateway_flow();
    verify_child_before_exit(verify_gateway_mocks);

    // When (Act)
    struct child_result result = run_child(run_gateway_flow);

    // Then (Assert)
    mock_paho_api_Destroy();
    mock_ftdi_api_Destroy();
    mock_delay_Destroy();
    assert_child_success(&result);
}

TEST_CASE("199\0x", 5)
TEST_CASE("\0", 1)
TEST_CASE("\xff", 1)
void test_given_binary_payload_when_gateway_receives_it_then_rejects_entire_payload_without_output(
    const char *data, int length)
{
    // Given (Arrange)
    prepare_command("dmx/channels/16/set", data, length, 1, 0);
    expect_gateway_flow();
    verify_child_before_exit(verify_gateway_mocks);

    // When (Act)
    struct child_result result = run_child(run_gateway_flow);

    // Then (Assert)
    mock_paho_api_Destroy();
    mock_ftdi_api_Destroy();
    mock_delay_Destroy();
    assert_child_success(&result);
}

static void run_gateway_allocation_failure(void)
{
    start_gateway();
    fail_malloc_on_call(allocation_failure);
    inside_callback = true;
    arrived(handle, topic, topic_length, &incoming);
    inside_callback = false;
    int status = MQTT_Poll();
    exit(Shutdown_Run(status));
}

static void record_client_destroy(MQTTAsync *actual, int calls)
{
    TEST_ASSERT_FALSE_MESSAGE(inside_callback, "Client destruction must wait until the Paho callback returns");
    client_destroyed++;
}

static int record_device_close(struct ftdi_context *context, int calls)
{
    TEST_ASSERT_EQUAL_PTR(ftdi, context);
    device_closed++;
    return 0;
}

static void record_context_release(struct ftdi_context *context, int calls)
{
    TEST_ASSERT_EQUAL_PTR(ftdi, context);
    context_released++;
}

static void verify_gateway_cleanup(void)
{
    verify_gateway_mocks();
    TEST_ASSERT_EQUAL_INT(1, client_destroyed);
    TEST_ASSERT_EQUAL_INT(1, device_closed);
    TEST_ASSERT_EQUAL_INT(1, context_released);
}

TEST_CASE(1)
TEST_CASE(2)
TEST_CASE(3)
TEST_CASE(4)
TEST_CASE(5)
TEST_CASE(6)
void test_given_message_allocation_failure_when_gateway_receives_command_then_releases_all_owned_resources(
    unsigned int allocation)
{
    // Given (Arrange)
    allocation_failure = allocation;
    prepare_command("dmx/channels/16/set", "199", 3, 1, 1);
    expect_gateway_start();
    MQTTAsync_freeMessage_Expect(NULL);
    MQTTAsync_freeMessage_IgnoreArg_msg();
    MQTTAsync_free_Expect(topic);
    // Error cleanup order between independent libraries is not part of the contract.
    MQTTAsync_destroy_Stub(record_client_destroy);
    ftdi_usb_close_Stub(record_device_close);
    ftdi_deinit_Stub(record_context_release);
    verify_child_before_exit(verify_gateway_cleanup);

    // When (Act)
    struct child_result result = run_child(run_gateway_allocation_failure);

    // Then (Assert)
    mock_paho_api_Destroy();
    mock_ftdi_api_Destroy();
    mock_delay_Destroy();
    assert_child_failure(&result, EXIT_FAILURE, 0, "alloc");
}

static int enqueue_during_write(struct ftdi_context *context, const unsigned char *bytes,
                                 int size, int calls)
{
    DMXValue before[DMX_FRAME_SIZE];
    memcpy(before, bytes, sizeof(before));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_frame, bytes, DMX_FRAME_SIZE);
    TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, topic_length, &incoming));
    TEST_ASSERT_EQUAL_MEMORY(before, bytes, sizeof(before));
    TEST_ASSERT_NULL(publication.onSuccess);
    return size;
}

void test_given_input_during_ftdi_write_when_frame_finishes_then_applies_command_only_before_next_frame(void)
{
    // Given (Arrange)
    prepare_command("dmx/channels/16/set", "199", 3, 1, 1);
    expected_frame[16] = 0;
    expect_gateway_start();
    start_gateway();
    ftdi_set_line_property2_ExpectAndReturn(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON, 0);
    usleep_ExpectAndReturn(120, 0);
    ftdi_set_line_property2_ExpectAndReturn(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF, 0);
    usleep_ExpectAndReturn(16, 0);
    ftdi_write_data_ExpectAndReturn(ftdi, NULL, DMX_FRAME_SIZE, DMX_FRAME_SIZE);
    ftdi_write_data_IgnoreArg_buf();
    ftdi_write_data_AddCallback(enqueue_during_write);
    MQTTAsync_freeMessage_Expect(NULL);
    MQTTAsync_freeMessage_IgnoreArg_msg();
    MQTTAsync_free_Expect(topic);
    usleep_ExpectAndReturn(12000, 0);
    // When (Act)
    int first = Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, first);
    TEST_ASSERT_NULL(publication.onSuccess);
    // Given (Arrange)
    MQTTAsync_sendMessage_ExpectAndReturn(handle, confirmation_topic, NULL, NULL, 0);
    MQTTAsync_sendMessage_IgnoreArg_msg();
    MQTTAsync_sendMessage_IgnoreArg_response();
    MQTTAsync_sendMessage_AddCallback(capture_publication);
    expected_frame[16] = 199;
    ftdi_set_line_property2_ExpectAndReturn(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON, 0);
    usleep_ExpectAndReturn(120, 0);
    ftdi_set_line_property2_ExpectAndReturn(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF, 0);
    usleep_ExpectAndReturn(16, 0);
    ftdi_write_data_ExpectAndReturn(ftdi, NULL, DMX_FRAME_SIZE, DMX_FRAME_SIZE);
    ftdi_write_data_IgnoreArg_buf();
    ftdi_write_data_AddCallback(capture_frame);
    usleep_ExpectAndReturn(12000, 0);
    // When (Act)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    int second = Application_WriteFrame();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, second);
    TEST_ASSERT_NOT_NULL(publication.onSuccess);
    publication.onSuccess(publication.context, NULL);
    assert_watched_frees();
}

TEST_CASE(0)
TEST_CASE(1)
void test_given_pending_send_or_disconnect_when_shutdown_deadline_reached_then_releases_messages_and_hardware(
    int send_pending)
{
    // Given (Arrange)
    prepare_command("dmx/channels/16/set", "199", 3, 1, 1);
    expect_gateway_start();
    start_gateway();
    MQTTAsync_freeMessage_Expect(NULL);
    MQTTAsync_freeMessage_IgnoreArg_msg();
    MQTTAsync_free_Expect(topic);
    MQTTAsync_sendMessage_ExpectAndReturn(handle, confirmation_topic, NULL, NULL, 0);
    MQTTAsync_sendMessage_IgnoreArg_msg();
    MQTTAsync_sendMessage_IgnoreArg_response();
    MQTTAsync_sendMessage_AddCallback(capture_publication);
    TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, topic_length, &incoming));
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    uint64_t start = 100, deadline = 5100;
    Clock_Now_ExpectAndReturn(NULL, EXIT_SUCCESS);
    Clock_Now_IgnoreArg_milliseconds();
    Clock_Now_ReturnThruPtr_milliseconds(&start);
    if (!send_pending)
    {
        publication.onSuccess(publication.context, NULL);
        assert_watched_frees();
        MQTTAsync_disconnect_ExpectAndReturn(handle, NULL, 0);
        MQTTAsync_disconnect_IgnoreArg_options();
        MQTTAsync_disconnect_AddCallback(capture_disconnect);
    }
    Clock_Now_ExpectAndReturn(NULL, EXIT_SUCCESS);
    Clock_Now_IgnoreArg_milliseconds();
    Clock_Now_ReturnThruPtr_milliseconds(&deadline);
    MQTTAsync_destroy_Expect(NULL);
    MQTTAsync_destroy_IgnoreArg_handle();
    ftdi_usb_close_ExpectAndReturn(ftdi, 0);
    ftdi_deinit_Expect(ftdi);
    // When (Act)
    int status = Shutdown_Run(EXIT_SUCCESS);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    if (send_pending)
        assert_watched_frees();
    TEST_ASSERT_TRUE(MQTT_IsClosed());
}
