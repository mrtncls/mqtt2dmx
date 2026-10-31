#ifndef TEST_FTDI_API_H
#define TEST_FTDI_API_H

#include <ftdi.h>

int ftdi_init(struct ftdi_context *ftdi);
void ftdi_deinit(struct ftdi_context *ftdi);
int ftdi_usb_close(struct ftdi_context *ftdi);
int ftdi_usb_open_desc(struct ftdi_context *ftdi, int vendor, int product,
                       const char *description, const char *serial);
int ftdi_usb_reset(struct ftdi_context *ftdi);
int ftdi_set_interface(struct ftdi_context *ftdi, enum ftdi_interface interface);
int ftdi_set_baudrate(struct ftdi_context *ftdi, int baudrate);
int ftdi_set_line_property(struct ftdi_context *ftdi, enum ftdi_bits_type bits,
                           enum ftdi_stopbits_type sbit, enum ftdi_parity_type parity);
int ftdi_setflowctrl(struct ftdi_context *ftdi, int flowctrl);
int ftdi_set_line_property2(struct ftdi_context *ftdi, enum ftdi_bits_type bits,
                            enum ftdi_stopbits_type sbit, enum ftdi_parity_type parity,
                            enum ftdi_break_type break_type);
int ftdi_write_data(struct ftdi_context *ftdi, const unsigned char *buf, int size);

#endif
