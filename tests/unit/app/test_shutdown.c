#include "unity.h"
#include "shutdown.h"
#include "mock_mqtt.h"
#include "mock_dmx.h"
#include "mock_clock.h"
#include <stdlib.h>

static uint64_t times[16];
static size_t time_count;

void setUp(void) { time_count = 0; }
void tearDown(void) {}

static void expect_time(uint64_t now)
{
    Clock_Now_ExpectAndReturn(NULL, EXIT_SUCCESS);
    Clock_Now_IgnoreArg_milliseconds();
    times[time_count] = now;
    Clock_Now_ReturnThruPtr_milliseconds(&times[time_count++]);
}

static void expect_close(int dmx_status, bool mqtt_failure)
{
    MQTT_Close_Expect();
    MQTT_HasFailed_IgnoreAndReturn(mqtt_failure);
    DMX_Close_ExpectAndReturn(dmx_status);
}

void test_given_delayed_disconnect_when_stopping_then_waits_for_completion_and_closes_both_adapters(void)
{
    // Given (Arrange)
    MQTT_IsClosed_ExpectAndReturn(false);
    expect_time(100);
    MQTT_Stop_ExpectAndReturn(EXIT_SUCCESS);
    MQTT_IsClosed_ExpectAndReturn(false);
    expect_time(101);
    MQTT_Poll_ExpectAndReturn(EXIT_SUCCESS);
    Clock_Wait_ExpectAndReturn(1, EXIT_SUCCESS);
    MQTT_IsClosed_ExpectAndReturn(false);
    expect_time(102);
    MQTT_Poll_ExpectAndReturn(EXIT_SUCCESS);
    Clock_Wait_ExpectAndReturn(1, EXIT_SUCCESS);
    MQTT_IsClosed_ExpectAndReturn(true);
    expect_close(EXIT_SUCCESS, false);
    // When (Act)
    int status = Shutdown_Run(EXIT_SUCCESS);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
}

void test_given_disconnect_never_completes_when_exact_deadline_reached_then_fails_and_closes(void)
{
    // Given (Arrange)
    MQTT_IsClosed_ExpectAndReturn(false);
    expect_time(100);
    MQTT_Stop_ExpectAndReturn(EXIT_SUCCESS);
    MQTT_IsClosed_ExpectAndReturn(false);
    expect_time(5099);
    MQTT_Poll_ExpectAndReturn(EXIT_SUCCESS);
    Clock_Wait_ExpectAndReturn(1, EXIT_SUCCESS);
    MQTT_IsClosed_ExpectAndReturn(false);
    expect_time(5100);
    expect_close(EXIT_SUCCESS, false);
    // When (Act)
    int status = Shutdown_Run(EXIT_SUCCESS);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
}

void test_given_fatal_error_when_stopping_then_closes_without_waiting_or_requesting_disconnect(void)
{
    // Given (Arrange)
    expect_close(EXIT_SUCCESS, false);
    // When (Act)
    int status = Shutdown_Run(EXIT_FAILURE);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
}

void test_given_failure_callback_during_destroy_when_stopping_then_does_not_report_success(void)
{
    // Given (Arrange)
    MQTT_IsClosed_ExpectAndReturn(true);
    expect_close(EXIT_SUCCESS, true);
    // When (Act)
    int status = Shutdown_Run(EXIT_SUCCESS);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
}

void test_given_device_close_failure_when_stopping_then_reports_unsuccessful_exit(void)
{
    // Given (Arrange)
    MQTT_IsClosed_ExpectAndReturn(true);
    expect_close(EXIT_FAILURE, false);
    // When (Act)
    int status = Shutdown_Run(EXIT_SUCCESS);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
}

TEST_CASE(0)
TEST_CASE(1)
TEST_CASE(2)
TEST_CASE(3)
void test_given_shutdown_operation_failure_when_draining_then_still_closes_both_adapters(int stage)
{
    // Given (Arrange)
    MQTT_IsClosed_ExpectAndReturn(false);
    if (stage == 0)
    {
        Clock_Now_ExpectAndReturn(NULL, EXIT_FAILURE);
        Clock_Now_IgnoreArg_milliseconds();
    }
    else
    {
        expect_time(0);
        MQTT_Stop_ExpectAndReturn(stage == 1 ? EXIT_FAILURE : EXIT_SUCCESS);
        if (stage > 1)
        {
            MQTT_IsClosed_ExpectAndReturn(false);
            expect_time(1);
            MQTT_Poll_ExpectAndReturn(stage == 2 ? EXIT_FAILURE : EXIT_SUCCESS);
            if (stage == 3)
                Clock_Wait_ExpectAndReturn(1, EXIT_FAILURE);
        }
    }
    expect_close(EXIT_SUCCESS, false);
    // When (Act)
    int status = Shutdown_Run(EXIT_SUCCESS);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
}
