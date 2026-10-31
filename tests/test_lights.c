#include "unity.h"
#include "lights.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_given_dirty_state_when_initialized_then_all_slots_and_start_code_are_zero(void)
{
    // Given (Arrange)
    LightState state;
    memset(&state, 255, sizeof(state));
    DMXValue frame[DMX_FRAME_SIZE], expected[DMX_FRAME_SIZE] = {0};
    // When (Act)
    Lights_Initialize(&state);
    Lights_Frame(&state, frame);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, DMX_FRAME_SIZE);
}

void test_given_full_universe_when_updated_then_snapshot_preserves_all_other_slots(void)
{
    // Given (Arrange)
    LightState state;
    Lights_Initialize(&state);
    DMXValue frame[DMX_FRAME_SIZE], expected[DMX_FRAME_SIZE] = {0};
    // When (Act)
    for (DMXChannel channel = 1; channel <= 512; channel++)
    {
        expected[channel] = (DMXValue)channel;
        TEST_ASSERT_TRUE(Lights_Set(&state, channel, (DMXValue)channel));
    }
    Lights_Frame(&state, frame);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, DMX_FRAME_SIZE);
}

TEST_CASE(0)
TEST_CASE(513)
TEST_CASE(65535)
void test_given_invalid_slot_when_set_then_state_remains_unchanged(int channel)
{
    // Given (Arrange)
    LightState state, unchanged;
    Lights_Initialize(&state);
    unchanged = state;
    // When (Act)
    bool valid = Lights_Set(&state, (DMXChannel)channel, 255);
    // Then (Assert)
    TEST_ASSERT_FALSE(valid);
    TEST_ASSERT_EQUAL_MEMORY(&unchanged, &state, sizeof(state));
}

void test_given_existing_snapshot_when_state_changes_then_snapshot_remains_stable(void)
{
    // Given (Arrange)
    LightState state;
    Lights_Initialize(&state);
    TEST_ASSERT_TRUE(Lights_Set(&state, 16, 199));
    DMXValue first[DMX_FRAME_SIZE], second[DMX_FRAME_SIZE];
    Lights_Frame(&state, first);
    // When (Act)
    TEST_ASSERT_TRUE(Lights_Set(&state, 16, 0));
    Lights_Frame(&state, second);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(199, first[16]);
    TEST_ASSERT_EQUAL_UINT8(0, second[16]);
    TEST_ASSERT_EQUAL_UINT8(0, first[0]);
    TEST_ASSERT_EQUAL_UINT8(0, second[0]);
}
