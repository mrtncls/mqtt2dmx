#include "dmx.h"
#include <errno.h>
#include <ftdi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct ftdi_context ftdi;
static DMXValue last_frame[DMX_FRAME_SIZE];
static bool initialized, opened;

int DMX_Close(void)
{
    int result = EXIT_SUCCESS;
    if (opened)
    {
        int rc = ftdi_usb_close(&ftdi);
        if (rc != 0)
        {
            fprintf(stderr, "ftdi_usb_close failed, return code %d\n", rc);
            result = EXIT_FAILURE;
        }
        opened = false;
    }
    if (initialized)
    {
        ftdi_deinit(&ftdi);
        initialized = false;
    }
    return result;
}

static int check(int rc, const char *operation)
{
    if (rc == 0)
        return EXIT_SUCCESS;
    fprintf(stderr, "%s failed, return code %d\n", operation, rc);
    return EXIT_FAILURE;
}

static int delay(unsigned int duration)
{
    while (usleep(duration) != 0)
        if (errno != EINTR)
            return check(-1, "usleep");
    return EXIT_SUCCESS;
}

int DMX_Initialize(void)
{
    if (initialized)
        return check(-1, "FTDI already initialized");
    memset(last_frame, 0, sizeof(last_frame));
    if (check(ftdi_init(&ftdi), "ftdi_init"))
        return EXIT_FAILURE;
    initialized = true;
    if (check(ftdi_usb_open_desc(&ftdi, 0x0403, 0x6001, NULL, NULL), "ftdi_usb_open_desc"))
        return EXIT_FAILURE;
    opened = true;
    if (check(ftdi_usb_reset(&ftdi), "ftdi_usb_reset") ||
        check(ftdi_set_interface(&ftdi, INTERFACE_ANY), "ftdi_set_interface") ||
        check(ftdi_set_baudrate(&ftdi, 250000), "ftdi_set_baudrate") ||
        check(ftdi_set_line_property(&ftdi, BITS_8, STOP_BIT_2, NONE), "ftdi_set_line_property") ||
        check(ftdi_setflowctrl(&ftdi, SIO_DISABLE_FLOW_CTRL), "ftdi_setflowctrl"))
        return EXIT_FAILURE;
    printf("FTDI device intialized for DMX\n");
    return EXIT_SUCCESS;
}

int DMX_WriteFrame(const DMXValue frame[DMX_FRAME_SIZE])
{
    if (!opened)
        return check(-1, "FTDI frame before initialization");
    if (check(ftdi_set_line_property2(&ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON),
              "ftdi_set_line_property2") || delay(120) ||
        check(ftdi_set_line_property2(&ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF),
              "ftdi_set_line_property2") || delay(16))
        return EXIT_FAILURE;
    int written = ftdi_write_data(&ftdi, frame, DMX_FRAME_SIZE);
    if (written != DMX_FRAME_SIZE)
    {
        fprintf(stderr, "ftdi_write_data failed: expected %d bytes, wrote %d\n",
                DMX_FRAME_SIZE, written);
        return EXIT_FAILURE;
    }
    if (delay(12000))
        return EXIT_FAILURE;
    for (int i = 0; i < DMX_FRAME_SIZE; i++)
        if (frame[i] != last_frame[i])
            printf("DMX channel %u: %u\n", i, frame[i]);
    memcpy(last_frame, frame, sizeof(last_frame));
    return EXIT_SUCCESS;
}
