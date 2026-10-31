#include "unity.h"
#include "transition.h"

void setUp(void) {}
void tearDown(void) {}

TEST_CASE(0, 0)
TEST_CASE(1, 66)
TEST_CASE(128, 161)
TEST_CASE(255, 255)
void test_given_calibrated_light_when_converting_integer_brightness_then_matches_template(
    int brightness, int expected)
{
    // Given (Arrange)
    DMXValue minimum = 66;
    // When (Act)
    DMXValue raw = Transition_Calibrate(brightness, minimum);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(expected, raw);
}

void test_given_all_integer_levels_when_calibrated_then_matches_exact_rational_rounding(void)
{
    // Given (Arrange)
    unsigned int minimum = 66;
    // When (Act)
    DMXValue outputs[256];
    for (int b = 0; b < 256; b++)
        outputs[b] = Transition_Calibrate(b, minimum);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(0, outputs[0]);
    for (int b = 1; b < 256; b++)
        TEST_ASSERT_EQUAL_UINT8(minimum + ((b - 1) * (255 - minimum) + 127) / 254, outputs[b]);
    TEST_ASSERT_EQUAL_UINT8(66, Transition_Calibrate(0.001, minimum));
    TEST_ASSERT_EQUAL_FLOAT(1, Transition_Inverse(10, minimum));
    TEST_ASSERT_EQUAL_FLOAT(255, Transition_Inverse(10, 255));
}

TEST_CASE(0, 250, 63.75)
TEST_CASE(1, 250, 39.84375)
TEST_CASE(0, 500, 127.5)
TEST_CASE(1, 500, 127.5)
TEST_CASE(0, 1000, 255)
TEST_CASE(1, 2000, 255)
void test_given_fade_when_evaluated_at_elapsed_time_then_uses_selected_curve(
    int curve, int elapsed, float expected)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.channel = 10, .minimum = 66, .duration_ms = 1000, .curve = curve};
    LightCommand command = {.has_state = true, .on = true};
    // When (Act)
    Transition_Apply(&fade, &config, &command, 50);
    double value = Transition_Value(&fade, config.curve, 50 + elapsed);
    // Then (Assert)
    TEST_ASSERT_EQUAL_FLOAT(expected, value);
    TEST_ASSERT_EQUAL_UINT8(255, Transition_Target(&fade));
}

void test_given_midfade_when_retargeted_then_preserves_unrounded_current_value(void)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.channel = 10, .minimum = 66, .duration_ms = 1000, .curve = CURVE_LINEAR};
    LightCommand on = {.has_state = true, .on = true};
    LightCommand off = {.has_state = true, .on = false};
    Transition_Apply(&fade, &config, &on, 100);
    // When (Act)
    Transition_Apply(&fade, &config, &off, 350);
    double midpoint = Transition_Value(&fade, config.curve, 850);
    // Then (Assert)
    TEST_ASSERT_EQUAL_FLOAT(63.75, fade.start);
    TEST_ASSERT_EQUAL_FLOAT(31.875, midpoint);
    TEST_ASSERT_EQUAL_UINT8(255, fade.remembered);
}

void test_given_explicit_zero_when_applied_then_waits_for_successful_endpoint_submission(void)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.duration_ms = 500, .minimum = 66};
    LightCommand command = {.has_brightness = true, .brightness = 128, .has_duration = true};
    // When (Act)
    Transition_Apply(&fade, &config, &command, 10);
    DMXValue output = Transition_Evaluate(&fade, &config, 10);
    bool pending = fade.endpoint_pending;
    Transition_Submitted(&fade);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(161, output);
    TEST_ASSERT_TRUE(pending);
    TEST_ASSERT_FALSE(fade.endpoint_pending);
    TEST_ASSERT_FALSE(fade.active);
}

void test_given_raw_override_when_fade_restarts_then_holds_exact_raw_until_new_command(void)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.minimum = 66, .duration_ms = 1000, .curve = CURVE_LINEAR};
    LightCommand on = {.has_state = true, .on = true};
    Transition_Apply(&fade, &config, &on, 0);
    // When (Act)
    Transition_Raw(&fade, &config, 10);
    DMXValue raw = Transition_Evaluate(&fade, &config, 2000);
    Transition_Apply(&fade, &config, &on, 3000);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(10, raw);
    TEST_ASSERT_EQUAL_FLOAT(1, fade.start);
    TEST_ASSERT_EQUAL_UINT8(1, fade.remembered);
    TEST_ASSERT_FALSE(fade.raw_override);
}

void test_given_maximum_duration_near_clock_limit_when_late_then_reaches_exact_endpoint(void)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.minimum = 66, .curve = CURVE_SMOOTHSTEP};
    LightCommand on = {.has_state = true, .on = true, .has_duration = true,
        .duration_ms = TRANSITION_LIMIT_MS};
    uint64_t start = UINT64_MAX - 4000000;
    // When (Act)
    Transition_Apply(&fade, &config, &on, start);
    DMXValue end = Transition_Evaluate(&fade, &config, start + 3600001);
    // Then (Assert)
    TEST_ASSERT_EQUAL_UINT8(255, end);
    TEST_ASSERT_TRUE(fade.endpoint_pending);
    TEST_ASSERT_TRUE(fade.active);
}

void test_given_off_with_brightness_when_on_without_brightness_then_recalls_accepted_target(void)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.duration_ms = 500};
    LightCommand off = {.has_state = true, .has_brightness = true, .brightness = 128};
    LightCommand on = {.has_state = true, .on = true};
    // When (Act)
    Transition_Apply(&fade, &config, &off, 0);
    double off_target = fade.target;
    Transition_Apply(&fade, &config, &on, 50);
    // Then (Assert)
    TEST_ASSERT_EQUAL_FLOAT(0, off_target);
    TEST_ASSERT_EQUAL_UINT8(128, Transition_Target(&fade));
}

TEST_CASE(0, 191.25)
TEST_CASE(1, 215.15625)
void test_given_full_brightness_when_fading_off_then_descends_without_unsigned_wrap(int curve, float expected)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.minimum = 66, .duration_ms = 1000, .curve = curve};
    LightCommand on = {.has_state = true, .on = true, .has_duration = true};
    LightCommand off = {.has_state = true};
    Transition_Apply(&fade, &config, &on, 0);
    // When (Act)
    Transition_Apply(&fade, &config, &off, 100);
    double quarter = Transition_Value(&fade, curve, 350);
    DMXValue endpoint = Transition_Evaluate(&fade, &config, 1101);
    // Then (Assert)
    TEST_ASSERT_EQUAL_FLOAT(expected, quarter);
    TEST_ASSERT_EQUAL_UINT8(0, endpoint);
}

void test_given_equal_target_when_applied_then_finishes_without_waiting_for_default_duration(void)
{
    // Given (Arrange)
    Transition fade;
    Transition_Initialize(&fade);
    LightConfig config = {.duration_ms = 500};
    LightCommand off = {.has_state = true};
    // When (Act)
    Transition_Apply(&fade, &config, &off, 100);
    // Then (Assert)
    TEST_ASSERT_FALSE(fade.active);
    TEST_ASSERT_TRUE(fade.endpoint_pending);
    TEST_ASSERT_EQUAL_UINT8(0, Transition_Target(&fade));
}
