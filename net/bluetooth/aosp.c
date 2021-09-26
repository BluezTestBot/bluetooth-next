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

	kfree_skb(skb);
}

void aosp_do_close(struct hci_dev *hdev)
{
	if (!hdev->aosp_capable)
		return;

	bt_dev_dbg(hdev, "Cleanup of AOSP extension");
}

/* BQR command */
#define BQR_OPCODE			hci_opcode_pack(0x3f, 0x015e)

/* BQR report action */
#define REPORT_ACTION_ADD		0x00
#define REPORT_ACTION_DELETE		0x01
#define REPORT_ACTION_CLEAR		0x02

/* BQR event masks */
#define QUALITY_MONITORING		BIT(0)
#define APPRAOCHING_LSTO		BIT(1)
#define A2DP_AUDIO_CHOPPY		BIT(2)
#define SCO_VOICE_CHOPPY		BIT(3)

#define DEFAULT_BQR_EVENT_MASK	(QUALITY_MONITORING | APPRAOCHING_LSTO | \
				 A2DP_AUDIO_CHOPPY | SCO_VOICE_CHOPPY)

/* Reporting at milliseconds so as not to stress the controller too much.
 * Range: 0 ~ 65535 ms
 */
#define DEFALUT_REPORT_INTERVAL_MS	5000

struct aosp_bqr_cp {
	__u8	report_action;
	__u32	event_mask;
	__u16	min_report_interval;
} __packed;

static int enable_quality_report(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct aosp_bqr_cp cp;

	cp.report_action = REPORT_ACTION_ADD;
	cp.event_mask = DEFAULT_BQR_EVENT_MASK;
	cp.min_report_interval = DEFALUT_REPORT_INTERVAL_MS;

	skb = __hci_cmd_sync(hdev, BQR_OPCODE, sizeof(cp), &cp,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Enabling Android BQR failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	kfree_skb(skb);
	return 0;
}

static int disable_quality_report(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct aosp_bqr_cp cp = { 0 };

	cp.report_action = REPORT_ACTION_CLEAR;

	skb = __hci_cmd_sync(hdev, BQR_OPCODE, sizeof(cp), &cp,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Disabling Android BQR failed (%ld)",
			   PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	kfree_skb(skb);
	return 0;
}

int aosp_set_quality_report(struct hci_dev *hdev, bool enable)
{
	bt_dev_info(hdev, "quality report enable %d", enable);

	/* Enable or disable the quality report feature. */
	if (enable)
		return enable_quality_report(hdev);
	else
		return disable_quality_report(hdev);
}
