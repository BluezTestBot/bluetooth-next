/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  Bluetooth HCI UART driver
 *  Copyright 2018-2022 NXP
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */
#ifndef _HCI_NXP_H_
#define _HCI_NXP_H_

#define TTY_FUNC tty->ops

struct ps_data {
	u8    ps_mode;
	u8    cur_psmode;
	u8    ps_state;
	u8    ps_cmd;
	u8    wakeupmode;
	u8    cur_wakeupmode;
	u8    driver_sent_cmd;
	u8    timer_on;
	u32   interval;
	struct work_struct work;
	struct tty_struct *tty;
	struct timer_list ps_timer;
};

struct nxp_struct {
	struct sk_buff      *rx_skb;
	struct sk_buff_head  txq;
	struct list_head     nxp_list_head;
	unsigned char        id;
	struct ps_data      *psdata;
};

/* Default ps timeout period in milli-second */
#define PS_DEFAULT_TIMEOUT_PERIOD     2000

/* wakeup methods */
#define WAKEUP_METHOD_DTR       0
#define WAKEUP_METHOD_BREAK     1
#define WAKEUP_METHOD_EXT_BREAK 2
#define WAKEUP_METHOD_RTS       3
#define WAKEUP_METHOD_INVALID   0xff

/* ps mode disable */
#define PS_MODE_DISABLE         0
/* ps mode enable */
#define PS_MODE_ENABLE          1

/* PS Commands to ps_work_func  */
#define PS_CMD_EXIT_PS          1
#define PS_CMD_ENTER_PS         2

/* ps state */
#define PS_STATE_AWAKE          0
#define PS_STATE_SLEEP          1

/* Bluetooth vendor command : Sleep mode */
#define HCI_NXP_AUTO_SLEEP_MODE	0xFC23
/* Bluetooth vendor command : Wakeup method */
#define HCI_NXP_WAKEUP_METHOD	0xFC53

/* Bluetooth Power State : Vendor cmd params */
#define BT_PS_ENABLE			0x02
#define BT_PS_DISABLE			0x03

/* Bluetooth Host Wakeup Methods */
#define BT_HOST_WAKEUP_METHOD_NONE      0x00
#define BT_HOST_WAKEUP_METHOD_DTR       0x01
#define BT_HOST_WAKEUP_METHOD_BREAK     0x02
#define BT_HOST_WAKEUP_METHOD_GPIO      0x03
#define BT_HOST_WAKEUP_DEFAULT_GPIO     20

/* Bluetooth Chip Wakeup Methods */
#define BT_CTRL_WAKEUP_METHOD_DSR       0x00
#define BT_CTRL_WAKEUP_METHOD_BREAK     0x01
#define BT_CTRL_WAKEUP_METHOD_GPIO      0x02
#define BT_CTRL_WAKEUP_METHOD_EXT_BREAK 0x04
#define BT_CTRL_WAKEUP_METHOD_RTS       0x05
#define BT_CTRL_WAKEUP_DEFAULT_GPIO     4

#endif /* _HCI_UART_H_ */
