// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#if IS_ENABLED(CONFIG_BT_AOSPEXT)

void aosp_do_open(struct hci_dev *hdev);
void aosp_do_close(struct hci_dev *hdev);

bool aosp_has_quality_report(struct hci_dev *hdev);
int aosp_set_quality_report(struct hci_dev *hdev, bool enable);
bool aosp_check_quality_report_len(struct sk_buff *skb);
struct ext_vendor_prefix *aosp_get_ext_prefix(struct hci_dev *hdev);
void aosp_vendor_evt(struct hci_dev *hdev, struct sk_buff *skb);

#else

static inline void aosp_do_open(struct hci_dev *hdev) {}
static inline void aosp_do_close(struct hci_dev *hdev) {}

static inline bool aosp_has_quality_report(struct hci_dev *hdev)
{
	return false;
}

static inline int aosp_set_quality_report(struct hci_dev *hdev, bool enable)
{
	return -EOPNOTSUPP;
}

static inline bool aosp_check_quality_report_len(struct sk_buff *skb)
{
	return false;
}

static inline struct ext_vendor_prefix *
aosp_get_ext_prefix(struct hci_dev *hdev)
{
	return NULL;
}

static inline void aosp_vendor_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
}

#endif
