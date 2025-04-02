/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

struct hci_drv_cmd_hdr {
	__le16	opcode;
	__le16	len;
} __packed;

struct hci_drv_resp_hdr {
	__le16	opcode;
	__le16	len;
} __packed;

struct hci_drv_resp_status {
	__u8	status;
} __packed;

#define HCI_DRV_STATUS_SUCCESS			0x00
#define HCI_DRV_STATUS_UNSPECIFIED_ERROR	0x01
#define HCI_DRV_STATUS_UNKNOWN_COMMAND		0x02
#define HCI_DRV_STATUS_INVALID_PARAMETERS	0x03

/* Common commands that make sense on all drivers start from 0x0000. */

#define HCI_DRV_OP_READ_SUPPORTED_DRIVER_COMMANDS	0x0000
struct hci_drv_resp_read_supported_driver_commands {
	__u8	status;
	__le16	num_commands;
	__le16	commands[];
} __packed;

/* btusb specific commands start from 0x1135.
 * No particular reason - It's my lucky number.
 */

#define HCI_DRV_OP_SWITCH_ALT_SETTING	0x1135
struct hci_drv_cmd_switch_alt_setting {
	__u8	new_alt;
} __packed;

static inline struct sk_buff *hci_drv_skb_alloc(u16 opcode, u16 plen, gfp_t how)
{
	struct hci_drv_resp_hdr *hdr;
	struct sk_buff *skb;

	skb = bt_skb_alloc(sizeof(*hdr) + plen, how);
	if (!skb)
		return NULL;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->opcode = __cpu_to_le16(opcode);
	hdr->len = __cpu_to_le16(plen);

	hci_skb_pkt_type(skb) = HCI_DRV_PKT;

	return skb;
}
