#include "unity.h"
#include "ha.h"
#include "mock_mqtt.h"
#include <stdlib.h>
#include <string.h>

TEST_SOURCE_FILE("config.c")
TEST_SOURCE_FILE("json_contract.c")
TEST_SOURCE_FILE("transition.c")
TEST_SOURCE_FILE("ha_protocol.c")
TEST_SOURCE_FILE("ha_topics.c")
TEST_SOURCE_FILE("lights.c")

static GatewayConfig config;
static HomeAssistant ha;
static LightState lights;
static MQTTMessage message;
static char published_topic[HA_TOPIC_LIMIT], published_payload[DISCOVERY_LIMIT];
static size_t pending, sends;
static bool online;
static unsigned int epoch;

static bool connected(int calls) { return online; }
static unsigned int connection_epoch(int calls) { return epoch; }
static size_t publications(int calls) { return pending; }

static MQTTMessage *create(const char *topic, int topicLen, const char *payload, int payloadLen, int calls)
{
    TEST_ASSERT_LESS_THAN(sizeof(published_topic), topicLen);
    TEST_ASSERT_LESS_THAN(sizeof(published_payload), payloadLen);
    memcpy(published_topic, topic, topicLen);
    published_topic[topicLen] = 0;
    memcpy(published_payload, payload, payloadLen);
    published_payload[payloadLen] = 0;
    message = (MQTTMessage){.topic = published_topic, .payload = published_payload};
    return &message;
}

static int publish(MQTTMessage *owned, int calls)
{
    TEST_ASSERT_TRUE(owned->retained);
    TEST_ASSERT_EQUAL_UINT(0, pending);
    pending++;
    sends++;
    return EXIT_SUCCESS;
}

void setUp(void)
{
    memset(&config, 0, sizeof(config));
    strcpy(config.gateway_id, "gateway");
    strcpy(config.name, "Gateway");
    strcpy(config.prefix, "dmx");
    strcpy(config.discovery_prefix, "homeassistant");
    strcpy(config.birth_topic, "homeassistant/status");
    config.discovery = true;
    config.count = 2;
    config.lights[0] = (LightConfig){.id = "one", .name = "One", .channel = 10,
        .minimum = 66, .duration_ms = 1000, .curve = CURVE_LINEAR};
    config.lights[1] = (LightConfig){.id = "two", .name = "Two", .channel = 11,
        .minimum = 0, .duration_ms = 1000, .curve = CURVE_SMOOTHSTEP};
    HA_Initialize(&ha, &config);
    Lights_Initialize(&lights);
    pending = sends = 0;
    online = true;
    epoch = 1;
    published_topic[0] = published_payload[0] = 0;
    MQTT_IsConnected_Stub(connected);
    MQTT_ConnectionEpoch_Stub(connection_epoch);
    MQTT_PublicationsPending_Stub(publications);
    MQTT_CreateMessage_Stub(create);
    MQTT_PublishAndFree_Stub(publish);
}

void tearDown(void) {}

static MQTTMessage input(const char *topic, const char *payload)
{
    return (MQTTMessage){.topic = (char *)topic, .payload = (char *)payload,
        .topic_length = strlen(topic), .payload_length = strlen(payload)};
}

void test_given_light_command_when_owner_applies_then_emits_calibrated_midpoint_and_target_state(void)
{
    // Given (Arrange)
    MQTTMessage command = input("dmx/lights/one/set", "{\"state\":\"ON\"}");
    // When (Act)
    bool handled = HA_Receive(&ha, &command, 100);
    bool rendered = HA_Render(&ha, &lights, 600);
    // Then (Assert)
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_TRUE(rendered);
    TEST_ASSERT_EQUAL_UINT8(160, lights.slots[10]);
    TEST_ASSERT_EQUAL_UINT8(0, lights.slots[0]);
    TEST_ASSERT_EQUAL_UINT8(0, lights.slots[11]);
    TEST_ASSERT_EQUAL_UINT8(255, Transition_Target(&ha.transitions[0]));
    TEST_ASSERT_TRUE(ha.dirty[0]);
}

void test_given_two_fades_when_one_is_raw_overridden_then_other_continues(void)
{
    // Given (Arrange)
    MQTTMessage one = input("dmx/lights/one/set", "{\"state\":\"ON\"}");
    MQTTMessage two = input("dmx/lights/two/set", "{\"state\":\"ON\"}");
    HA_Receive(&ha, &one, 0);
    HA_Receive(&ha, &two, 0);
    // When (Act)
    HA_Raw(&ha, 10, 10);
    HA_Render(&ha, &lights, 500);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(10, lights.slots[10]);
    TEST_ASSERT_EQUAL_UINT8(127, lights.slots[11]);
    TEST_ASSERT_EQUAL_UINT8(1, Transition_Target(&ha.transitions[0]));
    TEST_ASSERT_FALSE(ha.transitions[0].active);
    TEST_ASSERT_TRUE(ha.transitions[1].active);
}

void test_given_endpoint_frame_when_not_yet_submitted_then_completion_remains_pending(void)
{
    // Given (Arrange)
    MQTTMessage command = input("dmx/lights/one/set", "{\"state\":\"ON\",\"transition\":0}");
    HA_Receive(&ha, &command, 0);
    // When (Act)
    HA_Render(&ha, &lights, 0);
    bool pending_before_write = ha.transitions[0].endpoint_pending;
    HA_Submitted(&ha);
    // Then (Assert)
    TEST_ASSERT_TRUE(pending_before_write);
    TEST_ASSERT_FALSE(ha.transitions[0].endpoint_pending);
}

void test_given_unknown_light_when_received_then_does_not_fall_back_to_raw_channel(void)
{
    // Given (Arrange)
    MQTTMessage command = input("dmx/lights/10/set", "255");
    // When (Act)
    bool handled = HA_Receive(&ha, &command, 0);
    // Then (Assert)
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_FALSE(ha.dirty[0]);
    TEST_ASSERT_EQUAL_UINT(0, sends);
}

void test_given_startup_when_serviced_then_waits_for_each_snapshot_before_online(void)
{
    // Given (Arrange)
    const char *topics[] = {"dmx/availability",
        "homeassistant/light/mqtt2dmx_gateway_one/config",
        "homeassistant/light/mqtt2dmx_gateway_two/config",
        "dmx/lights/one/state", "dmx/lights/two/state", "dmx/availability"};
    char actual[6][HA_TOPIC_LIMIT], payloads[6][DISCOVERY_LIMIT];
    size_t counts[6];
    // When (Act)
    for (size_t i = 0; i < 6; i++)
    {
        pending = 0;
        HA_Service(&ha);
        strcpy(actual[i], published_topic);
        strcpy(payloads[i], published_payload);
        HA_Service(&ha);
        counts[i] = sends;
    }
    // Then (Assert)
    for (size_t i = 0; i < 6; i++)
    {
        TEST_ASSERT_EQUAL_STRING(topics[i], actual[i]);
        TEST_ASSERT_EQUAL_UINT(i + 1, counts[i]);
    }
    TEST_ASSERT_EQUAL_STRING("offline", payloads[0]);
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"OFF\",\"brightness\":0}", payloads[3]);
    TEST_ASSERT_EQUAL_STRING("online", payloads[5]);
}

void test_given_disconnected_gateway_when_fading_then_skips_publications_but_reaches_endpoint(void)
{
    // Given (Arrange)
    online = false;
    MQTTMessage command = input("dmx/lights/one/set", "{\"state\":\"ON\"}");
    HA_Receive(&ha, &command, 0);
    // When (Act)
    HA_Service(&ha);
    HA_Render(&ha, &lights, 1000);
    HA_Submitted(&ha);
    online = true;
    HA_Service(&ha);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT(1, sends);
    TEST_ASSERT_EQUAL_STRING("offline", published_payload);
    TEST_ASSERT_EQUAL_UINT8(255, lights.slots[10]);
    TEST_ASSERT_FALSE(ha.transitions[0].active);
}

static void ready(void)
{
    for (int i = 0; i < 6; i++)
    {
        pending = 0;
        HA_Service(&ha);
    }
    pending = 0;
    sends = 0;
}

void test_given_ha_birth_burst_when_refreshing_then_coalesces_without_restarting_fade(void)
{
    // Given (Arrange)
    ready();
    MQTTMessage command = input("dmx/lights/one/set", "{\"state\":\"ON\"}");
    MQTTMessage birth = input("homeassistant/status", "online");
    HA_Receive(&ha, &command, 100);
    // When (Act)
    HA_Receive(&ha, &birth, 200);
    HA_Service(&ha);
    HA_Receive(&ha, &birth, 300);
    pending = 0;
    HA_Service(&ha);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT(2, sends);
    TEST_ASSERT_EQUAL_STRING("homeassistant/light/mqtt2dmx_gateway_one/config", published_topic);
    TEST_ASSERT_EQUAL_UINT64(100, ha.transitions[0].started);
}

void test_given_changed_connection_epoch_when_serviced_then_republishes_current_configuration(void)
{
    // Given (Arrange)
    ready();
    epoch++;
    // When (Act)
    HA_Service(&ha);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT(1, sends);
    TEST_ASSERT_EQUAL_STRING("offline", published_payload);
}

void test_given_removal_manifest_with_discovery_disabled_when_restarted_then_cleans_exact_retained_topics(void)
{
    // Given (Arrange)
    config.discovery = false;
    config.removal_count = 1;
    strcpy(config.removals[0].discovery, "old/light/mqtt2dmx_old_removed/config");
    strcpy(config.removals[0].state, "old/lights/removed/state");
    char first[HA_TOPIC_LIMIT], second[HA_TOPIC_LIMIT];
    size_t first_length, second_length;
    // When (Act)
    HA_Service(&ha);
    pending = 0;
    HA_Service(&ha);
    strcpy(first, published_topic);
    first_length = strlen(published_payload);
    pending = 0;
    HA_Service(&ha);
    strcpy(second, published_topic);
    second_length = strlen(published_payload);
    // Then (Assert)
    TEST_ASSERT_EQUAL_STRING(config.removals[0].discovery, first);
    TEST_ASSERT_EQUAL_STRING(config.removals[0].state, second);
    TEST_ASSERT_EQUAL_UINT(0, first_length);
    TEST_ASSERT_EQUAL_UINT(0, second_length);
    TEST_ASSERT_EQUAL_UINT(3, sends);
}

void test_given_pending_cleanup_while_offline_when_reconnected_then_retries_manifest_from_start(void)
{
    // Given (Arrange)
    config.removal_count = 1;
    strcpy(config.removals[0].discovery, "old/light/mqtt2dmx_old_removed/config");
    strcpy(config.removals[0].state, "old/lights/removed/state");
    HA_Service(&ha);
    online = false;
    pending = 0;
    // When (Act)
    HA_Service(&ha);
    size_t before = sends;
    online = true;
    epoch++;
    HA_Service(&ha);
    pending = 0;
    HA_Service(&ha);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT(1, before);
    TEST_ASSERT_EQUAL_STRING(config.removals[0].discovery, published_topic);
    TEST_ASSERT_EQUAL_STRING("", published_payload);
}

void test_given_rapid_targets_when_publication_pending_then_coalesces_latest_target(void)
{
    // Given (Arrange)
    ready();
    MQTTMessage first = input("dmx/lights/one/set", "{\"brightness\":128}");
    MQTTMessage last = input("dmx/lights/one/set", "{\"brightness\":64}");
    HA_Receive(&ha, &first, 0);
    HA_Service(&ha);
    // When (Act)
    HA_Receive(&ha, &last, 100);
    HA_Service(&ha);
    size_t before = sends;
    pending = 0;
    HA_Service(&ha);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT(1, before);
    TEST_ASSERT_EQUAL_UINT(2, sends);
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"ON\",\"brightness\":64}", published_payload);
}

void test_given_publication_failure_when_refreshing_then_propagates_fatal_status(void)
{
    // Given (Arrange)
    MQTT_PublishAndFree_Stub(NULL);
    MQTT_PublishAndFree_ExpectAndReturn(&message, EXIT_FAILURE);
    // When (Act)
    int status = HA_Service(&ha);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
}

void test_given_retained_light_command_when_received_then_does_not_restore_stale_hardware_state(void)
{
    // Given (Arrange)
    MQTTMessage command = input("dmx/lights/one/set", "{\"state\":\"ON\",\"transition\":0}");
    command.retained = true;
    // When (Act)
    bool handled = HA_Receive(&ha, &command, 0);
    HA_Render(&ha, &lights, 0);
    // Then (Assert)
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_UINT8(0, lights.slots[10]);
    TEST_ASSERT_FALSE(ha.dirty[0]);
}
