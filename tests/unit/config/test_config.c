#include "unity.h"
#include "config.h"
#include <string.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>

TEST_SOURCE_FILE("json_contract.c")

static GatewayConfig config;
void setUp(void) { memset(&config, 0, sizeof(config)); }
void tearDown(void) {}

TEST_CASE("{\"gateway_id\":\"bad/id\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"-gateway\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"gateway_1\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"\",\"name\":\"A\",\"channel\":1}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a+\",\"name\":\"A\",\"channel\":1}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a#\",\"name\":\"A\",\"channel\":1}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a/b\",\"name\":\"A\",\"channel\":1}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"A\",\"name\":\"A\",\"channel\":1}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"\",\"channel\":1}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"A\",\"channel\":0}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"A\",\"channel\":256}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"A\",\"channel\":1,\"minimum\":256}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"A\",\"channel\":1,\"transition\":3601}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"A\",\"channel\":1,\"curve\":\"gamma\"}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[],\"unknown\":true}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"gateway_id\":\"other\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"prefix\":\"dmx/+\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"A\",\"channel\":1},{\"id\":\"a\",\"name\":\"B\",\"channel\":2}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"a\",\"name\":\"A\",\"channel\":1},{\"id\":\"b\",\"name\":\"B\",\"channel\":1}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"discovery\":\"true\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"birth_topic\":\"dmx/availability\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"birth_topic\":\"dmx/lights/one/set\",\"lights\":[]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[],\"removals\":[{\"discovery\":\"homeassistant/light/+/config\",\"state\":\"dmx/lights/old/state\"}]}")
TEST_CASE("{\"gateway_id\":\"gateway\",\"lights\":[{\"id\":\"one\",\"name\":\"One\",\"channel\":1}],\"removals\":[{\"discovery\":\"homeassistant/light/mqtt2dmx_gateway_one/config\",\"state\":\"dmx/lights/old/state\"}]}")
void test_given_invalid_config_when_parsed_then_rejects_without_partial_update(const char *input)
{
    // Given (Arrange)
    config.count = 42;
    // When (Act)
    bool valid = Config_Parse(input, strlen(input), &config);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_EQUAL_UINT(42, config.count);
}

void test_given_light_definition_when_parsed_then_maps_stable_id_and_defaults(void)
{
    // Given (Arrange)
    const char *input = "{\"gateway_id\":\"gateway-1\",\"lights\":["
        "{\"id\":\"zithoek-wandlampen\",\"name\":\"Zithoek\",\"channel\":10,\"minimum\":66}]}";
    // When (Act)
    bool valid = Config_Parse(input, strlen(input), &config);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_UINT(1, config.count);
    TEST_ASSERT_EQUAL_STRING("zithoek-wandlampen", config.lights[0].id);
    TEST_ASSERT_EQUAL_UINT(10, config.lights[0].channel);
    TEST_ASSERT_EQUAL_UINT(66, config.lights[0].minimum);
    TEST_ASSERT_EQUAL_UINT(500, config.lights[0].duration_ms);
    TEST_ASSERT_EQUAL_INT(CURVE_SMOOTHSTEP, config.lights[0].curve);
    TEST_ASSERT_EQUAL_STRING("dmx", config.prefix);
    TEST_ASSERT_TRUE(config.discovery);
}

TEST_CASE("dmx/#")
TEST_CASE("#")
TEST_CASE("dmx/lights/+/state")
TEST_CASE("homeassistant/light/+/config")
TEST_CASE("dmx/availability")
void test_given_legacy_filter_matching_outputs_when_validated_then_rejects_self_subscription(const char *filter)
{
    // Given (Arrange)
    const char *input = "{\"gateway_id\":\"gateway\",\"lights\":["
        "{\"id\":\"one\",\"name\":\"One\",\"channel\":10}]}";
    TEST_ASSERT_TRUE(Config_Parse(input, strlen(input), &config));
    // When (Act)
    bool valid = Config_ValidateSubscriptions(&config, filter);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
}

TEST_CASE("dmx/channels/+/set")
TEST_CASE("custom/+/set")
TEST_CASE("dmx/channels/10/set")
void test_given_legacy_command_filter_when_validated_then_preserves_custom_topics(const char *filter)
{
    // Given (Arrange)
    const char *input = "{\"gateway_id\":\"gateway\",\"lights\":[]}";
    TEST_ASSERT_TRUE(Config_Parse(input, strlen(input), &config));
    // When (Act)
    bool valid = Config_ValidateSubscriptions(&config, filter);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
}

void test_given_birth_topic_matching_state_when_configured_then_rejects_self_subscription(void)
{
    // Given (Arrange)
    const char *input = "{\"gateway_id\":\"gateway\",\"birth_topic\":\"dmx/lights/one/state\","
        "\"lights\":[{\"id\":\"one\",\"name\":\"One\",\"channel\":10}]}";
    // When (Act)
    bool valid = Config_Parse(input, strlen(input), &config);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
}

void test_given_gateway_rename_with_shared_state_topic_when_manifest_parsed_then_allows_cleanup_and_republish(void)
{
    // Given (Arrange)
    const char *input = "{\"gateway_id\":\"new\",\"lights\":[{\"id\":\"one\",\"name\":\"One\",\"channel\":10}],"
        "\"removals\":[{\"discovery\":\"homeassistant/light/mqtt2dmx_old_one/config\","
        "\"state\":\"dmx/lights/one/state\"}]}";
    // When (Act)
    bool valid = Config_Parse(input, strlen(input), &config);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_UINT(1, config.removal_count);
}

void test_given_maximum_light_count_when_parsed_then_accepts_255_unique_channels_and_rejects_256(void)
{
    // Given (Arrange)
    json_t *root = json_pack("{s:s,s:[]}", "gateway_id", "gateway", "lights");
    json_t *lights = json_object_get(root, "lights");
    for (int i = 1; i <= 255; i++)
    {
        char id[16];
        snprintf(id, sizeof(id), "light-%d", i);
        json_array_append_new(lights, json_pack("{s:s,s:s,s:i}", "id", id, "name", id, "channel", i));
    }
    char *valid_input = json_dumps(root, JSON_COMPACT);
    json_array_append_new(lights, json_pack("{s:s,s:s,s:i}", "id", "extra", "name", "Extra", "channel", 1));
    char *extra_input = json_dumps(root, JSON_COMPACT);
    // When (Act)
    bool boundary = Config_Parse(valid_input, strlen(valid_input), &config);
    bool excess = Config_Parse(extra_input, strlen(extra_input), &config);
    // Then (Assert)
    free(valid_input);
    free(extra_input);
    json_decref(root);
    TEST_ASSERT_TRUE(boundary);
    TEST_ASSERT_FALSE(excess);
    TEST_ASSERT_EQUAL_UINT(255, config.count);
    TEST_ASSERT_EQUAL_UINT(255, config.lights[254].channel);
}

void test_given_readonly_json_file_when_loaded_then_uses_validated_configuration(void)
{
    // Given (Arrange)
    const char *path = "/tmp/mqtt2dmx-unit-config.json";
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    fputs("{\"gateway_id\":\"gateway\",\"lights\":[]}", file);
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    // When (Act)
    bool valid = Config_Load(path, &config);
    int removed = remove(path);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_INT(0, removed);
    TEST_ASSERT_EQUAL_STRING("gateway", config.gateway_id);
}

void test_given_missing_config_file_when_loaded_then_reports_failure(void)
{
    // Given (Arrange)
    const char *path = "/tmp/mqtt2dmx-unit-file-does-not-exist.json";
    // When (Act)
    bool valid = Config_Load(path, &config);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
}

void test_given_excess_configuration_size_when_parsed_then_rejects_before_parsing(void)
{
    // Given (Arrange)
    char *data = malloc(CONFIG_LIMIT + 1);
    TEST_ASSERT_NOT_NULL(data);
    memset(data, ' ', CONFIG_LIMIT + 1);
    memcpy(data, "{\"gateway_id\":\"gateway\",\"lights\":[]}", 36);
    // When (Act)
    bool valid = Config_Parse(data, CONFIG_LIMIT + 1, &config);
    free(data);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
}

void test_given_removal_manifest_at_capacity_when_parsed_then_rejects_an_extra_entry(void)
{
    // Given (Arrange)
    json_t *root = json_pack("{s:s,s:[],s:[]}", "gateway_id", "gateway", "lights", "removals");
    json_t *removals = json_object_get(root, "removals");
    json_t *entry = json_pack("{s:s,s:s}", "discovery", "old/light/mqtt2dmx_old_lamp/config",
        "state", "old/lights/lamp/state");
    for (int i = 0; i < REMOVAL_LIMIT; i++)
        json_array_append(removals, entry);
    char *boundary_input = json_dumps(root, JSON_COMPACT);
    json_array_append(removals, entry);
    char *excess_input = json_dumps(root, JSON_COMPACT);
    // When (Act)
    bool boundary = Config_Parse(boundary_input, strlen(boundary_input), &config);
    bool excess = Config_Parse(excess_input, strlen(excess_input), &config);
    // Then (Assert)
    free(boundary_input);
    free(excess_input);
    json_decref(entry);
    json_decref(root);
    TEST_ASSERT_TRUE(boundary);
    TEST_ASSERT_FALSE(excess);
    TEST_ASSERT_EQUAL_UINT(REMOVAL_LIMIT, config.removal_count);
}

void test_given_light_id_at_length_boundary_when_parsed_then_preserves_it_and_rejects_excess(void)
{
    // Given (Arrange)
    char id[ID_LIMIT + 2], input[256];
    memset(id, 'a', ID_LIMIT + 1);
    id[ID_LIMIT] = 0;
    snprintf(input, sizeof(input), "{\"gateway_id\":\"gateway\",\"lights\":["
        "{\"id\":\"%s\",\"name\":\"Lamp\",\"channel\":1}]}", id);
    // When (Act)
    bool boundary = Config_Parse(input, strlen(input), &config);
    id[ID_LIMIT] = 'a';
    id[ID_LIMIT + 1] = 0;
    snprintf(input, sizeof(input), "{\"gateway_id\":\"gateway\",\"lights\":["
        "{\"id\":\"%s\",\"name\":\"Lamp\",\"channel\":1}]}", id);
    bool excess = Config_Parse(input, strlen(input), &config);
    // Then (Assert)
    TEST_ASSERT_TRUE(boundary);
    TEST_ASSERT_FALSE(excess);
    TEST_ASSERT_EQUAL_UINT(ID_LIMIT, strlen(config.lights[0].id));
}

void test_given_documented_example_when_loaded_then_maps_template_light_and_valid_subscriptions(void)
{
    // Given (Arrange)
    const char *path = "lights.example.json";
    // When (Act)
    bool valid = Config_Load(path, &config);
    // Then (Assert)
    TEST_ASSERT_TRUE(valid);
    TEST_ASSERT_EQUAL_UINT(1, config.count);
    TEST_ASSERT_EQUAL_STRING("zithoek-wandlampen", config.lights[0].id);
    TEST_ASSERT_EQUAL_UINT(10, config.lights[0].channel);
    TEST_ASSERT_EQUAL_UINT(66, config.lights[0].minimum);
    TEST_ASSERT_TRUE(Config_ValidateSubscriptions(&config, "dmx/channels/+/set"));
}
