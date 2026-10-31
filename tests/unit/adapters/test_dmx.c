#include "unity.h"
#include "dmx.h"
#include "mock_ftdi_api.h"
#include "mock_delay.h"
#include "test_helpers.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct ftdi_context *context;
static DMXValue frame[DMX_FRAME_SIZE], expected_frame[DMX_FRAME_SIZE];
static int frames_written;

void setUp(void)
{
    context = NULL;
    memset(frame, 0, sizeof(frame));
    memset(expected_frame, 0, sizeof(expected_frame));
    frames_written = 0;
}

void tearDown(void)
{
    mock_ftdi_api_Verify();
    mock_delay_Verify();
    ftdi_usb_close_IgnoreAndReturn(0);
    ftdi_deinit_Ignore();
    DMX_Close();
}

static int capture_context(struct ftdi_context *actual, int calls)
{
    TEST_ASSERT_NOT_NULL(actual);
    context = actual;
    return 0;
}

static void expect_initialize(void)
{
    ftdi_init_ExpectAndReturn(NULL, 0);
    ftdi_init_IgnoreArg_ftdi();
    ftdi_init_AddCallback(capture_context);
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
}

static void initialize(void)
{
    expect_initialize();
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_Initialize());
}

static int check_frame(struct ftdi_context *actual, const unsigned char *data, int size, int calls)
{
    TEST_ASSERT_EQUAL_PTR(context, actual);
    TEST_ASSERT_EQUAL_INT(DMX_FRAME_SIZE, size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_frame, data, DMX_FRAME_SIZE);
    frames_written++;
    return size;
}

static void expect_frame(void)
{
    ftdi_set_line_property2_ExpectAndReturn(context, BITS_8, STOP_BIT_2, NONE, BREAK_ON, 0);
    usleep_ExpectAndReturn(120, 0);
    ftdi_set_line_property2_ExpectAndReturn(context, BITS_8, STOP_BIT_2, NONE, BREAK_OFF, 0);
    usleep_ExpectAndReturn(16, 0);
    ftdi_write_data_ExpectAndReturn(context, frame, DMX_FRAME_SIZE, DMX_FRAME_SIZE);
    ftdi_write_data_AddCallback(check_frame);
    usleep_ExpectAndReturn(12000, 0);
}

void test_given_available_device_when_initialized_then_ftdi_is_configured_for_dmx_in_order(void)
{
    // Given (Arrange)
    expect_initialize();
    // When (Act)
    int status = DMX_Initialize();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    mock_ftdi_api_Verify();
}

void test_given_initial_state_when_frame_written_then_zero_frame_is_sent_with_break_and_delays(void)
{
    // Given (Arrange)
    initialize();
    expect_frame();
    // When (Act)
    int status = DMX_WriteFrame(frame);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    TEST_ASSERT_EQUAL_INT(1, frames_written);
}

void test_given_channel_values_when_frame_written_then_all_slots_and_start_code_are_preserved(void)
{
    // Given (Arrange)
    initialize();
    frame[1] = expected_frame[1] = 255;
    frame[16] = expected_frame[16] = 199;
    frame[255] = expected_frame[255] = 128;
    expect_frame();
    // When (Act)
    int status = DMX_WriteFrame(frame);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    TEST_ASSERT_EQUAL_INT(1, frames_written);
}

void test_given_channel_value_when_frame_repeats_then_full_frame_persists_without_change(void)
{
    // Given (Arrange)
    initialize();
    frame[255] = expected_frame[255] = 255;
    expect_frame();
    expect_frame();
    // When (Act)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(2, frames_written);
}

void test_given_previously_sent_channel_value_when_cleared_then_zero_is_written(void)
{
    // Given (Arrange)
    initialize();
    frame[255] = expected_frame[255] = 255;
    expect_frame();
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
    expect_frame();
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
    frame[255] = expected_frame[255] = 0;
    expect_frame();
    // When (Act)
    int status = DMX_WriteFrame(frame);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    TEST_ASSERT_EQUAL_INT(3, frames_written);
}

static void verify_dmx_mocks(void)
{
    mock_ftdi_api_Verify();
    mock_delay_Verify();
}

static void initialize_and_exit(void)
{
    int status = DMX_Initialize();
    DMX_Close();
    exit(status);
}

TEST_CASE(0, "ftdi_init")
TEST_CASE(1, "ftdi_usb_open_desc")
TEST_CASE(2, "ftdi_usb_reset")
TEST_CASE(3, "ftdi_set_interface")
TEST_CASE(4, "ftdi_set_baudrate")
TEST_CASE(5, "ftdi_set_line_property")
TEST_CASE(6, "ftdi_setflowctrl")
void test_given_ftdi_setup_failure_when_initialized_then_reports_error_and_caller_releases_acquired_resources(
    int step, const char *diagnostic)
{
    // Given (Arrange)
    ftdi_init_ExpectAndReturn(NULL, step == 0 ? -1 : 0);
    ftdi_init_IgnoreArg_ftdi();
    if (step > 0)
    {
        ftdi_usb_open_desc_ExpectAndReturn(NULL, 0x0403, 0x6001, NULL, NULL, step == 1 ? -1 : 0);
        ftdi_usb_open_desc_IgnoreArg_ftdi();
    }
    if (step > 1)
    {
        ftdi_usb_reset_ExpectAndReturn(NULL, step == 2 ? -1 : 0);
        ftdi_usb_reset_IgnoreArg_ftdi();
    }
    if (step > 2)
    {
        ftdi_set_interface_ExpectAndReturn(NULL, INTERFACE_ANY, step == 3 ? -1 : 0);
        ftdi_set_interface_IgnoreArg_ftdi();
    }
    if (step > 3)
    {
        ftdi_set_baudrate_ExpectAndReturn(NULL, 250000, step == 4 ? -1 : 0);
        ftdi_set_baudrate_IgnoreArg_ftdi();
    }
    if (step > 4)
    {
        ftdi_set_line_property_ExpectAndReturn(NULL, BITS_8, STOP_BIT_2, NONE, step == 5 ? -1 : 0);
        ftdi_set_line_property_IgnoreArg_ftdi();
    }
    if (step > 5)
    {
        ftdi_setflowctrl_ExpectAndReturn(NULL, SIO_DISABLE_FLOW_CTRL, -1);
        ftdi_setflowctrl_IgnoreArg_ftdi();
    }
    if (step > 1)
    {
        ftdi_usb_close_ExpectAndReturn(NULL, 0);
        ftdi_usb_close_IgnoreArg_ftdi();
    }
    if (step > 0)
    {
        ftdi_deinit_Expect(NULL);
        ftdi_deinit_IgnoreArg_ftdi();
    }
    verify_child_before_exit(verify_dmx_mocks);
    // When (Act)
    struct child_result result = run_child(initialize_and_exit);
    // Then (Assert)
    mock_ftdi_api_Destroy();
    mock_delay_Destroy();
    assert_child_failure(&result, EXIT_FAILURE, 0, diagnostic);
}

static void write_and_exit(void)
{
    int status = DMX_WriteFrame(frame);
    DMX_Close();
    exit(status);
}

TEST_CASE(0, -1, "ftdi_set_line_property2")
TEST_CASE(1, -1, "usleep")
TEST_CASE(2, -1, "ftdi_set_line_property2")
TEST_CASE(3, -1, "usleep")
TEST_CASE(4, 512, "ftdi_write_data")
TEST_CASE(4, 0, "ftdi_write_data")
TEST_CASE(4, -1, "ftdi_write_data")
TEST_CASE(5, -1, "usleep")
void test_given_frame_io_failure_when_written_then_reports_error_and_caller_releases_device(
    int step, int failure, const char *diagnostic)
{
    // Given (Arrange)
    initialize();
    errno = EIO;
    ftdi_set_line_property2_ExpectAndReturn(context, BITS_8, STOP_BIT_2, NONE, BREAK_ON,
                                          step == 0 ? failure : 0);
    if (step > 0)
        usleep_ExpectAndReturn(120, step == 1 ? failure : 0);
    if (step > 1)
        ftdi_set_line_property2_ExpectAndReturn(context, BITS_8, STOP_BIT_2, NONE, BREAK_OFF,
                                              step == 2 ? failure : 0);
    if (step > 2)
        usleep_ExpectAndReturn(16, step == 3 ? failure : 0);
    if (step > 3)
        ftdi_write_data_ExpectAndReturn(context, frame, DMX_FRAME_SIZE, step == 4 ? failure : DMX_FRAME_SIZE);
    if (step > 4)
        usleep_ExpectAndReturn(12000, failure);
    ftdi_usb_close_ExpectAndReturn(context, 0);
    ftdi_deinit_Expect(context);
    verify_child_before_exit(verify_dmx_mocks);
    // When (Act)
    struct child_result result = run_child(write_and_exit);
    // Then (Assert)
    mock_ftdi_api_Destroy();
    mock_delay_Destroy();
    assert_child_failure(&result, EXIT_FAILURE, 0, diagnostic);
}

TEST_CASE(0)
TEST_CASE(1)
TEST_CASE(2)
void test_given_interrupted_delay_when_frame_written_then_retries_delay_and_finishes_frame(int interrupted_delay)
{
    // Given (Arrange)
    initialize();
    errno = EINTR;
    ftdi_set_line_property2_ExpectAndReturn(context, BITS_8, STOP_BIT_2, NONE, BREAK_ON, 0);
    if (interrupted_delay == 0)
        usleep_ExpectAndReturn(120, -1);
    usleep_ExpectAndReturn(120, 0);
    ftdi_set_line_property2_ExpectAndReturn(context, BITS_8, STOP_BIT_2, NONE, BREAK_OFF, 0);
    if (interrupted_delay == 1)
        usleep_ExpectAndReturn(16, -1);
    usleep_ExpectAndReturn(16, 0);
    ftdi_write_data_ExpectAndReturn(context, frame, DMX_FRAME_SIZE, DMX_FRAME_SIZE);
    ftdi_write_data_AddCallback(check_frame);
    if (interrupted_delay == 2)
        usleep_ExpectAndReturn(12000, -1);
    usleep_ExpectAndReturn(12000, 0);
    // When (Act)
    int status = DMX_WriteFrame(frame);
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, status);
    TEST_ASSERT_EQUAL_INT(1, frames_written);
}

void test_given_open_device_when_closed_twice_then_releases_resources_once(void)
{
    // Given (Arrange)
    initialize();
    ftdi_usb_close_ExpectAndReturn(context, 0);
    ftdi_deinit_Expect(context);
    // When (Act)
    int first = DMX_Close();
    int second = DMX_Close();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, first);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, second);
}

void test_given_close_failure_when_closed_then_deinitializes_and_reports_failure(void)
{
    // Given (Arrange)
    initialize();
    ftdi_usb_close_ExpectAndReturn(context, -1);
    ftdi_deinit_Expect(context);
    // When (Act)
    int status = DMX_Close();
    // Then (Assert)
    TEST_ASSERT_EQUAL_INT(EXIT_FAILURE, status);
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_Close());
}

static void write_repeated_and_changed_frames(void)
{
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
    frame[255] = expected_frame[255] = 0;
    TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
}

void test_given_repeated_and_changed_frames_when_written_then_summarizes_without_per_channel_spam(void)
{
    // Given (Arrange)
    initialize();
    frame[255] = expected_frame[255] = 255;
    expect_frame();
    expect_frame();
    expect_frame();
    verify_child_before_exit(verify_dmx_mocks);
    // When (Act)
    struct child_result result = run_child(write_repeated_and_changed_frames);
    // Then (Assert)
    mock_ftdi_api_Destroy();
    mock_delay_Destroy();
    assert_child_success(&result);
    char *first = strstr(result.output, "DMX output updated: 1 changed channels");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NULL(strstr(first + 1, "DMX output updated"));
    TEST_ASSERT_NULL(strstr(result.output, "DMX channel"));
}

static void write_animation_frames(void)
{
    for (int i = 0; i < 257; i++)
    {
        frame[255] = expected_frame[255] = 1 + i % 2;
        TEST_ASSERT_EQUAL_INT(EXIT_SUCCESS, DMX_WriteFrame(frame));
    }
    TEST_ASSERT_EQUAL_INT(257, frames_written);
}

void test_given_continuously_changing_frames_when_written_then_logs_only_once_per_256_changes(void)
{
    // Given (Arrange)
    initialize();
    ftdi_set_line_property2_IgnoreAndReturn(0);
    usleep_IgnoreAndReturn(0);
    ftdi_write_data_Stub(check_frame);
    verify_child_before_exit(verify_dmx_mocks);
    // When (Act)
    struct child_result result = run_child(write_animation_frames);
    // Then (Assert)
    mock_ftdi_api_Destroy();
    mock_delay_Destroy();
    assert_child_success(&result);
    char *first = strstr(result.output, "DMX output updated");
    TEST_ASSERT_NOT_NULL(first);
    char *second = strstr(first + 1, "DMX output updated");
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_NULL(strstr(second + 1, "DMX output updated"));
}
