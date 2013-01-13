/* include/linux/steelhead_avr.h
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_STEELHEAD_AVR_H
#define __LINUX_STEELHEAD_AVR_H

#ifdef __KERNEL__

struct steelhead_avr_platform_data {
	/* GPIO ID of the interrupt request from AVR -> CPU */
	unsigned interrupt_gpio;
	/* GPIO ID of the reset request from CPU -> AVR */
	unsigned reset_gpio;
};

#endif

#include <linux/ioctl.h>

struct avr_led_rgb_vals {
	u8 rgb[3];
};

struct avr_led_set_range_vals {
	u8 start;
	u8 count;
	u8 rgb_triples;
	struct avr_led_rgb_vals rgb_vals[0]; /* array of size rgb_triples */
};

#define AVR_LED_MAGIC 0xE2

#define AVR_LED_GET_FIRMWARE_REVISION _IOR(AVR_LED_MAGIC, 1, __u16)
#define AVR_LED_GET_HARDWARE_TYPE     _IOR(AVR_LED_MAGIC, 2, __u8)
#define AVR_LED_GET_HARDWARE_REVISION _IOR(AVR_LED_MAGIC, 3, __u8)
#define AVR_LED_GET_MODE              _IOR(AVR_LED_MAGIC, 4, __u8)
#define AVR_LED_SET_MODE              _IOW(AVR_LED_MAGIC, 5, __u8)
#define AVR_LED_GET_COUNT             _IOR(AVR_LED_MAGIC, 6, __u8)
#define AVR_LED_SET_ALL_VALS          _IOW(AVR_LED_MAGIC, 9, \
					   struct avr_led_rgb_vals)
#define AVR_LED_SET_RANGE_VALS        _IOW(AVR_LED_MAGIC, 11, \
					   struct avr_led_set_range_vals)
#define AVR_LED_COMMIT_LED_STATE      _IOW(AVR_LED_MAGIC, 13, __u8)
#define AVR_LED_RESET                 _IO(AVR_LED_MAGIC, 14)
#define AVR_LED_SET_MUTE              _IOW(AVR_LED_MAGIC, 15, \
					   struct avr_led_rgb_vals)


#endif  /* __LINUX_STEELHEAD_AVR_H */
