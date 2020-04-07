#include "dmx.h"

#define FRAME_DELAY 12000
#define BREAK_DELAY 120
#define MAB_DELAY 16
#define PACKET_SIZE 513

struct ftdi_context ftdi;
unsigned char packet[PACKET_SIZE] = {0};
unsigned char last_packet[PACKET_SIZE] = {0};
bool packet_changed;

void DMX_Initialize()
{
    ftdi_init(&ftdi);
    assert(ftdi_usb_open_desc(&ftdi, 0x0403, 0x6001, NULL, NULL) == 0);
    assert(ftdi_usb_reset(&ftdi) == 0);
    assert(ftdi_set_interface(&ftdi, INTERFACE_ANY) == 0);
    assert(ftdi_set_baudrate(&ftdi, 250000) == 0);
    assert(ftdi_set_line_property(&ftdi, BITS_8, STOP_BIT_2, NONE) == 0);
    assert(ftdi_setflowctrl(&ftdi, SIO_DISABLE_FLOW_CTRL) == 0);

    printf("FTDI device intialized for DMX\n");
}

void DMX_Loop()
{
    assert(ftdi_set_line_property2(&ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON) == 0);
    usleep(BREAK_DELAY);

    assert(ftdi_set_line_property2(&ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF) == 0);
    usleep(MAB_DELAY);

    assert(ftdi_write_data(&ftdi, packet, sizeof(packet)) == sizeof(packet));
    usleep(FRAME_DELAY);

    packet_changed = false;
    for (int i = 0; i < PACKET_SIZE; i++)
    {
        if (packet[i] != last_packet[i])
        {
            packet_changed = true;
            printf("DMX channel %u: %u\n", i, packet[i]);
        }
    }
    if (packet_changed)
    {
        // printf("DMX frame: ");
        // for (int i = 0; i < PACKET_SIZE; i++)
        //     printf("%u ", packet[i]);
        // printf("\n");

        memcpy(last_packet, packet, PACKET_SIZE);
    }    
}

void DMX_SetValueForChannel(unsigned char channel, unsigned char value)
{
    packet[channel] = value;
}