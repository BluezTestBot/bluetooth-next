/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) 2016,2017 MediaTek Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef _BTMTK_SDIO_H_
#define _BTMTK_SDIO_H_
/* It's for reset procedure */
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>

#include <linux/of_gpio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "btmtk_define.h"
#include "btmtk_main.h"
#include "btmtk_buffer_mode.h"

#ifndef SDIO_DEBUG
#define SDIO_DEBUG 0
#endif

/**
 * Card-relate definition.
 */
#define SDIO_VENDOR_ID_MEDIATEK 0x037A

#define HCI_HEADER_LEN	4

#define MTK_STP_TLR_SIZE	2
#define STP_HEADER_LEN	4
#define STP_HEADER_CRC_LEN	2
#define HCI_MAX_COMMAND_SIZE	255
#define URB_MAX_BUFFER_SIZE	(4*1024)
#define BTMTK_SDIO_FUNC 2


/* common register address */
#define CCIR		0x0000
#define CHLPCR		0x0004
#define CSDIOCSR	0x0008
#define CHCR		0x000C
#define CHISR		0x0010
#define CHIER		0x0014
#define CTDR		0x0018
#define CRDR		0x001C
#define CTFSR		0x0020
#define CRPLR		0x0024
#define PD2HRM0R	0x00DC
#define SWPCDBGR	0x0154
/* CHLPCR */
#define C_FW_INT_EN_SET			0x00000001
#define C_FW_INT_EN_CLEAR		0x00000002
/* CHISR */
#define RX_PKT_LEN				0xFFFF0000
#define FIRMWARE_INT			0x0000FE00
/* MCU notify host dirver for L0.5 reset */
#define FIRMWARE_INT_BIT31		0x80000000
/* MCU notify host driver for coredump */
#define FIRMWARE_INT_BIT15		0x00008000
#define TX_FIFO_OVERFLOW		0x00000100
#define FW_INT_IND_INDICATOR	0x00000080
#define TX_COMPLETE_COUNT		0x00000070
#define TX_UNDER_THOLD			0x00000008
#define TX_EMPTY				0x00000004
#define RX_DONE					0x00000002
#define FW_OWN_BACK_INT			0x00000001

/* MCU address offset */
#define MCU_ADDRESS_OFFSET_CMD 12
#define MCU_ADDRESS_OFFSET_EVT 16

/* wifi CR */
#define CONDBGCR		0x0034
#define CONDBGCR_SEL		0x0040
#define SDIO_CTRL_EN		(1 << 31)
#define WM_MONITER_SEL		(~(0x40000000))
#define PC_MONITER_SEL		(~(0x20000000))
#define PC_IDX_SWH(val, idx)	((val & (~(0x3F << 16))) | ((0x3F & idx) << 16))

typedef int (*pdwnc_func) (u8 fgReset);
typedef int (*reset_func_ptr2) (unsigned int gpio, int init_value);
typedef int (*set_gpio_low)(u8 gpio);
typedef int (*set_gpio_high)(u8 gpio);


/**
 * Send cmd dispatch evt
 */
#define HCI_EV_VENDOR			0xff
#define SDIO_BLOCK_SIZE                 512
#define SDIO_RW_RETRY_COUNT 500
#define MTK_SDIO_PACKET_HEADER_SIZE 4

/* Driver & FW own related */
#define DRIVER_OWN 0
#define FW_OWN 1
#define SET_OWN_LOOP_COUNT 20

struct btmtk_sdio_hdr {
	/* For SDIO Header */
	__le16	len;
	__le16	reserved;
	/* For hci type */
	u8	bt_type;
} __packed;

struct btmtk_sdio_thread {
	struct task_struct *task;
	wait_queue_head_t wait_q;
	void *priv;
	u8 thread_status;
};

struct btmtk_sdio_dev {
	struct sdio_func *func;

	bool no_fw_own;
	atomic_t int_count;
	atomic_t tx_rdy;

	/* TODO, need to confirm the max size of urb data, also need to confirm
	 * whether intr_complete and bulk_complete and soc_complete can all share
	 * this urb_transfer_buf
	 */
	unsigned char	*transfer_buf;
	unsigned char	*sdio_packet;

	struct sk_buff_head tx_queue;
	struct btmtk_sdio_thread sdio_thread;

	struct btmtk_buffer_mode_struct *buffer_mode;
};
#endif
