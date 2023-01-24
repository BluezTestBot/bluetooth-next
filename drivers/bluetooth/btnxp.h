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

#define BT_FW_CONF_FILE             "nxp/bt_mod_para.conf"
#define FW_NAME_TAG                 "fw_name"
#define OPER_SPEED_TAG              "oper_speed"
#define FW_DL_PRI_BAUDRATE_TAG      "fw_dl_pri_speed"
#define FW_DL_SEC_BAUDRATE_TAG      "fw_dl_sec_speed"
#define FW_INIT_BAUDRATE            "fw_init_speed"

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
#define HCI_NXP_AUTO_SLEEP_MODE	0xFC23
/* Bluetooth vendor command : Wakeup method */
#define HCI_NXP_WAKEUP_METHOD	0xFC53
/* Bluetooth vendor command : Set operational baudrate */
#define HCI_NXP_SET_OPER_SPEED	0xFC09

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
	u8    driver_sent_cmd;
	u8    timer_on;
	u32   interval;
	struct hci_dev *hdev;
	struct work_struct work;
	struct timer_list ps_timer;
};

struct btnxpuart_data {
	const struct h4_recv_pkt *recv_pkts;
	int recv_pkts_cnt;
	unsigned int manufacturer;
	int (*open)(struct hci_dev *hdev);
	int (*close)(struct hci_dev *hdev);
	int (*setup)(struct hci_dev *hdev);
	int (*enqueue)(struct hci_dev *hdev, struct sk_buff *skb);
	struct sk_buff *(*dequeue)(void *data);
	u32 fw_dnld_pri_baudrate;
	u32 fw_dnld_sec_baudrate;
	u32 fw_init_baudrate;
	u32 oper_speed;
};

struct btnxpuart_dev {
	struct hci_dev *hdev;
	struct serdev_device *serdev;

	struct work_struct tx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;
	struct sk_buff *rx_skb;

	const struct firmware *fw;
	const struct firmware *fw_config;
	u8 fw_name[MAX_FW_FILE_NAME_LEN];
	u32 fw_dnld_offset;
	u32 fw_sent_bytes;
	u32 fw_v3_offset_correction;
	wait_queue_head_t suspend_wait_q;

	u32 fw_dnld_pri_baudrate;
	u32 fw_dnld_sec_baudrate;
	u32 fw_init_baudrate;
	u32 oper_speed;
	u32 new_baudrate;
	u32 current_baudrate;

	struct ps_data *psdata;
	const struct btnxpuart_data *nxp_data;
};

struct chip_id_map_table {
	u16 chip_id;
	const u8 *chip_name;
};

struct fw_params {
	u16 chip_id;
	u8  chip_name[MAX_CHIP_NAME_LEN];
	u8  fw_name[MAX_FW_FILE_NAME_LEN];
	u32 fw_dnld_pri_baudrate;
	u32 fw_dnld_sec_baudrate;
	u32 fw_init_baudrate;
	u32 oper_speed;
};

#define NXP_V1_FW_REQ_PKT      0xA5
#define NXP_V1_CHIP_VER_PKT    0xAA
#define NXP_V3_FW_REQ_PKT      0xA7
#define NXP_V3_CHIP_VER_PKT    0xAB

#define NXP_ACK_V1             0x5A
#define NXP_NAK_V1             0xBF
#define NXP_ACK_V3             0x7A
#define NXP_NAK_V3             0x7B
#define NXP_CRC_ERROR_V3       0x7C

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

struct V1_DATA_REQ {
	u16 len;
	u16 len_comp;
} __packed;

struct V3_DATA_REQ {
	u16 len;
	u32 offset;
	u16 error;
	u8 crc;
} __packed;

struct V3_START_IND {
	u16 chip_id;
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

#define SWAPL(x) ((((x) >> 24) & 0xff) \
				 | (((x) >> 8) & 0xff00) \
				 | (((x) << 8) & 0xff0000L) \
				 | (((x) << 24) & 0xff000000L))

#define POLYNOMIAL8				0x07
#define POLYNOMIAL32			0x04c11db7L

static void btnxpuart_tx_wakeup(struct btnxpuart_dev *nxpdev);

#endif
