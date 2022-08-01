/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2014 Intel Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#include <linux/sched/signal.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "smp.h"
#include "hci_request.h"
#include "msft.h"
#include "eir.h"

void hci_req_init(struct hci_request *req, struct hci_dev *hdev)
{
	skb_queue_head_init(&req->cmd_q);
	req->hdev = hdev;
	req->err = 0;
}

void hci_req_purge(struct hci_request *req)
{
	skb_queue_purge(&req->cmd_q);
}

bool hci_req_status_pend(struct hci_dev *hdev)
{
	return hdev->req_status == HCI_REQ_PEND;
}

static int req_run(struct hci_request *req, hci_req_complete_t complete,
		   hci_req_complete_skb_t complete_skb)
{
	struct hci_dev *hdev = req->hdev;
	struct sk_buff *skb;
	unsigned long flags;

	bt_dev_dbg(hdev, "length %u", skb_queue_len(&req->cmd_q));

	/* If an error occurred during request building, remove all HCI
	 * commands queued on the HCI request queue.
	 */
	if (req->err) {
		skb_queue_purge(&req->cmd_q);
		return req->err;
	}

	/* Do not allow empty requests */
	if (skb_queue_empty(&req->cmd_q))
		return -ENODATA;

	skb = skb_peek_tail(&req->cmd_q);
	if (complete) {
		bt_cb(skb)->hci.req_complete = complete;
	} else if (complete_skb) {
		bt_cb(skb)->hci.req_complete_skb = complete_skb;
		bt_cb(skb)->hci.req_flags |= HCI_REQ_SKB;
	}

	spin_lock_irqsave(&hdev->cmd_q.lock, flags);
	skb_queue_splice_tail(&req->cmd_q, &hdev->cmd_q);
	spin_unlock_irqrestore(&hdev->cmd_q.lock, flags);

	queue_work(hdev->workqueue, &hdev->cmd_work);

	return 0;
}

int hci_req_run(struct hci_request *req, hci_req_complete_t complete)
{
	return req_run(req, complete, NULL);
}

int hci_req_run_skb(struct hci_request *req, hci_req_complete_skb_t complete)
{
	return req_run(req, NULL, complete);
}

void hci_req_sync_complete(struct hci_dev *hdev, u8 result, u16 opcode,
			   struct sk_buff *skb)
{
	bt_dev_dbg(hdev, "result 0x%2.2x", result);

	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = result;
		hdev->req_status = HCI_REQ_DONE;
		if (skb)
			hdev->req_skb = skb_get(skb);
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

/* Execute request and wait for completion. */
int __hci_req_sync(struct hci_dev *hdev, int (*func)(struct hci_request *req,
						     unsigned long opt),
		   unsigned long opt, u32 timeout, u8 *hci_status)
{
	struct hci_request req;
	int err = 0;

	bt_dev_dbg(hdev, "start");

	hci_req_init(&req, hdev);

	hdev->req_status = HCI_REQ_PEND;

	err = func(&req, opt);
	if (err) {
		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;
		return err;
	}

	err = hci_req_run_skb(&req, hci_req_sync_complete);
	if (err < 0) {
		hdev->req_status = 0;

		/* ENODATA means the HCI request command queue is empty.
		 * This can happen when a request with conditionals doesn't
		 * trigger any commands to be sent. This is normal behavior
		 * and should not trigger an error return.
		 */
		if (err == -ENODATA) {
			if (hci_status)
				*hci_status = 0;
			return 0;
		}

		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;

		return err;
	}

	err = wait_event_interruptible_timeout(hdev->req_wait_q,
			hdev->req_status != HCI_REQ_PEND, timeout);

	if (err == -ERESTARTSYS)
		return -EINTR;

	switch (hdev->req_status) {
	case HCI_REQ_DONE:
		err = -bt_to_errno(hdev->req_result);
		if (hci_status)
			*hci_status = hdev->req_result;
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;
		break;

	default:
		err = -ETIMEDOUT;
		if (hci_status)
			*hci_status = HCI_ERROR_UNSPECIFIED;
		break;
	}

	kfree_skb(hdev->req_skb);
	hdev->req_skb = NULL;
	hdev->req_status = hdev->req_result = 0;

	bt_dev_dbg(hdev, "end: err %d", err);

	return err;
}

int hci_req_sync(struct hci_dev *hdev, int (*req)(struct hci_request *req,
						  unsigned long opt),
		 unsigned long opt, u32 timeout, u8 *hci_status)
{
	int ret;

	/* Serialize all requests */
	hci_req_sync_lock(hdev);
	/* check the state after obtaing the lock to protect the HCI_UP
	 * against any races from hci_dev_do_close when the controller
	 * gets removed.
	 */
	if (test_bit(HCI_UP, &hdev->flags))
		ret = __hci_req_sync(hdev, req, opt, timeout, hci_status);
	else
		ret = -ENETDOWN;
	hci_req_sync_unlock(hdev);

	return ret;
}

struct sk_buff *hci_prepare_cmd(struct hci_dev *hdev, u16 opcode, u32 plen,
				const void *param)
{
	int len = HCI_COMMAND_HDR_SIZE + plen;
	struct hci_command_hdr *hdr;
	struct sk_buff *skb;

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb)
		return NULL;

	hdr = skb_put(skb, HCI_COMMAND_HDR_SIZE);
	hdr->opcode = cpu_to_le16(opcode);
	hdr->plen   = plen;

	if (plen)
		skb_put_data(skb, param, plen);

	bt_dev_dbg(hdev, "skb len %d", skb->len);

	hci_skb_pkt_type(skb) = HCI_COMMAND_PKT;
	hci_skb_opcode(skb) = opcode;

	return skb;
}

/* Queue a command to an asynchronous HCI request */
void hci_req_add_ev(struct hci_request *req, u16 opcode, u32 plen,
		    const void *param, u8 event)
{
	struct hci_dev *hdev = req->hdev;
	struct sk_buff *skb;

	bt_dev_dbg(hdev, "opcode 0x%4.4x plen %d", opcode, plen);

	/* If an error occurred during request building, there is no point in
	 * queueing the HCI command. We can simply return.
	 */
	if (req->err)
		return;

	skb = hci_prepare_cmd(hdev, opcode, plen, param);
	if (!skb) {
		bt_dev_err(hdev, "no memory for command (opcode 0x%4.4x)",
			   opcode);
		req->err = -ENOMEM;
		return;
	}

	if (skb_queue_empty(&req->cmd_q))
		bt_cb(skb)->hci.req_flags |= HCI_REQ_START;

	hci_skb_event(skb) = event;

	skb_queue_tail(&req->cmd_q, skb);
}

void hci_req_add(struct hci_request *req, u16 opcode, u32 plen,
		 const void *param)
{
	bt_dev_dbg(req->hdev, "HCI_REQ-0x%4.4x", opcode);
	hci_req_add_ev(req, opcode, plen, param, 0);
}
