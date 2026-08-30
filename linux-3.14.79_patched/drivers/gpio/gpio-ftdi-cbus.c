/*
 * gpiolib support for FTDI SIO chips supporting CBUS GPIO's (FT232R class)
 *
 * Copyright 2015 Stefan Agner
 *
 * Author: Stefan Agner <stefan@agner.ch>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 * Note: To use the GPIOs on CBUS the signal option need to be set to
 * I/O mode in EEPROM!
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/regmap.h>

#include <linux/usb/ftdi_sio.h>

struct ftdi_cbus_gpio {
	struct usb_serial_port *port;
	struct gpio_chip gpio_chip;
	u8 cbus_mask;
};

static inline struct ftdi_cbus_gpio *to_ftdi_cbus_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct ftdi_cbus_gpio, gpio_chip);
}

static int ftdi_cbus_gpio_direction_input(struct gpio_chip *chip,
					  unsigned offset)
{
	struct ftdi_cbus_gpio *fcg = to_ftdi_cbus_gpio(chip);

	fcg->cbus_mask &= ~((1 << offset) << 4);

	return ftdi_sio_set_bitmode(fcg->port, fcg->cbus_mask, BITMODE_CBUS);
}

static int ftdi_cbus_gpio_direction_output(struct gpio_chip *chip,
					   unsigned offset, int value)
{
	struct ftdi_cbus_gpio *fcg = to_ftdi_cbus_gpio(chip);

	fcg->cbus_mask |= ((1 << offset) << 4);
	if (value)
		fcg->cbus_mask |= (1 << offset);
	else
		fcg->cbus_mask &= ~(1 << offset);

	return ftdi_sio_set_bitmode(fcg->port, fcg->cbus_mask, BITMODE_CBUS);
}

static void ftdi_cbus_gpio_set(struct gpio_chip *chip, unsigned offset,
			       int value)
{
	struct ftdi_cbus_gpio *fcg = to_ftdi_cbus_gpio(chip);
	int ret;

	if (value)
		fcg->cbus_mask |= (1 << offset);
	else
		fcg->cbus_mask &= ~(1 << offset);

	ret = ftdi_sio_set_bitmode(fcg->port, fcg->cbus_mask, 0x20);
	if (ret < 0)
		dev_warn(chip->dev, "error setting pin value, %d\n", ret);
}

static int ftdi_cbus_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct ftdi_cbus_gpio *fcg = to_ftdi_cbus_gpio(chip);
	u8 val;
	int ret;

	ret = ftdi_sio_read_pins(fcg->port, &val);

	if (ret < 0) {
		dev_warn(chip->dev, "error getting pin value, %d\n", ret);
		return 0;
	}

	if (val & (1 << offset))
		return 1;
	else
		return 0;
}

static struct gpio_chip ftdi_cbus_gpio_chip = {
	.label			= "ftdi-cbus-gpio",
	.owner			= THIS_MODULE,
	.direction_input	= ftdi_cbus_gpio_direction_input,
	.direction_output	= ftdi_cbus_gpio_direction_output,
	.get			= ftdi_cbus_gpio_get,
	.set			= ftdi_cbus_gpio_set,
	.can_sleep		= true,
};

static int ftdi_cbus_gpio_probe(struct platform_device *pdev)
{
	struct ftdi_cbus_gpio *ftdi_cbus_gpio;
	int ret = 0;

	ftdi_cbus_gpio = devm_kzalloc(&pdev->dev, sizeof(*ftdi_cbus_gpio),
				      GFP_KERNEL);
	if (ftdi_cbus_gpio == NULL)
		return -ENOMEM;

	ftdi_cbus_gpio->port = to_usb_serial_port(pdev->dev.parent);
	ftdi_cbus_gpio->gpio_chip = ftdi_cbus_gpio_chip;
	ftdi_cbus_gpio->gpio_chip.base = -1;
	ftdi_cbus_gpio->gpio_chip.ngpio = 4;
	ftdi_cbus_gpio->gpio_chip.dev = &pdev->dev;

	ret = gpiochip_add(&ftdi_cbus_gpio->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip, %d\n",
			ret);
		goto err;
	}

	platform_set_drvdata(pdev, ftdi_cbus_gpio);

err:
	return ret;
}

static int ftdi_cbus_gpio_remove(struct platform_device *pdev)
{
	struct ftdi_cbus_gpio *fcg = platform_get_drvdata(pdev);

	gpiochip_remove(&fcg->gpio_chip);

	return 0;
}

static struct platform_driver ftdi_cbus_gpio_driver = {
	.driver.name	= "ftdi-cbus-gpio",
	.driver.owner	= THIS_MODULE,
	.probe		= ftdi_cbus_gpio_probe,
	.remove		= ftdi_cbus_gpio_remove,
};

static int __init ftdi_cbus_gpio_init(void)
{
	return platform_driver_register(&ftdi_cbus_gpio_driver);
}
subsys_initcall(ftdi_cbus_gpio_init);

static void __exit ftdi_cbus_gpio_exit(void)
{
	platform_driver_unregister(&ftdi_cbus_gpio_driver);
}
module_exit(ftdi_cbus_gpio_exit);

MODULE_AUTHOR("Stefan Agner <stefan@agner.ch>");
MODULE_DESCRIPTION("GPIO interface for FTDI SIO chips using CBUS bitmode");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ftdi-cbus-gpio");
