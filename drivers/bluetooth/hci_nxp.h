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
	u32    ps_mode;
	u32    cur_psmode;
	u32    ps_state;
	u32    ps_cmd;
	u32    interval;
	u32    wakeupmode;
	u32    cur_wakeupmode;
	u32    send_cmd;
	struct work_struct work;
	struct tty_struct *tty;
	struct timer_list ps_timer;
	u32    timer_on;
};

struct nxp_struct {
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
	struct list_head        nxp_list_head;
	unsigned char           id;
	struct ps_data          *psdata;
};

/** Default ps timeout period in milli-second */
#define DEFAULT_TIME_PERIOD     2000

/** wakeup method DTR */
#define WAKEUP_METHOD_DTR       0
/** wakeup method break */
#define WAKEUP_METHOD_BREAK     1
/** wake up method EX break */
#define WAKEUP_METHOD_EXT_BREAK  2
/** wake up method RTS */
#define WAKEUP_METHOD_RTS       3
/** wakeup method invalid */
#define  WAKEUP_METHOD_INVALID  0xff

/** ps mode disable */
#define PS_MODE_DISABLE         0
/** ps mode enable */
#define PS_MODE_ENABLE          1

/** ps cmd exit ps  */
#define PS_CMD_EXIT_PS          1
/** ps cmd enter ps */
#define PS_CMD_ENTER_PS         2

/** ps state awake */
#define PS_STATE_AWAKE          0
/** ps state SLEEP */
#define PS_STATE_SLEEP          1

/** OGF */
#define OGF				        0x3F
/** Bluetooth command : Sleep mode */
#define BT_CMD_AUTO_SLEEP_MODE		0x23
/** Bluetooth command: Wakeup method */
#define BT_CMD_WAKEUP_METHOD    0x53

/** Bluetooth Power State : Enable */
#define BT_PS_ENABLE			0x02
/** Bluetooth Power State : Disable */
#define BT_PS_DISABLE			0x03

/** Bluetooth Host Wakeup Methods **/
#define BT_HOST_WAKEUP_METHOD_NONE      0x00
#define BT_HOST_WAKEUP_METHOD_DTR       0x01
#define BT_HOST_WAKEUP_METHOD_BREAK     0x02
#define BT_HOST_WAKEUP_METHOD_GPIO      0x03
#define BT_HOST_WAKEUP_DEFAULT_GPIO     5

/** Bluetooth Chip Wakeup Methods **/
#define BT_CTRL_WAKEUP_METHOD_DSR       0x00
#define BT_CTRL_WAKEUP_METHOD_BREAK     0x01
#define BT_CTRL_WAKEUP_METHOD_GPIO      0x02
#define BT_CTRL_WAKEUP_METHOD_EXT_BREAK  0x04
#define BT_CTRL_WAKEUP_METHOD_RTS       0x05
#define BT_CTRL_WAKEUP_DEFAULT_GPIO     4

#define  HCI_OP_AUTO_SLEEP_MODE         0xfc23
#define  HCI_OP_WAKEUP_METHOD           0xfc53

/** send cmd flags **/
#define SEND_WAKEUP_METHOD_CMD          0x01
#define SEND_AUTO_SLEEP_MODE_CMD        0x02

int ps_init_work(struct hci_uart *hu);
void ps_init_timer(struct hci_uart *hu);
void ps_start_timer(struct hci_uart *hu);
void ps_cancel_timer(struct hci_uart *hu);
int ps_wakeup(struct hci_uart *hu);
void ps_init(struct hci_uart *hu);
void ps_check_event_packet(struct hci_uart *hu, struct sk_buff *skb);

#endif /* _HCI_UART_H_ */
