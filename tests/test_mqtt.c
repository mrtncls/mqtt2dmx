#include "unity.h"
#include "mqtt.h"
#include "mock_paho_api.h"
#include "test_helpers.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static MQTTAsync client;

static int handle_storage;
static MQTTAsync handle;
static MQTTAsync_connectionLost *lost;
static MQTTAsync_messageArrived *arrived;
static MQTTAsync_connected *connected;
static MQTTAsync_connectOptions connect_options;
static MQTTAsync_responseOptions subscribe_options;
static MQTTAsync_responseOptions send_options;
static MQTTAsync_disconnectOptions disconnect_options;
static MQTTMessage *published;
static MQTTAsync_message *received_paho_message;
static int received_count;
static bool inside_callback;
static MQTT_Received *consumer_callback;

static void consumer(MQTTMessage *message);

void setUp(void)
{
    handle = &handle_storage;
    client = NULL;
    lost = NULL;
    arrived = NULL;
    connected = NULL;
    published = NULL;
    received_paho_message = NULL;
    received_count = 0;
    inside_callback = false;
    consumer_callback = consumer;
    memset(&connect_options, 0, sizeof(connect_options));
    memset(&subscribe_options, 0, sizeof(subscribe_options));
    memset(&send_options, 0, sizeof(send_options));
    memset(&disconnect_options, 0, sizeof(disconnect_options));
    watch_frees(NULL, 0);
}

void tearDown(void)
{
    watch_frees(NULL, 0);
    mock_paho_api_Verify();
    MQTTAsync_destroy_Ignore();
    MQTT_Close();
}

static void watch_message(MQTTMessage *message)
{
    void *pointers[] = {message->topic, message->payload, message};
    watch_frees(pointers, 3);
}

void test_message_copies_explicit_lengths_and_owns_terminated_buffers(void)
{
    char topic[] = {'a', '/', 'b', 'X'};
    char payload[] = {'1', '9', '9', 'X'};
    MQTTMessage *message = MQTT_CreateMessage(topic, 3, payload, 3);
    TEST_ASSERT_EQUAL_STRING("a/b", message->topic);
    TEST_ASSERT_EQUAL_STRING("199", message->payload);
    TEST_ASSERT_EQUAL_UINT(3, message->topic_length);
    TEST_ASSERT_EQUAL_UINT(3, message->payload_length);
    TEST_ASSERT_NOT_EQUAL(topic, message->topic);
    TEST_ASSERT_NOT_EQUAL(payload, message->payload);
    topic[0] = 'z';
    payload[0] = '0';
    TEST_ASSERT_EQUAL_STRING("a/b", message->topic);
    TEST_ASSERT_EQUAL_STRING("199", message->payload);
    watch_message(message);
    MQTT_Free(message);
    assert_watched_frees();
}

void test_message_handles_empty_payload_and_copies_embedded_null_bytes(void)
{
    char payload[] = {'a', '\0', 'b'};
    MQTTMessage *empty = MQTT_CreateMessage("topic", 5, "", 0);
    TEST_ASSERT_EQUAL_STRING("", empty->payload);
    MQTT_Free(empty);
    MQTTMessage *binary = MQTT_CreateMessage("topic", 5, payload, 3);
    TEST_ASSERT_EQUAL_MEMORY(payload, binary->payload, 3);
    TEST_ASSERT_EQUAL_CHAR('\0', binary->payload[3]);
    MQTT_Free(binary);
}

static void consumer(MQTTMessage *message)
{
    received_count++;
    TEST_ASSERT_EQUAL_STRING("dmx/channels/16/set", message->topic);
    TEST_ASSERT_EQUAL_STRING("199", message->payload);
    watch_message(message);
    MQTT_Free(message);
    assert_watched_frees();
}

static int capture_callbacks(MQTTAsync actual, void *context,
                             MQTTAsync_connectionLost *cl, MQTTAsync_messageArrived *ma,
                             MQTTAsync_deliveryComplete *dc, int calls)
{
    TEST_ASSERT_NOT_NULL(cl);
    TEST_ASSERT_NOT_NULL(ma);
    lost = cl;
    arrived = ma;
    return MQTTASYNC_SUCCESS;
}

static int capture_connect(MQTTAsync actual, const MQTTAsync_connectOptions *options,
                           int calls)
{
    connect_options = *options;
    TEST_ASSERT_EQUAL_INT(1, options->automaticReconnect);
    TEST_ASSERT_EQUAL_INT(20, options->keepAliveInterval);
    TEST_ASSERT_EQUAL_INT(1, options->cleansession);
    TEST_ASSERT_NULL(options->onSuccess);
    TEST_ASSERT_NOT_NULL(options->onFailure);
    return MQTTASYNC_SUCCESS;
}

static int capture_connected(MQTTAsync actual, void *context, MQTTAsync_connected *co, int calls)
{
    connected = co;
    return MQTTASYNC_SUCCESS;
}

static void expect_connect(void)
{
    MQTTAsync_connect_ExpectAndReturn(handle, NULL, MQTTASYNC_SUCCESS);
    MQTTAsync_connect_IgnoreArg_options();
    MQTTAsync_connect_AddCallback(capture_connect);
}

static void start_client(void)
{
    MQTTAsync_create_ExpectAndReturn(&client, "tcp://test.invalid:1883", "test-client",
                                     MQTTCLIENT_PERSISTENCE_NONE, NULL, MQTTASYNC_SUCCESS);
    MQTTAsync_create_IgnoreArg_handle();
    MQTTAsync_create_ReturnThruPtr_handle(&handle);
    MQTTAsync_setCallbacks_ExpectAndReturn(handle, NULL, NULL, NULL, NULL,
                                           MQTTASYNC_SUCCESS);
    MQTTAsync_setCallbacks_IgnoreArg_cl();
    MQTTAsync_setCallbacks_IgnoreArg_ma();
    MQTTAsync_setCallbacks_AddCallback(capture_callbacks);
    MQTTAsync_setConnected_ExpectAndReturn(handle, NULL, NULL, MQTTASYNC_SUCCESS);
    MQTTAsync_setConnected_IgnoreArg_co();
    MQTTAsync_setConnected_AddCallback(capture_connected);
    expect_connect();
    MQTT_Start("tcp://test.invalid:1883", "test-client", "dmx/channels/+/set", consumer_callback);
}

static int capture_subscribe(MQTTAsync actual, const char *topic, int qos,
                             MQTTAsync_responseOptions *options, int calls)
{
    subscribe_options = *options;
    TEST_ASSERT_NOT_NULL(options->onSuccess);
    TEST_ASSERT_NOT_NULL(options->onFailure);
    return MQTTASYNC_SUCCESS;
}

void test_start_registers_callbacks_and_connect_success_subscribes_at_qos_zero(void)
{
    start_client();
    MQTTAsync_subscribe_ExpectAndReturn(handle, "dmx/channels/+/set", 0, NULL,
                                        MQTTASYNC_SUCCESS);
    MQTTAsync_subscribe_IgnoreArg_response();
    MQTTAsync_subscribe_AddCallback(capture_subscribe);
    MQTTAsync_successData response = {0};
    response.alt.connect.serverURI = "tcp://test.invalid:1883";
    response.alt.connect.MQTTVersion = 4;
    connected(NULL, NULL);
    MQTT_Poll();
    subscribe_options.onSuccess(subscribe_options.context, NULL);
}

void test_given_connection_loss_when_callback_runs_then_only_paho_owns_reconnect(void)
{
    // Given (Arrange)
    start_client();
    // When (Act)
    lost(handle, "test disconnect");
    lost(handle, NULL);
    // Then (Assert)
    mock_paho_api_Verify();
}

static void check_paho_message_release(MQTTAsync_message **message, int calls)
{
    TEST_ASSERT_EQUAL_INT(0, received_count);
    TEST_ASSERT_EQUAL_PTR(received_paho_message, *message);
}

void test_arrival_copies_data_delivers_to_consumer_and_releases_paho_buffers(void)
{
    start_client();
    char topic[] = "dmx/channels/16/set";
    char payload[] = {'1', '9', '9'};
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = payload;
    message.payloadlen = sizeof(payload);
    MQTTAsync_message *message_pointer = &message;
    received_paho_message = &message;
    MQTTAsync_freeMessage_Expect(&message_pointer);
    MQTTAsync_freeMessage_IgnoreArg_msg();
    MQTTAsync_freeMessage_AddCallback(check_paho_message_release);
    MQTTAsync_free_Expect(topic);
    TEST_ASSERT_EQUAL_INT(1, arrived(handle, topic, strlen(topic), &message));
    TEST_ASSERT_EQUAL_INT(0, received_count);
    MQTT_Poll();
    TEST_ASSERT_EQUAL_INT(1, received_count);
    TEST_ASSERT_EQUAL_STRING("dmx/channels/16/set", topic);
    TEST_ASSERT_EQUAL_MEMORY("199", payload, 3);
}

static int capture_send(MQTTAsync actual, const char *topic, const MQTTAsync_message *message,
                        MQTTAsync_responseOptions *options, int calls)
{
    TEST_ASSERT_EQUAL_STRING("dmx/channels/16", topic);
    TEST_ASSERT_EQUAL_PTR(published->payload, message->payload);
    TEST_ASSERT_EQUAL_MEMORY("199", message->payload, 3);
    TEST_ASSERT_EQUAL_INT(3, message->payloadlen);
    TEST_ASSERT_EQUAL_INT(0, message->qos);
    TEST_ASSERT_EQUAL_INT(0, message->retained);
    TEST_ASSERT_EQUAL_PTR(published, options->context);
    TEST_ASSERT_NOT_NULL(options->onSuccess);
    TEST_ASSERT_NOT_NULL(options->onFailure);
    send_options = *options;
    return MQTTASYNC_SUCCESS;
}

static void publish_message(void)
{
    if (MQTT_IsClosed())
        start_client();
    published = MQTT_CreateMessage("dmx/channels/16", 15, "199", 3);
    watch_message(published);
    MQTTAsync_sendMessage_ExpectAndReturn(handle, published->topic, NULL, NULL,
                                          MQTTASYNC_SUCCESS);
    MQTTAsync_sendMessage_IgnoreArg_msg();
    MQTTAsync_sendMessage_IgnoreArg_response();
    MQTTAsync_sendMessage_AddCallback(capture_send);
    MQTT_PublishAndFree(published);
    assert_no_watched_frees();
}

void test_publish_retains_message_until_success_callback_then_frees_once(void)
{
    publish_message();
    send_options.onSuccess(send_options.context, NULL);
    assert_watched_frees();
}

static int capture_disconnect(MQTTAsync actual, const MQTTAsync_disconnectOptions *options,
                              int calls)
{
    disconnect_options = *options;
    TEST_ASSERT_NOT_NULL(options->onSuccess);
    TEST_ASSERT_NOT_NULL(options->onFailure);
    return MQTTASYNC_SUCCESS;
}

static void verify_destroy_outside_callback(MQTTAsync *actual, int calls)
{
    TEST_ASSERT_FALSE_MESSAGE(inside_callback, "Client destruction must wait until the Paho callback returns");
}

static void request_stop(void)
{
    if (MQTT_IsClosed())
        start_client();
    MQTTAsync_disconnect_ExpectAndReturn(handle, NULL, MQTTASYNC_SUCCESS);
    MQTTAsync_disconnect_IgnoreArg_options();
    MQTTAsync_disconnect_AddCallback(capture_disconnect);
    MQTT_Stop();
}

void test_stop_disconnects_and_success_callback_destroys_client(void)
{
    request_stop();
    MQTTAsync_destroy_Expect(&client);
    MQTTAsync_destroy_IgnoreArg_handle();
    MQTTAsync_destroy_AddCallback(verify_destroy_outside_callback);
    inside_callback = true;
    disconnect_options.onSuccess(disconnect_options.context, NULL);
    inside_callback = false;
    MQTT_Poll();
}

static char incoming_topic[] = "dmx/channels/16/set";
static char incoming_payload[] = "199";
static MQTTAsync_message incoming_message;
static int incoming_topic_length;

static void prepare_arrival(int topic_length)
{
    incoming_message = (MQTTAsync_message)MQTTAsync_message_initializer;
    incoming_message.payload = incoming_payload;
    incoming_message.payloadlen = 3;
    incoming_topic_length = topic_length;
    received_paho_message = &incoming_message;
    MQTTAsync_freeMessage_Expect(&received_paho_message);
    MQTTAsync_freeMessage_IgnoreArg_msg();
    MQTTAsync_freeMessage_AddCallback(check_paho_message_release);
    MQTTAsync_free_Expect(incoming_topic);
}

static void deliver_message(void)
{
    TEST_ASSERT_EQUAL_INT(1, arrived(handle, incoming_topic, incoming_topic_length, &incoming_message));
}

static void deliver_and_poll(void)
{
    deliver_message();
    MQTT_Poll();
}

void test_given_zero_topic_length_when_message_arrives_then_delivers_full_topic_and_releases_buffers(void)
{
    // Given (Arrange)
    start_client();
    prepare_arrival(0);
    verify_child_before_exit(mock_paho_api_Verify);

    // When (Act)
    struct child_result result = run_child(deliver_and_poll);

    // Then (Assert)
    mock_paho_api_Destroy();
    assert_child_success(&result);
}

enum failure_stage
{
    CREATE_FAILURE,
    CALLBACK_FAILURE,
    CONNECT_FAILURE,
    ASYNC_CONNECT_FAILURE,
    SUBSCRIBE_FAILURE,
    ASYNC_SUBSCRIBE_FAILURE,
    DISCONNECT_FAILURE,
    ASYNC_DISCONNECT_FAILURE,
    SEND_FAILURE,
    ASYNC_SEND_FAILURE
};

static int failing_stage;
static int null_response;

static void invoke_failure(void)
{
    MQTTAsync_failureData failure = {0};
    MQTTAsync_successData success = {0};
    failure.code = -3;
    success.alt.connect.serverURI = "tcp://test.invalid:1883";
    inside_callback = failing_stage == ASYNC_CONNECT_FAILURE ||
                      failing_stage == SUBSCRIBE_FAILURE ||
                      failing_stage == ASYNC_SUBSCRIBE_FAILURE ||
                      failing_stage == ASYNC_DISCONNECT_FAILURE ||
                      failing_stage == ASYNC_SEND_FAILURE;
    switch (failing_stage)
    {
    case CREATE_FAILURE:
    case CALLBACK_FAILURE:
    case CONNECT_FAILURE:
        MQTT_Start("tcp://test.invalid:1883", "test-client", "dmx/channels/+/set", consumer);
        break;
    case ASYNC_CONNECT_FAILURE:
        connect_options.onFailure(handle, null_response ? NULL : &failure);
        break;
    case SUBSCRIBE_FAILURE:
        connected(NULL, NULL);
        break;
    case ASYNC_SUBSCRIBE_FAILURE:
        subscribe_options.onFailure(handle, null_response ? NULL : &failure);
        break;
    case DISCONNECT_FAILURE:
        MQTT_Stop();
        break;
    case ASYNC_DISCONNECT_FAILURE:
        disconnect_options.onFailure(handle, null_response ? NULL : &failure);
        break;
    case SEND_FAILURE:
        MQTT_PublishAndFree(published);
        break;
    case ASYNC_SEND_FAILURE:
        send_options.onFailure(send_options.context, null_response ? NULL : &failure);
        break;
    }
    inside_callback = false;
    int status = MQTT_Poll();
    if (status != EXIT_SUCCESS)
    {
        MQTT_Close();
        exit(status);
    }
}

static void prepare_failure(int stage)
{
    if (stage <= CONNECT_FAILURE)
    {
        MQTTAsync_create_ExpectAndReturn(&client, "tcp://test.invalid:1883", "test-client",
            MQTTCLIENT_PERSISTENCE_NONE, NULL, stage == CREATE_FAILURE ? -3 : MQTTASYNC_SUCCESS);
        MQTTAsync_create_IgnoreArg_handle();
        if (stage != CREATE_FAILURE)
        {
            MQTTAsync_create_ReturnThruPtr_handle(&handle);
            MQTTAsync_setCallbacks_ExpectAndReturn(handle, NULL, NULL, NULL, NULL,
                stage == CALLBACK_FAILURE ? -3 : MQTTASYNC_SUCCESS);
            MQTTAsync_setCallbacks_IgnoreArg_cl();
            MQTTAsync_setCallbacks_IgnoreArg_ma();
        }
        if (stage == CONNECT_FAILURE)
        {
            MQTTAsync_setConnected_ExpectAndReturn(handle, NULL, NULL, 0);
            MQTTAsync_setConnected_IgnoreArg_co();
            MQTTAsync_connect_ExpectAndReturn(handle, NULL, -3);
            MQTTAsync_connect_IgnoreArg_options();
        }
    }
    else
    {
        start_client();
        switch (stage)
        {
        case SUBSCRIBE_FAILURE:
            MQTTAsync_subscribe_ExpectAndReturn(handle, "dmx/channels/+/set", 0, NULL, -3);
            MQTTAsync_subscribe_IgnoreArg_response();
            break;
        case ASYNC_SUBSCRIBE_FAILURE:
        {
            MQTTAsync_subscribe_ExpectAndReturn(handle, "dmx/channels/+/set", 0, NULL, 0);
            MQTTAsync_subscribe_IgnoreArg_response();
            MQTTAsync_subscribe_AddCallback(capture_subscribe);
            MQTTAsync_successData success = {0};
            success.alt.connect.serverURI = "tcp://test.invalid:1883";
            connected(NULL, NULL);
            MQTT_Poll();
            break;
        }
        case DISCONNECT_FAILURE:
            MQTTAsync_disconnect_ExpectAndReturn(handle, NULL, -3);
            MQTTAsync_disconnect_IgnoreArg_options();
            break;
        case ASYNC_DISCONNECT_FAILURE:
            request_stop();
            break;
        case SEND_FAILURE:
            published = MQTT_CreateMessage("dmx/channels/16", 15, "199", 3);
            watch_message(published);
            MQTTAsync_sendMessage_ExpectAndReturn(handle, published->topic, NULL, NULL, -3);
            MQTTAsync_sendMessage_IgnoreArg_msg();
            MQTTAsync_sendMessage_IgnoreArg_response();
            break;
        case ASYNC_SEND_FAILURE:
            publish_message();
            break;
        }
    }
    if (stage != CREATE_FAILURE)
    {
        MQTTAsync_destroy_Expect(&client);
        MQTTAsync_destroy_IgnoreArg_handle();
        MQTTAsync_destroy_AddCallback(verify_destroy_outside_callback);
    }
    verify_child_before_exit(mock_paho_api_Verify);
}

TEST_CASE(0, 0, "create")
TEST_CASE(1, 0, "callback")
TEST_CASE(2, 0, "connect failed")
TEST_CASE(3, 0, "connect failed, return code -3")
TEST_CASE(3, 1, "connect failed, return code 0")
TEST_CASE(4, 0, "subscribe")
TEST_CASE(5, 0, "failed, return code -3")
TEST_CASE(5, 1, "failed, return code 0")
TEST_CASE(6, 0, "disconnect failed")
TEST_CASE(7, 0, "disconnect failed, return code -3")
TEST_CASE(7, 1, "disconnect failed, return code 0")
TEST_CASE(8, 0, "sendMessage")
TEST_CASE(9, 0, "send failed, return code -3")
TEST_CASE(9, 1, "send failed, return code 0")
void test_given_mqtt_failure_when_operation_completes_then_cleans_up_and_exits_with_diagnostic(
    int stage, int missing_response, const char *diagnostic)
{
    // Given (Arrange)
    failing_stage = stage;
    null_response = missing_response;
    prepare_failure(stage);

    // When (Act)
    struct child_result result = run_child(invoke_failure);

    // Then (Assert)
    mock_paho_api_Destroy();
    watch_frees(NULL, 0);
    if (published && stage == SEND_FAILURE)
        MQTT_Free(published);
    assert_child_failure(&result, EXIT_FAILURE, 0, diagnostic);
}

static unsigned int failed_allocation;

static void allocate_message(void)
{
    fail_malloc_on_call(failed_allocation);
    MQTTMessage *message = MQTT_CreateMessage("topic", 5, "199", 3);
    MQTT_Free(message);
    if (MQTT_HasFailed())
        exit(EXIT_FAILURE);
}

TEST_CASE(1)
TEST_CASE(2)
TEST_CASE(3)
void test_given_allocation_failure_when_creating_message_then_frees_partial_message_and_exits(
    unsigned int allocation)
{
    // Given (Arrange)
    failed_allocation = allocation;

    // When (Act)
    struct child_result result = run_child(allocate_message);

    // Then (Assert)
    assert_child_failure(&result, EXIT_FAILURE, 0, "alloc");
}

static void deliver_with_allocation_failure(void)
{
    fail_malloc_on_call(failed_allocation);
    inside_callback = true;
    deliver_message();
    inside_callback = false;
    int status = MQTT_Poll();
    if (status != EXIT_SUCCESS)
    {
        MQTT_Close();
        exit(status);
    }
}

static void check_failed_arrival_release(MQTTAsync_message **message, int calls)
{
    TEST_ASSERT_EQUAL_PTR(&incoming_message, *message);
}

TEST_CASE(1)
TEST_CASE(2)
TEST_CASE(3)
void test_given_arrival_allocation_failure_when_copying_message_then_releases_paho_buffers_and_client(
    unsigned int allocation)
{
    // Given (Arrange)
    failed_allocation = allocation;
    start_client();
    prepare_arrival(strlen(incoming_topic));
    // A failed copy must not invoke the consumer.
    MQTTAsync_freeMessage_AddCallback(check_failed_arrival_release);
    MQTTAsync_destroy_Expect(&client);
    MQTTAsync_destroy_IgnoreArg_handle();
    MQTTAsync_destroy_AddCallback(verify_destroy_outside_callback);
    verify_child_before_exit(mock_paho_api_Verify);

    // When (Act)
    struct child_result result = run_child(deliver_with_allocation_failure);

    // Then (Assert)
    mock_paho_api_Destroy();
    assert_child_failure(&result, EXIT_FAILURE, 0, "alloc");
}

static int capture_empty_send(MQTTAsync actual, const char *topic, const MQTTAsync_message *message,
                              MQTTAsync_responseOptions *options, int calls)
{
    TEST_ASSERT_EQUAL_STRING("topic", topic);
    TEST_ASSERT_EQUAL_INT(0, message->payloadlen);
    TEST_ASSERT_EQUAL_STRING("", message->payload);
    TEST_ASSERT_EQUAL_INT(0, message->qos);
    TEST_ASSERT_EQUAL_INT(0, message->retained);
    send_options = *options;
    return MQTTASYNC_SUCCESS;
}

void test_given_empty_payload_when_published_then_sends_zero_bytes_and_releases_after_completion(void)
{
    // Given (Arrange)
    start_client();
    published = MQTT_CreateMessage("topic", 5, "", 0);
    watch_message(published);
    MQTTAsync_sendMessage_ExpectAndReturn(handle, published->topic, NULL, NULL, 0);
    MQTTAsync_sendMessage_IgnoreArg_msg();
    MQTTAsync_sendMessage_IgnoreArg_response();
    MQTTAsync_sendMessage_AddCallback(capture_empty_send);

    // When (Act)
    MQTT_PublishAndFree(published);

    // Then (Assert)
    mock_paho_api_Verify();
    assert_no_watched_frees();
    TEST_ASSERT_NOT_NULL(send_options.onSuccess);
    TEST_ASSERT_EQUAL_PTR(published, send_options.context);
    send_options.onSuccess(send_options.context, NULL);
    assert_watched_frees();
}

void test_given_lost_connection_when_reconnect_succeeds_then_resubscribes_to_configured_topic(void)
{
    // Given (Arrange)
    start_client();
    MQTTAsync_successData response = {0};
    response.alt.connect.serverURI = "tcp://test.invalid:1883";
    MQTTAsync_subscribe_ExpectAndReturn(handle, "dmx/channels/+/set", 0, NULL, 0);
    MQTTAsync_subscribe_IgnoreArg_response();
    connected(NULL, NULL);
    MQTT_Poll();
    MQTTAsync_subscribe_ExpectAndReturn(handle, "dmx/channels/+/set", 0, NULL, 0);
    MQTTAsync_subscribe_IgnoreArg_response();

    // When (Act)
    lost(handle, "connection interrupted");
    connected(NULL, "reconnected");
    MQTT_Poll();

    // Then (Assert)
    mock_paho_api_Verify();
}

static void count_message(MQTTMessage *message)
{
    received_count++;
    MQTT_Free(message);
}

static void expect_arrivals(char *topic, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        MQTTAsync_freeMessage_Expect(NULL);
        MQTTAsync_freeMessage_IgnoreArg_msg();
        MQTTAsync_free_Expect(topic);
    }
}

void test_given_full_queue_when_polled_then_delivers_only_sixteen_messages_per_frame(void)
{
    // Given (Arrange)
    consumer_callback = count_message;
    start_client();
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = "1";
    message.payloadlen = 1;
    char topic[] = "/1/set";
    expect_arrivals(topic, MQTT_QUEUE_CAPACITY);
    // When (Act)
    for (size_t i = 0; i < MQTT_QUEUE_CAPACITY; i++)
        TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, 0, &message));
    TEST_ASSERT_EQUAL_INT(0, received_count);
    for (int frame = 1; frame <= 4; frame++)
    {
        TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
        // Then (Assert)
        TEST_ASSERT_EQUAL_INT(frame * 16, received_count);
    }
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    TEST_ASSERT_EQUAL_INT(64, received_count);
}

void test_given_saturated_queue_when_another_message_arrives_then_reports_failure_without_delivery(void)
{
    // Given (Arrange)
    consumer_callback = count_message;
    start_client();
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = "1";
    message.payloadlen = 1;
    char topic[] = "/1/set";
    expect_arrivals(topic, MQTT_QUEUE_CAPACITY + 1);
    // When (Act)
    for (size_t i = 0; i <= MQTT_QUEUE_CAPACITY; i++)
        TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, 0, &message));
    int status = MQTT_Poll();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    TEST_ASSERT_TRUE(MQTT_HasFailed());
    TEST_ASSERT_EQUAL_INT(0, received_count);
}

TEST_CASE(1024, 64, 0)
TEST_CASE(1025, 64, 1)
TEST_CASE(1024, 65, 1)
TEST_CASE(-1, 1, 1)
TEST_CASE(1, -1, 1)
void test_given_message_length_limits_when_arriving_then_accepts_boundaries_and_rejects_excess(
    int topic_length, int payload_length, int rejected)
{
    // Given (Arrange)
    consumer_callback = count_message;
    start_client();
    char topic[1026], payload[66];
    memset(topic, 't', sizeof(topic));
    topic[1025] = '\0';
    memset(payload, '1', sizeof(payload));
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = payload;
    message.payloadlen = payload_length;
    expect_arrivals(topic, 1);
    // When (Act)
    TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, topic_length, &message));
    int status = MQTT_Poll();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(rejected ? EXIT_FAILURE : EXIT_SUCCESS, status);
    TEST_ASSERT_EQUAL_INT(rejected ? 0 : 1, received_count);
}

void test_given_oversize_terminated_topic_when_zero_length_arrives_then_checks_bounded_length(void)
{
    // Given (Arrange)
    start_client();
    char topic[1026];
    memset(topic, 't', sizeof(topic));
    topic[1025] = '\0';
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = "1";
    message.payloadlen = 1;
    expect_arrivals(topic, 1);
    // When (Act)
    TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, 0, &message));
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, MQTT_Poll());
    TEST_ASSERT_EQUAL_INT(0, received_count);
}

void test_given_queued_commands_when_stopping_then_discards_them_and_rejects_later_arrivals(void)
{
    // Given (Arrange)
    consumer_callback = count_message;
    start_client();
    char topic[] = "/1/set";
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = "1";
    message.payloadlen = 1;
    expect_arrivals(topic, 1);
    TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, 0, &message));
    MQTTAsync_disconnect_ExpectAndReturn(handle, NULL, 0);
    MQTTAsync_disconnect_IgnoreArg_options();
    expect_arrivals(topic, 1);
    // When (Act)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Stop());
    TEST_ASSERT_EQUAL_INT(1, arrived(NULL, topic, 0, &message));
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(0, received_count);
    TEST_ASSERT_FALSE(MQTT_IsClosed());
}

void test_given_pending_publication_when_stopping_then_waits_for_send_completion_before_disconnect(void)
{
    // Given (Arrange)
    publish_message();
    // When (Act)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Stop());
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    // Then (Assert)
    assert_no_watched_frees();
    TEST_ASSERT_FALSE(MQTT_IsClosed());
    // When (Act)
    send_options.onSuccess(send_options.context, NULL);
    // Then (Assert)
    assert_watched_frees();
    MQTTAsync_disconnect_ExpectAndReturn(handle, NULL, 0);
    MQTTAsync_disconnect_IgnoreArg_options();
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
}

void test_given_pending_publication_when_forced_closed_then_releases_it_once_after_destroy(void)
{
    // Given (Arrange)
    publish_message();
    MQTTAsync_destroy_Expect(NULL);
    MQTTAsync_destroy_IgnoreArg_handle();
    // When (Act)
    MQTT_Close();
    MQTT_Close();
    // Then (Assert)
    assert_watched_frees();
    TEST_ASSERT_TRUE(MQTT_IsClosed());
}

static void fail_pending_send_during_destroy(MQTTAsync *actual, int calls)
{
    send_options.onFailure(send_options.context, NULL);
}

void test_given_send_failure_callback_during_destroy_when_closed_then_does_not_double_free(void)
{
    // Given (Arrange)
    publish_message();
    MQTTAsync_destroy_Expect(NULL);
    MQTTAsync_destroy_IgnoreArg_handle();
    MQTTAsync_destroy_AddCallback(fail_pending_send_during_destroy);
    // When (Act)
    MQTT_Close();
    // Then (Assert)
    assert_watched_frees();
    TEST_ASSERT_TRUE(MQTT_HasFailed());
    TEST_ASSERT_TRUE(MQTT_IsClosed());
}

TEST_CASE(0)
TEST_CASE(1)
void test_given_established_connection_when_automatic_reconnect_attempt_fails_then_paho_keeps_retrying(
    int null_response)
{
    // Given (Arrange)
    start_client();
    MQTTAsync_subscribe_ExpectAndReturn(handle, "dmx/channels/+/set", 0, NULL, 0);
    MQTTAsync_subscribe_IgnoreArg_response();
    connected(NULL, NULL);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
    MQTTAsync_failureData failure = {0};
    failure.code = -3;
    // When (Act)
    lost(NULL, "interrupted");
    connect_options.onFailure(NULL, null_response ? NULL : &failure);
    bool failed = MQTT_HasFailed();
    int status = MQTT_Poll();
    MQTTAsync_destroy_Expect(NULL);
    MQTTAsync_destroy_IgnoreArg_handle();
    MQTT_Close();
    // Then (Assert)
    TEST_ASSERT_FALSE(failed);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
}

void test_given_connected_callback_registration_failure_when_started_then_caller_can_release_partial_client(void)
{
    // Given (Arrange)
    MQTTAsync_create_ExpectAndReturn(NULL, "test", "test", MQTTCLIENT_PERSISTENCE_NONE, NULL, 0);
    MQTTAsync_create_IgnoreArg_handle();
    MQTTAsync_create_ReturnThruPtr_handle(&handle);
    MQTTAsync_setCallbacks_ExpectAndReturn(handle, NULL, NULL, NULL, NULL, 0);
    MQTTAsync_setCallbacks_IgnoreArg_cl();
    MQTTAsync_setCallbacks_IgnoreArg_ma();
    MQTTAsync_setConnected_ExpectAndReturn(handle, NULL, NULL, -3);
    MQTTAsync_setConnected_IgnoreArg_co();
    MQTTAsync_destroy_Expect(NULL);
    MQTTAsync_destroy_IgnoreArg_handle();
    // When (Act)
    int status = MQTT_Start("test", "test", "/+/set", consumer);
    MQTT_Close();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    TEST_ASSERT_TRUE(MQTT_IsClosed());
    TEST_ASSERT_TRUE(MQTT_HasFailed());
}

void test_given_two_publications_when_one_completes_and_other_is_closed_then_each_is_freed_once(void)
{
    // Given (Arrange)
    start_client();
    MQTTMessage *first = MQTT_CreateMessage("topic", 5, "", 0);
    MQTTMessage *second = MQTT_CreateMessage("topic", 5, "", 0);
    void *pointers[] = {first->topic, first->payload, first, second->topic, second->payload, second};
    watch_frees(pointers, 6);
    MQTTAsync_sendMessage_ExpectAndReturn(handle, first->topic, NULL, NULL, 0);
    MQTTAsync_sendMessage_IgnoreArg_msg();
    MQTTAsync_sendMessage_IgnoreArg_response();
    MQTTAsync_sendMessage_AddCallback(capture_empty_send);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_PublishAndFree(first));
    MQTTAsync_responseOptions first_options = send_options;
    MQTTAsync_sendMessage_ExpectAndReturn(handle, second->topic, NULL, NULL, 0);
    MQTTAsync_sendMessage_IgnoreArg_msg();
    MQTTAsync_sendMessage_IgnoreArg_response();
    MQTTAsync_destroy_Expect(NULL);
    MQTTAsync_destroy_IgnoreArg_handle();
    // When (Act)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_PublishAndFree(second));
    first_options.onSuccess(first_options.context, NULL);
    MQTT_Close();
    // Then (Assert)
    assert_watched_frees();
    TEST_ASSERT_TRUE(MQTT_IsClosed());
}
