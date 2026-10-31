#include "unity.h"
#include "ha_protocol.h"
#include <jansson.h>
#include <string.h>

TEST_SOURCE_FILE("config.c")
TEST_SOURCE_FILE("json_contract.c")
TEST_SOURCE_FILE("transition.c")

void setUp(void) {}
void tearDown(void) {}

TEST_CASE("{\"state\":\"ON\",\"brightness\":128,\"transition\":2}", 2000)
TEST_CASE(" { \"transition\": 1.5005, \"brightness\": 128, \"state\": \"ON\" } ", 1501)
void test_given_home_assistant_command_when_decoded_then_preserves_optional_fields(
    const char *payload, int duration)
{
    // Given (Arrange)
    LightCommand command = {0};
    // When (Act)
    bool valid = HA_Decode(payload, strlen(payload), &command);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_TRUE(command.has_state);
    TEST_ASSERT_TRUE(command.on);
    TEST_ASSERT_TRUE(command.has_brightness);
    TEST_ASSERT_EQUAL_UINT8(128, command.brightness);
    TEST_ASSERT_TRUE(command.has_duration);
    TEST_ASSERT_EQUAL_UINT32(duration, command.duration_ms);
}

TEST_CASE("{}")
TEST_CASE("{\"transition\":1}")
TEST_CASE("{\"state\":\"on\"}")
TEST_CASE("{\"state\":true}")
TEST_CASE("{\"state\":\"ON\",\"state\":\"OFF\"}")
TEST_CASE("{\"brightness\":1.5}")
TEST_CASE("{\"brightness\":true}")
TEST_CASE("{\"brightness\":-1}")
TEST_CASE("{\"brightness\":256}")
TEST_CASE("{\"brightness\":null}")
TEST_CASE("{\"state\":\"ON\",\"transition\":-1}")
TEST_CASE("{\"state\":\"ON\",\"transition\":3600.001}")
TEST_CASE("{\"state\":\"ON\",\"transition\":\"2\"}")
TEST_CASE("{\"state\":\"ON\",\"transition\":1e999}")
TEST_CASE("{\"state\":\"ON\",\"color\":{}}")
TEST_CASE("{\"state\":\"ON\"} trailing")
TEST_CASE("{\"state\":\"ON\",}")
TEST_CASE("{\"state\":\"ON\\u0000OFF\"}")
TEST_CASE("[]")
void test_given_invalid_json_when_decoded_then_rejects_without_mutation(const char *payload)
{
    // Given (Arrange)
    LightCommand command = {.brightness = 42};
    // When (Act)
    bool valid = HA_Decode(payload, strlen(payload), &command);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_EQUAL_UINT8(42, command.brightness);
}

TEST_CASE("{\"brightness\":0}")
TEST_CASE("{\"state\":\"ON\",\"brightness\":0}")
TEST_CASE("{\"state\":\"OFF\",\"brightness\":128}")
TEST_CASE("{\"state\":\"ON\"}")
TEST_CASE("{\"state\":\"OFF\",\"transition\":3600}")
void test_given_supported_edge_command_when_decoded_then_accepts_without_extra_fields(const char *payload)
{
    // Given (Arrange)
    LightCommand command;
    // When (Act)
    bool valid = HA_Decode(payload, strlen(payload), &command);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
}

void test_given_binary_payload_when_decoded_then_rejects_embedded_nul(void)
{
    // Given (Arrange)
    const char payload[] = "{\"state\":\"ON\"}\0{}";
    LightCommand command;
    // When (Act)
    bool valid = HA_Decode(payload, sizeof(payload) - 1, &command);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
}

static GatewayConfig config;

static void definition(void)
{
    memset(&config, 0, sizeof(config));
    strcpy(config.gateway_id, "gateway-1");
    strcpy(config.name, "Gateway");
    strcpy(config.prefix, "house/dmx");
    strcpy(config.discovery_prefix, "ha");
    config.count = 1;
    strcpy(config.lights[0].id, "kitchen");
    strcpy(config.lights[0].name, "Kitchen \"spots\"\n");
    config.lights[0].channel = 10;
}

void test_given_custom_prefix_when_routing_then_uses_exact_configured_id_not_channel(void)
{
    // Given (Arrange)
    definition();
    const char *topic = "house/dmx/lights/kitchen/set";
    // When (Act)
    int found = HA_Route(&config, topic, strlen(topic));
    int numeric = HA_Route(&config, "house/dmx/lights/10/set", 23);
    int other = HA_Route(&config, "other/lights/kitchen/set", 24);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(0, found);
    TEST_ASSERT_EQUAL_INT(-1, numeric);
    TEST_ASSERT_EQUAL_INT(-2, other);
}

void test_given_discovery_when_serialized_then_escapes_names_and_advertises_only_brightness(void)
{
    // Given (Arrange)
    definition();
    char payload[DISCOVERY_LIMIT], topic[HA_TOPIC_LIMIT];
    // When (Act)
    bool valid = HA_Discovery(&config, 0, payload, sizeof(payload));
    HA_Topic(&config, 0, HA_DISCOVERY_TOPIC, topic);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_STRING("ha/light/mqtt2dmx_gateway-1_kitchen/config", topic);
    json_error_t error;
    json_t *root = json_loads(payload, JSON_REJECT_DUPLICATES, &error);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL_STRING("json", json_string_value(json_object_get(root, "schema")));
    TEST_ASSERT_EQUAL_STRING(config.lights[0].name, json_string_value(json_object_get(root, "name")));
    TEST_ASSERT_EQUAL_STRING("house/dmx/lights/kitchen/set", json_string_value(json_object_get(root, "command_topic")));
    TEST_ASSERT_EQUAL_STRING("house/dmx/lights/kitchen/state", json_string_value(json_object_get(root, "state_topic")));
    TEST_ASSERT_EQUAL_STRING("house/dmx/availability", json_string_value(json_object_get(root, "availability_topic")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "transition")));
    json_t *modes = json_object_get(root, "supported_color_modes");
    TEST_ASSERT_EQUAL_UINT(1, json_array_size(modes));
    TEST_ASSERT_EQUAL_STRING("brightness", json_string_value(json_array_get(modes, 0)));
    TEST_ASSERT_NULL(json_object_get(root, "effect"));
    json_decref(root);
}

void test_given_channel_or_display_name_change_when_forming_identity_then_identity_is_stable(void)
{
    // Given (Arrange)
    definition();
    char before[HA_TOPIC_LIMIT], after[HA_TOPIC_LIMIT];
    HA_Topic(&config, 0, HA_DISCOVERY_TOPIC, before);
    // When (Act)
    config.lights[0].channel = 255;
    strcpy(config.lights[0].name, "Renamed");
    HA_Topic(&config, 0, HA_DISCOVERY_TOPIC, after);
    // Then (Assert)
    TEST_ASSERT_EQUAL_STRING(before, after);
    TEST_ASSERT_NOT_EQUAL(0, strlen(after));
}

void test_given_tiny_discovery_buffer_when_serialized_then_reports_failure(void)
{
    // Given (Arrange)
    definition();
    char payload[8];
    // When (Act)
    bool valid = HA_Discovery(&config, 0, payload, sizeof(payload));
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
}

void test_given_maximum_configured_strings_when_discovered_then_fits_bound_and_preserves_full_identity(void)
{
    // Given (Arrange)
    definition();
    memset(config.prefix, 'p', PREFIX_LIMIT);
    config.prefix[PREFIX_LIMIT] = 0;
    memset(config.discovery_prefix, 'd', PREFIX_LIMIT);
    config.discovery_prefix[PREFIX_LIMIT] = 0;
    memset(config.gateway_id, 'g', ID_LIMIT);
    config.gateway_id[ID_LIMIT] = 0;
    memset(config.lights[0].id, 'i', ID_LIMIT);
    config.lights[0].id[ID_LIMIT] = 0;
    memset(config.name, 1, NAME_LIMIT);
    config.name[NAME_LIMIT] = 0;
    memset(config.lights[0].name, 1, NAME_LIMIT);
    config.lights[0].name[NAME_LIMIT] = 0;
    char payload[DISCOVERY_LIMIT], topic[HA_TOPIC_LIMIT];
    // When (Act)
    bool valid = HA_Discovery(&config, 0, payload, sizeof(payload));
    HA_Topic(&config, 0, HA_DISCOVERY_TOPIC, topic);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    json_error_t error;
    json_t *root = json_loads(payload, 0, &error);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL_UINT(9 + ID_LIMIT + 1 + ID_LIMIT,
        json_string_length(json_object_get(root, "unique_id")));
    TEST_ASSERT_EQUAL_UINT(PREFIX_LIMIT + 7 + 9 + ID_LIMIT + 1 + ID_LIMIT + 7, strlen(topic));
    TEST_ASSERT_EQUAL_STRING(config.name,
        json_string_value(json_object_get(json_object_get(root, "device"), "name")));
    json_decref(root);
}

void test_given_payload_at_finite_limit_when_decoded_then_accepts_exact_bound_and_rejects_excess(void)
{
    // Given (Arrange)
    char payload[513];
    memset(payload, ' ', sizeof(payload));
    memcpy(payload, "{\"state\":\"ON\"}", 14);
    LightCommand command;
    // When (Act)
    bool boundary = HA_Decode(payload, 512, &command);
    bool oversized = HA_Decode(payload, 513, &command);
    // Then (Assert)
    TEST_ASSERT_TRUE(boundary);
    TEST_ASSERT_FALSE(oversized);
}

TEST_CASE("{\"state\":\"ON\",\"transition\":0.0005}", 1)
TEST_CASE("{\"state\":\"ON\",\"transition\":0.0004}", 0)
TEST_CASE("{\"state\":\"ON\",\"transition\":3600}", 3600000)
void test_given_fractional_or_maximum_duration_when_decoded_then_rounds_to_bounded_milliseconds(
    const char *payload, int expected)
{
    // Given (Arrange)
    LightCommand command;
    // When (Act)
    bool valid = HA_Decode(payload, strlen(payload), &command);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_UINT32(expected, command.duration_ms);
}
