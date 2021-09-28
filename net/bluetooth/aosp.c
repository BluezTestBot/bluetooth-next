// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "aosp.h"

void aosp_do_open(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Initialize AOSP extension");

	/* LE Get Vendor Capabilities Command */
	skb = __hci_cmd_sync(hdev, hci_opcode_pack(0x3f, 0x153), 0, NULL,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb))
		return;

	/* TODO set hdev->aosp_quality_report accordingly */

	kfree_skb(skb);
}

void aosp_do_close(struct hci_dev *hdev)
{
	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Cleanup of AOSP extension");
}

int aosp_set_quality_report(struct hci_dev *hdev, bool enable)
{
	if (!hdev->aosp_quality_report)
		return -EOPNOTSUPP;

	/* TODO execute HCI command */

	return -EOPNOTSUPP;
}

bool aosp_has_quality_report(struct hci_dev *hdev)
{
	return hdev->aosp_quality_report;
}
