#include "unity.h"
#include "mqtt.h"
#include "paho_fake.h"
#include "test_helpers.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

TEST_SOURCE_FILE("mqtt.c")
TEST_SOURCE_FILE("paho_fake.c")

static pthread_barrier_t barrier;
static pthread_t owner;
static atomic_uint finished, delivery_failures;
static unsigned int seen[64], received_count, last_seen[2];
static bool producer_seen[2];

void setUp(void)
{
    PahoFake_Reset();
    atomic_store(&finished, 0);
    atomic_store(&delivery_failures, 0);
    memset(seen, 0, sizeof(seen));
    memset(producer_seen, 0, sizeof(producer_seen));
    received_count = 0;
    owner = pthread_self();
}

void tearDown(void)
{
    MQTT_Close();
}

static void consume(MQTTMessage *message)
{
    TEST_ASSERT_TRUE(pthread_equal(owner, pthread_self()));
    unsigned int number = (unsigned int)strtoul(message->payload, NULL, 10);
    TEST_ASSERT_LESS_THAN(64, number);
    unsigned int producer = number / 32;
    if (producer_seen[producer])
        TEST_ASSERT_EQUAL_UINT(last_seen[producer] + 1, number);
    producer_seen[producer] = true;
    last_seen[producer] = number;
    seen[number]++;
    received_count++;
    MQTT_Free(message);
}

static void *produce(void *argument)
{
    unsigned int base = *(unsigned int *)argument;
    pthread_barrier_wait(&barrier);
    for (unsigned int i = base; i < base + 32; i++)
        if (PahoFake_Deliver(i) != 1)
            atomic_fetch_add(&delivery_failures, 1);
    atomic_fetch_add(&finished, 1);
    return NULL;
}

void test_given_two_concurrent_producers_when_owner_polls_then_each_command_is_delivered_once_in_producer_order(void)
{
    // Given (Arrange)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Start("test", "test", "/+/set", consume));
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&barrier, NULL, 3));
    pthread_t producers[2];
    unsigned int bases[2] = {0, 32};
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producers[0], NULL, produce, &bases[0]));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producers[1], NULL, produce, &bases[1]));
    // When (Act)
    pthread_barrier_wait(&barrier);
    for (unsigned int iteration = 0;
         iteration < 1000000 && (atomic_load(&finished) != 2 || received_count != 64);
         iteration++)
    {
        unsigned int before = received_count;
        TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Poll());
        TEST_ASSERT_LESS_OR_EQUAL_UINT(MQTT_DRAIN_BUDGET, received_count - before);
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_join(producers[0], NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(producers[1], NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_destroy(&barrier));
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT(0, atomic_load(&delivery_failures));
    TEST_ASSERT_EQUAL_UINT(64, received_count);
    for (size_t i = 0; i < 64; i++)
        TEST_ASSERT_EQUAL_UINT(1, seen[i]);
    TEST_ASSERT_EQUAL_UINT(64, PahoFake_ReleasedMessages());
    TEST_ASSERT_EQUAL_UINT(64, PahoFake_ReleasedTopics());
    TEST_ASSERT_FALSE(MQTT_HasFailed());
}

void test_given_synchronous_send_completion_when_published_then_releases_owned_message_before_returning(void)
{
    // Given (Arrange)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Start("test", "test", "/+/set", consume));
    MQTTMessage *message = MQTT_CreateMessage("/1", 2, "9", 1);
    void *pointers[] = {message->topic, message->payload, message};
    watch_frees(pointers, 3);
    // When (Act)
    int status = MQTT_PublishAndFree(message);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    assert_watched_frees();
}

void test_given_concurrent_arrivals_when_owner_stops_then_queued_and_later_commands_are_not_applied(void)
{
    // Given (Arrange)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, MQTT_Start("test", "test", "/+/set", consume));
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&barrier, NULL, 3));
    pthread_t producers[2];
    unsigned int bases[2] = {0, 32};
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producers[0], NULL, produce, &bases[0]));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producers[1], NULL, produce, &bases[1]));
    // When (Act)
    pthread_barrier_wait(&barrier);
    int status = MQTT_Stop();
    TEST_ASSERT_EQUAL_INT(0, pthread_join(producers[0], NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(producers[1], NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_destroy(&barrier));
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    TEST_ASSERT_EQUAL_UINT(0, received_count);
    TEST_ASSERT_EQUAL_UINT(64, PahoFake_ReleasedMessages());
    TEST_ASSERT_EQUAL_UINT(64, PahoFake_ReleasedTopics());
    TEST_ASSERT_TRUE(MQTT_IsClosed());
}
