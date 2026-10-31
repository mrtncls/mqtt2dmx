#include "unity.h"
#include "ha_topics.h"
#include <string.h>

TEST_SOURCE_FILE("config.c")
TEST_SOURCE_FILE("json_contract.c")

static GatewayConfig config;

void setUp(void)
{
    memset(&config, 0, sizeof(config));
    strcpy(config.gateway_id, "gateway");
    strcpy(config.prefix, "house/dmx");
    strcpy(config.discovery_prefix, "custom/ha");
    strcpy(config.lights[0].id, "kitchen");
    config.count = 1;
}

void tearDown(void) {}

TEST_CASE(HA_COMMAND_TOPIC, "house/dmx/lights/kitchen/set")
TEST_CASE(HA_STATE_TOPIC, "house/dmx/lights/kitchen/state")
TEST_CASE(HA_DISCOVERY_TOPIC, "custom/ha/light/mqtt2dmx_gateway_kitchen/config")
void test_given_custom_configuration_when_light_topic_built_then_preserves_exact_bytes(
    HATopic type, const char *expected)
{
    // Given (Arrange)
    char topic[HA_TOPIC_LIMIT];
    // When (Act)
    HA_Topic(&config, 0, type, topic);
    // Then (Assert)
    TEST_ASSERT_EQUAL_STRING(expected, topic);
}

void test_given_custom_prefix_when_command_filter_built_then_matches_light_command_namespace(void)
{
    // Given (Arrange)
    char filter[HA_TOPIC_LIMIT];
    // When (Act)
    HA_CommandFilter(&config, filter);
    // Then (Assert)
    TEST_ASSERT_EQUAL_STRING("house/dmx/lights/+/set", filter);
}

void test_given_custom_prefix_when_availability_built_then_preserves_exact_topic(void)
{
    // Given (Arrange)
    char topic[HA_TOPIC_LIMIT];
    // When (Act)
    HA_AvailabilityTopic(&config, topic);
    // Then (Assert)
    TEST_ASSERT_EQUAL_STRING("house/dmx/availability", topic);
}

void test_given_maximum_prefix_when_gateway_topics_built_then_preserves_full_prefix_and_suffix(void)
{
    // Given (Arrange)
    memset(config.prefix, 'p', PREFIX_LIMIT);
    config.prefix[PREFIX_LIMIT] = 0;
    char filter[HA_TOPIC_LIMIT], availability[HA_TOPIC_LIMIT];
    // When (Act)
    HA_CommandFilter(&config, filter);
    HA_AvailabilityTopic(&config, availability);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT(PREFIX_LIMIT + strlen("/lights/+/set"), strlen(filter));
    TEST_ASSERT_EQUAL_MEMORY(config.prefix, filter, PREFIX_LIMIT);
    TEST_ASSERT_EQUAL_STRING("/lights/+/set", filter + PREFIX_LIMIT);
    TEST_ASSERT_EQUAL_UINT(PREFIX_LIMIT + strlen("/availability"), strlen(availability));
    TEST_ASSERT_EQUAL_MEMORY(config.prefix, availability, PREFIX_LIMIT);
    TEST_ASSERT_EQUAL_STRING("/availability", availability + PREFIX_LIMIT);
}
