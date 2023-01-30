/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  NXP Bluetooth driver
 *  Copyright 2018-2023 NXP
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

#ifndef BT_NXP_H_
#define BT_NXP_H_

#define FIRMWARE_W8987	"nxp/uartuart8987_bt.bin"
#define FIRMWARE_W8997	"nxp/uartuart8997_bt_v4.bin"
#define FIRMWARE_W9098	"nxp/uartuart9098_bt_v1.bin"
#define FIRMWARE_IW416	"nxp/uartuart_iw416_bt.bin"
#define FIRMWARE_IW612	"nxp/uartspi_n61x_v1.bin"

#define MAX_CHIP_NAME_LEN       20
#define MAX_FW_FILE_NAME_LEN    50
#define MAX_NO_OF_CHIPS_SUPPORT 20

/* Default ps timeout period in milli-second */
#define PS_DEFAULT_TIMEOUT_PERIOD     2000

/* wakeup methods */
#define WAKEUP_METHOD_DTR       0
#define WAKEUP_METHOD_BREAK     1
#define WAKEUP_METHOD_EXT_BREAK 2
#define WAKEUP_METHOD_RTS       3
#define WAKEUP_METHOD_INVALID   0xff

/* power save mode status */
#define PS_MODE_DISABLE         0
#define PS_MODE_ENABLE          1

/* Power Save Commands to ps_work_func  */
#define PS_CMD_EXIT_PS          1
#define PS_CMD_ENTER_PS         2

/* power save state */
#define PS_STATE_AWAKE          0
#define PS_STATE_SLEEP          1

/* Bluetooth vendor command : Sleep mode */
#define HCI_NXP_AUTO_SLEEP_MODE	0xfc23
/* Bluetooth vendor command : Wakeup method */
#define HCI_NXP_WAKEUP_METHOD	0xfc53
/* Bluetooth vendor command : Set operational baudrate */
#define HCI_NXP_SET_OPER_SPEED	0xfc09

/* Bluetooth Power State : Vendor cmd params */
#define BT_PS_ENABLE			0x02
#define BT_PS_DISABLE			0x03

/* Bluetooth Host Wakeup Methods */
#define BT_HOST_WAKEUP_METHOD_NONE      0x00
#define BT_HOST_WAKEUP_METHOD_DTR       0x01
#define BT_HOST_WAKEUP_METHOD_BREAK     0x02
#define BT_HOST_WAKEUP_METHOD_GPIO      0x03
#define BT_HOST_WAKEUP_DEFAULT_GPIO     5

/* Bluetooth Chip Wakeup Methods */
#define BT_CTRL_WAKEUP_METHOD_DSR       0x00
#define BT_CTRL_WAKEUP_METHOD_BREAK     0x01
#define BT_CTRL_WAKEUP_METHOD_GPIO      0x02
#define BT_CTRL_WAKEUP_METHOD_EXT_BREAK 0x04
#define BT_CTRL_WAKEUP_METHOD_RTS       0x05
#define BT_CTRL_WAKEUP_DEFAULT_GPIO     4

struct ps_data {
	u8    ps_mode;
	u8    cur_psmode;
	u8    ps_state;
	u8    ps_cmd;
	u8    wakeupmode;
	u8    cur_wakeupmode;
	bool  driver_sent_cmd;
	u8    timer_on;
	u32   interval;
	struct hci_dev *hdev;
	struct work_struct work;
	struct timer_list ps_timer;
};

struct btnxpuart_data {
	const struct h4_recv_pkt *recv_pkts;
	int recv_pkts_cnt;
	int (*open)(struct hci_dev *hdev);
	int (*close)(struct hci_dev *hdev);
	int (*setup)(struct hci_dev *hdev);
	int (*enqueue)(struct hci_dev *hdev, struct sk_buff *skb);
	struct sk_buff *(*dequeue)(void *data);
	u32 fw_dnld_pri_baudrate;
	u32 fw_dnld_sec_baudrate;
	u32 fw_init_baudrate;
	u32 oper_speed;
	u16 chip_signature;
	const u8 *fw_name;
};

struct btnxpuart_dev {
	struct hci_dev *hdev;
	struct serdev_device *serdev;

	struct work_struct tx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;
	struct sk_buff *rx_skb;

	const struct firmware *fw;
	u8 fw_name[MAX_FW_FILE_NAME_LEN];
	u32 fw_dnld_offset;
	u32 fw_sent_bytes;
	u32 fw_v3_offset_correction;
	wait_queue_head_t suspend_wait_q;

	u32 new_baudrate;
	u32 current_baudrate;

	struct ps_data *psdata;
	const struct btnxpuart_data *nxp_data;
};

#define NXP_V1_FW_REQ_PKT      0xa5
#define NXP_V1_CHIP_VER_PKT    0xaa
#define NXP_V3_FW_REQ_PKT      0xa7
#define NXP_V3_CHIP_VER_PKT    0xab

#define NXP_ACK_V1             0x5a
#define NXP_NAK_V1             0xbf
#define NXP_ACK_V3             0x7a
#define NXP_NAK_V3             0x7b
#define NXP_CRC_ERROR_V3       0x7c

#define HDR_LEN					16

#define NXP_RECV_FW_REQ_V1 \
	.type = NXP_V1_FW_REQ_PKT, \
	.hlen = 4, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = 4

#define NXP_RECV_CHIP_VER_V3 \
	.type = NXP_V3_CHIP_VER_PKT, \
	.hlen = 4, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = 4

#define NXP_RECV_FW_REQ_V3 \
	.type = NXP_V3_FW_REQ_PKT, \
	.hlen = 9, \
	.loff = 0, \
	.lsize = 0, \
	.maxlen = 9

struct v1_data_req {
	__le16 len;
	__le16 len_comp;
} __packed;

struct v3_data_req {
	__le16 len;
	__le32 offset;
	__le16 error;
	u8 crc;
} __packed;

struct v3_start_ind {
	__le16 chip_id;
	u8 loader_ver;
	u8 crc;
} __packed;

/* UART register addresses of BT chip */
#define CLKDIVADDR       0x7f00008f
#define UARTDIVADDR      0x7f000090
#define UARTMCRADDR      0x7f000091
#define UARTREINITADDR   0x7f000092
#define UARTICRADDR      0x7f000093
#define UARTFCRADDR      0x7f000094

#define MCR   0x00000022
#define INIT  0x00000001
#define ICR   0x000000c7
#define FCR   0x000000c7

#define POLYNOMIAL8				0x07
#define POLYNOMIAL32			0x04c11db7L

struct uart_reg {
	__le32 address;
	__le32 value;
} __packed;

struct uart_config {
	struct uart_reg clkdiv;
	struct uart_reg uartdiv;
	struct uart_reg mcr;
	struct uart_reg re_init;
	struct uart_reg icr;
	struct uart_reg fcr;
	__le32 crc;
} __packed;

struct nxp_bootloader_cmd {
	__le32 header;
	__le32 arg;
	__le32 payload_len;
	__le32 crc;
} __packed;

static void btnxpuart_tx_wakeup(struct btnxpuart_dev *nxpdev);

#endif
