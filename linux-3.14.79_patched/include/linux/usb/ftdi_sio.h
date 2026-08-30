/*
 * Interface for FTDI SIO driver used by CBUS GPIO driver
 *
 * Copyright 2015 Stefan Agner
 *
 * Author: Stefan Agner <stefan@agner.ch>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __LINUX_USB_FTDI_SIO_H
#define __LINUX_USB_FTDI_SIO_H

#include <linux/usb/serial.h>

/* MPSSE bitbang modes */
enum ftdi_mpsse_mode {
	/* switch off bitbang mode, back to regular serial/FIFO */
	BITMODE_RESET = 0x00,

	/* Bitbang on CBUS pins of R-type chips, configure in EEPROM before */
	BITMODE_CBUS = 0x20,
};

int ftdi_sio_set_bitmode(struct usb_serial_port *port, u8 bitmask, u8 bitmode);
int ftdi_sio_read_pins(struct usb_serial_port *port, u8 *pins);

#endif /* __LINUX_USB_FTDI_SIO_H */

