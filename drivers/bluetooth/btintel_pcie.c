// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Intel Bluetooth PCIE driver
 *
 * Copyright (C) 2017 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2022  Intel Corporation
 *
 * Intel Bluetooth Driver for PCIE interface.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"
#include "btintel_pcie.h"

#define VERSION "0.1"

#define BTINTEL_PCI_DEVICE(dev, subdev)	\
	.vendor = PCI_VENDOR_ID_INTEL,	\
	.device = (dev),		\
	.subvendor = PCI_ANY_ID,	\
	.subdevice = (subdev),		\
	.driver_data = 0

/* Intel Bluetooth PCIe device id table */
static const struct pci_device_id btintel_pcie_table[] = {
	{ BTINTEL_PCI_DEVICE(0xA876, PCI_ANY_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, btintel_pcie_table);

/* Intel PCIe uses 4 bytes of HCI type instead of 1 byte BT SIG HCI type */
#define BTINTEL_PCIE_HCI_TYPE_LEN	4
#define BTINTEL_PCIE_HCI_CMD_PKT	0x00000001
#define BTINTEL_PCIE_HCI_ACL_PKT	0x00000002
#define BTINTEL_PCIE_HCI_SCO_PKT	0x00000003
#define BTINTEL_PCIE_HCI_EVT_PKT	0x00000004

#define BTITNEL_PCIE_ENABLE_HCI_DUMP	0

#if BTITNEL_PCIE_ENABLE_HCI_DUMP
static inline void btintel_pcie_hci_dump(const char *p, const void *b, int s)
{
	const unsigned char *ptr = (const unsigned char *)b;
	char str[64];
	int c, i;

	for (i = c = 0; c < s; c++) {
		i += snprintf(str + i, sizeof(str) - i, "%02x ", ptr[c]);
		if ((c > 0 && (c + 1) % 8 == 0) || (c == s - 1)) {
			BT_DBG("%s: %s", p, str);
			i = 0;
		}
	}
}
#else
static inline void btintel_pcie_hci_dump(const char *p, const void *b, int s)
{
}
#endif

static void ipc_print_ia_ring(struct ia *ia, u16 queue_num)
{
	BT_DBG("[%s] ---------------- ia ----------------",
	       queue_num == TXQ_NUM ? "TXQ" : "RXQ");
	BT_DBG("[%s] tr-h:%02u  tr-t:%02u  cr-h:%02u  cr-t:%02u",
	       queue_num == TXQ_NUM ? "TXQ" : "RXQ",
	       ia->tr_hia[queue_num], ia->tr_tia[queue_num],
	       ia->cr_hia[queue_num], ia->cr_tia[queue_num]);
}

static void ipc_print_urbd0(struct urbd0 *urbd0, u16 index)
{
	BT_DBG("[TXQ] -------------- urbd0[%u] --------------", index);
	BT_DBG("[TXQ] tfd_index:%u num_txq:%u cmpl_cnt:%u immediate_cmpl:0x%x",
	       urbd0->tfd_index, urbd0->num_txq, urbd0->cmpl_count,
	       urbd0->immediate_cmpl);
}

static void ipc_print_frbd(struct frbd *frbd, u16 index)
{
	BT_DBG("[RXQ] -------------- frbd[%u] --------------", index);
	BT_DBG("[RXQ] tag:%u addr:0x%llx", frbd->tag, frbd->addr);
}

static void ipc_print_urbd1(struct urbd1 *urbd1, u16 index)
{
	BT_DBG("[RXQ] -------------- urbd1[%u] --------------", index);
	BT_DBG("[RXQ] frbd_tag:%u status: 0x%x fixed:0x%x",
	       urbd1->frbd_tag, urbd1->status, urbd1->fixed);
}

/* Poll internal in microseconds */
#define POLL_INTERVAL			10

static int btintel_pcie_poll_bit(struct btintel_pcie_data *data, u32 offset,
				 u32 bits, u32 mask, int timeout)
{
	int t = 0;
	u32 reg;

	BT_DBG("Enter poll_bit");
	do {
		reg = btintel_pcie_rd_reg32(data, offset);
		BT_DBG("CURRENT FUNC_CTRL_REG: 0x%x", reg);

		if ((reg & mask) == (bits & mask)) {
			BT_DBG("Poll bit matched");
			return t;
		}
		udelay(POLL_INTERVAL);
		t += POLL_INTERVAL;
		BT_DBG("Poll wait: %d", t);
	} while (t < timeout);

	return -ETIMEDOUT;
}

static struct btintel_pcie_data *btintel_pcie_get_data(struct msix_entry *entry)
{
	u8 queue = entry->entry;
	struct msix_entry *entries = entry - queue;

	return container_of(entries, struct btintel_pcie_data, msix_entries[0]);
}

/* Set the doorbell for RXQ to notify the device that @index(actually index-1)
 * is available to receive the data
 */
static void btintel_pcie_set_rx_db(struct btintel_pcie_data *data, u16 index)
{
	u32 val;

	val = index;
	val |= (513 << 16);

	BT_DBG("[RXQ] Set doorbell for index: %u", index);
	btintel_pcie_wr_reg32(data, CSR_HBUS_TARG_WRPTR, val);
}

/* Update the FRBD(free buffer descriptor) with the @frbd_index and the
 * DMA address of the free buffer.
 */
static void btintel_pcie_prepare_rx(struct rxq *rxq, u16 frbd_index)
{
	struct data_buf *buf;
	struct frbd *frbd;

	/* Get the buffer of the frbd for DMA */
	buf = &rxq->bufs[frbd_index];

	frbd = &rxq->frbds[frbd_index];
	memset(frbd, 0, sizeof(*frbd));

	/* Update FRBD */
	frbd->tag = frbd_index;
	frbd->addr = buf->data_p_addr;
	ipc_print_frbd(frbd, frbd_index);
}

static int btintel_pcie_submit_rx(struct btintel_pcie_data *data)
{
	u16 frbd_index;
	struct rxq *rxq = &data->rxq;

	/* Read the frbd index from the TR_HIA(Head Index Array) for RXQ */
	frbd_index = data->ia.tr_hia[RXQ_NUM];
	BT_DBG("[RXQ] current frbd_index: %u", frbd_index);

	/* Make sure the index value is within the range. It shouldn't be
	 * bigger than the total count of the queue.
	 */
	if (frbd_index > rxq->count) {
		BT_ERR("[RXQ] RXQ out of range: (0x%x)", frbd_index);
		return -ERANGE;
	}

	/* Prepare for RX submit. It updates the FRBD with the address of DMA
	 * buffer
	 */
	btintel_pcie_prepare_rx(rxq, frbd_index);

	/* Update TR_HIA with new FRBD index */
	frbd_index = (frbd_index + 1) % rxq->count;
	data->ia.tr_hia[RXQ_NUM] = frbd_index;
	ipc_print_ia_ring(&data->ia, RXQ_NUM);

	/* Set the doorbell to notify the device */
	btintel_pcie_set_rx_db(data, frbd_index);

	BT_DBG("[RXQ] rx sumbit completed");

	return 0;
}

static int btintel_pcie_start_rx(struct btintel_pcie_data *data)
{
	int i, ret;

	for (i = 0; i < RX_MAX_QUEUE; i++) {
		ret = btintel_pcie_submit_rx(data);
		if (ret) {
			BT_ERR("[RXQ] failed to submit frbd(%d)", ret);
			return ret;
		}
	}

	return 0;
}

static void btintel_pcie_reset_ia(struct btintel_pcie_data *data)
{
	memset(data->ia.tr_hia, 0, sizeof(u16) * NUM_QUEUES);
	memset(data->ia.tr_tia, 0, sizeof(u16) * NUM_QUEUES);
	memset(data->ia.cr_hia, 0, sizeof(u16) * NUM_QUEUES);
	memset(data->ia.cr_tia, 0, sizeof(u16) * NUM_QUEUES);
	BT_DBG("Index Arrays are reset");
}

static void btintel_pcie_reset_bt(struct btintel_pcie_data *data)
{
	BT_INFO("Reset BT Function ");
	btintel_pcie_wr_reg32(data, CSR_FUNC_CTRL_REG, CSR_FUNC_CTRL_SW_RESET);
}

/* This function enables BT function by setting CSR_FUNC_CTRL_MAC_INIT bit in
 * CSR_FUNC_CTRL_REG register and wait for MSI-X with MSIX_HW_INT_CAUSES_GP0.
 * Then the host reads firmware version from CSR_F2D_MBX and the boot stage
 * from CSR_BOOT_STAGE_REG.
 */
static int btintel_pcie_enable_bt(struct btintel_pcie_data *data)
{
	int err;
	u32 reg;

	data->gp0_received = false;

	/* Update the DMA address of CI struct to CSR */
	btintel_pcie_wr_reg32(data, CSR_CI_ADDR_LSB_REG,
			      data->ci_p_addr & 0xffffffff);
	btintel_pcie_wr_reg32(data, CSR_CI_ADDR_MSB_REG,
			      data->ci_p_addr >> 32);

	/* Reset the cached value of boot stage. it is updated by the msix
	 * gp0 interrupt handler.
	 */
	data->boot_stage_cache = 0x0;

	/* Set MAC_INIT bit to start primary bootloader */
	reg = btintel_pcie_rd_reg32(data, CSR_FUNC_CTRL_REG);
	BT_INFO("Before: FUNC_CTRL_REG: 0x%x", reg);

	btintel_pcie_set_reg_bits(data, CSR_FUNC_CTRL_REG,
				  CSR_FUNC_CTRL_MAC_INIT);
	BT_INFO("MAC_INIT is set");

	/* Wait until MAC_ACCESS is granted */
	err = btintel_pcie_poll_bit(data, CSR_FUNC_CTRL_REG,
				    CSR_FUNC_CTRL_MAC_ACCESS_STS,
				    CSR_FUNC_CTRL_MAC_ACCESS_STS,
				    DEFAULT_MAC_ACCESS_TIMEOUT);
	if (err < 0) {
		BT_ERR("Failed to start bootloader even after %u ns",
		       DEFAULT_MAC_ACCESS_TIMEOUT);
		return -ENODEV;
	}

	/* MAC is ready. Enable BT FUNC */
	btintel_pcie_set_reg_bits(data, CSR_FUNC_CTRL_REG,
				  CSR_FUNC_CTRL_FUNC_ENA |
				  CSR_FUNC_CTRL_FUNC_INIT);

	reg = btintel_pcie_rd_reg32(data, CSR_FUNC_CTRL_REG);
	BT_INFO("After: FUNC_CTRL_REG: 0x%x", reg);

	/* wait for interrupt from the device after booting up to primary
	 * bootloader.
	 */
	err = wait_event_timeout(data->gp0_wait_q, data->gp0_received,
				 msecs_to_jiffies(DEFAULT_INTR_TIMEOUT));
	if (!err) {
		BT_ERR("Failed to receive mac_init interrupt");
		return -ETIME;
	}

	/* Check cached boot stage is CSR_BOOT_STAGE_ROM(BIT(0)) */
	if (~data->boot_stage_cache & CSR_BOOT_STAGE_ROM) {
		BT_ERR("Device is not running in rom");
		return -ENODEV;
	}

	return 0;
}

/* This function handles the MSI-X interrupt for gp0 cause(bit 0 in
 * CSR_MSIX_HW_INT_CAUSES) which is sent for boot stage and image response.
 */
static void btintel_pcie_msix_gp0_handler(struct btintel_pcie_data *data)
{
	u32 reg;

	/* This interrupt is for three different causes and it is not easy to
	 * know what causes the interrupt. So, it compares each register value
	 * with cached value and update it before it wake up the queue.
	 */
	reg = btintel_pcie_rd_reg32(data, CSR_BOOT_STAGE_REG);
	if (reg != data->boot_stage_cache) {
		data->boot_stage_cache = reg;

		BT_DBG("Boot Stage updated: 0x%x", reg);
	}

	reg = btintel_pcie_rd_reg32(data, CSR_IMG_RESPONSE_REG);
	if (reg != data->img_resp_cache) {
		data->img_resp_cache = reg;

		BT_DBG("Image Response updated: 0x%x", reg);
	}

	BT_DBG("---------- cached GP0 registers ----------");
	BT_DBG("Cached Boot Stage Reg: 0x%x", data->boot_stage_cache);
	BT_DBG("Cached Image Resp Reg: 0x%x", data->img_resp_cache);

	data->gp0_received = true;

	/* If the boot stage is OP or IML, reset IA and start RX again */
	if (data->boot_stage_cache & CSR_BOOT_STAGE_OPFW ||
	    data->boot_stage_cache & CSR_BOOT_STAGE_IML) {
		btintel_pcie_reset_ia(data);
		btintel_pcie_start_rx(data);
	}

	wake_up(&data->gp0_wait_q);
}

/* This function handles the MSX-X interrupt for rx queue 0 which is for TX
 */
static void btintel_pcie_msix_tx_handle(struct btintel_pcie_data *data)
{
	u16 cr_tia, cr_hia;
	struct txq *txq;
	struct urbd0 *urbd0;

	cr_tia = data->ia.cr_tia[TXQ_NUM];
	cr_hia = data->ia.cr_hia[TXQ_NUM];

	BT_DBG("[TXQ] cr_hia=%u  cr_tia=%u", cr_hia, cr_tia);

	/* Check CR_TIA and CR_HIA for change */
	if (cr_tia == cr_hia) {
		BT_ERR("[TXQ] no new CD found");
		return;
	}

	txq = &data->txq;

	while (cr_tia != cr_hia) {
		BT_DBG("[TXQ] wake up tx_wait_q");

		data->tx_wait_done = true;
		wake_up(&data->tx_wait_q);

		/* Get URBD0 pointed by cr_tia */
		urbd0 = &txq->urbd0s[cr_tia];
		ipc_print_urbd0(urbd0, cr_tia);

		/* Make sure the completed TFD index is within the range */
		if (urbd0->tfd_index > txq->count) {
			BT_ERR("[TXQ] out of range: (0x%x)", urbd0->tfd_index);
			return;
		}

		/* Increase cr_tia */
		cr_tia = (cr_tia + 1) % txq->count;
		data->ia.cr_tia[TXQ_NUM] = cr_tia;
		ipc_print_ia_ring(&data->ia, TXQ_NUM);
	}
}

static int btintel_pcie_recv_event_intel(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	if (btintel_test_flag(hdev, INTEL_BOOTLOADER)) {
		struct hci_event_hdr *hdr = (void *)skb->data;

		if (skb->len > HCI_EVENT_HDR_SIZE && hdr->evt == 0xff &&
		    hdr->plen > 0) {
			const void *ptr = skb->data + HCI_EVENT_HDR_SIZE + 1;
			unsigned int len = skb->len - HCI_EVENT_HDR_SIZE - 1;

			switch (skb->data[2]) {
			case 0x02:
				/* When switching to the operational firmware
				 * the device sends a vendor specific event
				 * indicating that the bootup completed.
				 */
				btintel_bootup(hdev, ptr, len);
				break;
			case 0x06:
				/* When the firmware loading completes the
				 * device sends out a vendor specific event
				 * indicating the result of the firmware
				 * loading.
				 */
				btintel_secure_send_result(hdev, ptr, len);
				break;
			}
		}
	}

	return hci_recv_frame(hdev, skb);
}

/* Process the received rx data
 * It check the frame header to identify the data type and create skb
 * and calling HCI API
 */
static int btintel_pcie_hci_recv_frame(struct btintel_pcie_data *data,
				       void *buf, int count)
{
	struct hci_dev *hdev = data->hdev;
	int ret;
	u32 pkt_type;
	u16 plen;
	struct sk_buff *skb;

	spin_lock(&data->hci_rx_lock);

	/* The first 4 bytes indicates the Intel PCIe specific packet type.
	 * Read the packet type here before remove it.
	 */
	pkt_type = get_unaligned_le32(buf);
	bt_dev_dbg(hdev, "pkt_type=%u count=%d", pkt_type, count);

	buf += BTINTEL_PCIE_HCI_TYPE_LEN;
	count -= BTINTEL_PCIE_HCI_TYPE_LEN;

	hdev->stat.byte_rx += count;

	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb) {
		bt_dev_err(hdev, "Failed to allocate skb for event");
		ret = -ENOMEM;
		goto exit_error;
	}

	switch (pkt_type) {
	case BTINTEL_PCIE_HCI_ACL_PKT:
		hci_skb_pkt_type(skb) = HCI_ACLDATA_PKT;
		memcpy(skb_put(skb, HCI_ACL_HDR_SIZE), buf, HCI_ACL_HDR_SIZE);
		plen = hci_acl_hdr(skb)->dlen;
		buf += HCI_ACL_HDR_SIZE;
		break;
	case BTINTEL_PCIE_HCI_SCO_PKT:
		hci_skb_pkt_type(skb) = HCI_SCODATA_PKT;
		memcpy(skb_put(skb, HCI_SCO_HDR_SIZE), buf, HCI_SCO_HDR_SIZE);
		plen = hci_sco_hdr(skb)->dlen;
		buf += HCI_SCO_HDR_SIZE;
		break;
	case BTINTEL_PCIE_HCI_EVT_PKT:
		hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
		memcpy(skb_put(skb, HCI_EVENT_HDR_SIZE), buf,
		       HCI_EVENT_HDR_SIZE);
		plen = hci_event_hdr(skb)->plen;
		buf += HCI_EVENT_HDR_SIZE;
		break;
	default:
		ret = -EILSEQ;
		kfree_skb(skb);
		goto exit_error;
	}
	memcpy(skb_put(skb, plen), buf, plen);

	if (pkt_type == BTINTEL_PCIE_HCI_EVT_PKT)
		ret = btintel_pcie_recv_event_intel(hdev, skb);
	else
		ret = hci_recv_frame(hdev, skb);

exit_error:
	if (ret)
		hdev->stat.err_rx++;

	spin_unlock(&data->hci_rx_lock);

	return ret;
}

/* RX work queue */
static void btintel_pcie_rx_work(struct work_struct *work)
{
	struct btintel_pcie_data *data = container_of(work,
					struct btintel_pcie_data, rx_work);
	struct sk_buff *skb;
	int err;

	/* Process the sk_buf in queue and send to the hci layer */
	while ((skb = skb_dequeue(&data->rx_skb_q))) {
		err = btintel_pcie_hci_recv_frame(data, skb->data, skb->len);
		if (err) {
			BT_ERR("Failed to send received frame: %d", err);
			kfree_skb(skb);
		}
	}
}

/* create the sk_buff with data and save it to queue and start rx work
 */
static int btintel_pcie_submit_rx_work(struct btintel_pcie_data *data, u8 status,
				       void *buf)
{
	int ret, len;
	struct rfh_hdr *rfh_hdr;
	struct sk_buff *skb;

	rfh_hdr = (struct rfh_hdr *)buf;
	btintel_pcie_hci_dump("RFH HDR", buf, sizeof(*rfh_hdr));

	len = rfh_hdr->packet_len;

	/* Remove RFH header */
	buf += sizeof(*rfh_hdr);
	btintel_pcie_hci_dump("RX", buf, len);

	/* Create the sk_buf with packet in the buf and save it to sk_buf queue
	 */
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		ret = -ENOMEM;
		goto resubmit;
	}

	/* Copy the data to skb */
	memcpy(skb_put(skb, len), buf, len);

	/* Save the skb to rx queue */
	skb_queue_tail(&data->rx_skb_q, skb);

	/* Calling rx_work queue to process the skb */
	queue_work(data->workqueue, &data->rx_work);

resubmit:
	BT_DBG("submit next read request");

	/* submit read */
	ret = btintel_pcie_submit_rx(data);

	return ret;
}

/* This function handles the MSI-X interrupt for rx queue 1 which is for RX
 */
static void btintel_pcie_msix_rx_handle(struct btintel_pcie_data *data)
{
	u16 cr_hia, cr_tia;
	struct rxq *rxq;
	struct urbd1 *urbd1;
	struct frbd *frbd;
	struct data_buf *buf;
	int ret;

	cr_hia = data->ia.cr_hia[RXQ_NUM];
	cr_tia = data->ia.cr_tia[RXQ_NUM];

	BT_DBG("[RXQ] cr_hia=%u  cr_tia=%u", cr_hia, cr_tia);

	/* Check CR_TIA and CR_HIA for change */
	if (cr_tia == cr_hia) {
		BT_ERR("[RXQ] no new CD found");
		return;
	}

	rxq = &data->rxq;

	/* The firmware sends multiple CD in a single MSIX and it needs to
	 * process all received CDs in this interrupt.
	 */
	while (cr_tia != cr_hia) {
		/* Get URBD1 pointed by cr_tia */
		urbd1 = &rxq->urbd1s[cr_tia];
		ipc_print_urbd1(urbd1, cr_tia);

		/* Get FRBD poined by urbd1->frbd_tag */
		frbd = &rxq->frbds[urbd1->frbd_tag];

		/* Get buf from FRBD tag */
		buf = &rxq->bufs[urbd1->frbd_tag];
		if (!buf) {
			BT_ERR("[RXQ] failed to get the DMA buffer for %d",
			       urbd1->frbd_tag);
			return;
		}

		/* prepare RX work */
		ret = btintel_pcie_submit_rx_work(data, urbd1->status,
						  buf->data);
		if (ret) {
			BT_ERR("[RXQ] failed to submit rx request");
			return;
		}

		/* Update cr_tia */
		cr_tia = (cr_tia + 1) % rxq->count;
		data->ia.cr_tia[RXQ_NUM] = cr_tia;
		ipc_print_ia_ring(&data->ia, RXQ_NUM);
	}
	BT_DBG("[RXQ] completed rx interrupt");
}

static irqreturn_t btintel_pcie_msix_isr(int irq, void *data)
{
	return IRQ_WAKE_THREAD;
}

static irqreturn_t btintel_pcie_irq_msix_handler(int irq, void *dev_id)
{
	struct msix_entry *entry = dev_id;
	struct btintel_pcie_data *data = btintel_pcie_get_data(entry);
	u32 intr_fh, intr_hw;

	BT_DBG("handling msix(irq=%d dev_id=0x%p)", irq, dev_id);

	spin_lock(&data->irq_lock);
	intr_fh = btintel_pcie_rd_reg32(data, CSR_MSIX_FH_INT_CAUSES);
	intr_hw = btintel_pcie_rd_reg32(data, CSR_MSIX_HW_INT_CAUSES);

	/* Clear causes registers to avoid being handling the same cause */
	btintel_pcie_wr_reg32(data, CSR_MSIX_FH_INT_CAUSES, intr_fh);
	btintel_pcie_wr_reg32(data, CSR_MSIX_HW_INT_CAUSES, intr_hw);
	spin_unlock(&data->irq_lock);

	BT_DBG("intr_fh=0x%x intr_hw=0x%x", intr_fh, intr_hw);

	if (unlikely(!(intr_fh | intr_hw))) {
		BT_DBG("Ignore interrupt, inta == 0");
		return IRQ_NONE;
	}

	/* This interrupt is triggered by the firmware after updating
	 * boot_stage register and image_response register
	 */
	if (intr_hw & MSIX_HW_INT_CAUSES_GP0) {
		BT_DBG("intr for MSIX_HW_INT_CAUSES_GP0");
		btintel_pcie_msix_gp0_handler(data);
	}

	/* For TX */
	if (intr_fh & MSIX_FH_INT_CAUSES_0) {
		BT_DBG("intr for MSIX_FH_INT_CAUSES_0");
		btintel_pcie_msix_tx_handle(data);
	}

	/* For RX */
	if (intr_fh & MSIX_FH_INT_CAUSES_1) {
		BT_DBG("intr for MSIX_FH_INT_CAUSES_1");
		btintel_pcie_msix_rx_handle(data);
	}

	/* TODO: Add handler for other causes */
	/*
	 * Before sending the interrupt the HW disables it to prevent
	 * a nested interrupt. This is done by writing 1 to the corresponding
	 * bit in the mask register. After handling the interrupt, it should be
	 * re-enabled by clearing this bit. This register is defined as
	 * write 1 clear (W1C) register, meaning that it's being clear
	 * by writing 1 to the bit.
	 */
	btintel_pcie_wr_reg32(data, CSR_MSIX_AUTOMASK_ST, BIT(entry->entry));

	return IRQ_HANDLED;
}

/* This function requests the irq for msix and registers the handlers per irq.
 * Currently, it requests only 1 irq for all interrupt causes.
 */
static int btintel_pcie_setup_irq(struct btintel_pcie_data *data)
{
	int err;
	int num_irqs, i;

	BT_DBG("Initialize msix_entries...");
	for (i = 0; i < MSIX_VEC_MAX; i++) {
		data->msix_entries[i].entry = i;
		BT_DBG("msix_entries[%d] vector=0x%x entry=0x%x",
		       i, data->msix_entries[i].vector,
		       data->msix_entries[i].entry);
	}

	num_irqs = pci_enable_msix_range(data->pdev, data->msix_entries,
					 MSIX_VEC_MIN,
					 MSIX_VEC_MAX);
	if (num_irqs < 0) {
		BT_ERR("Failed to enable msix range (%d)", num_irqs);
		return num_irqs;
	}

	data->alloc_vecs = num_irqs;
	data->msix_enabled = 1;
	data->def_irq = 0;

	BT_DBG("Returned num_irqs=%d", num_irqs);
	for (i = 0; i < num_irqs; i++) {
		BT_DBG("msix_entries[%d] vector=0x%x entry=0x%x", i,
		       data->msix_entries[i].vector,
		       data->msix_entries[i].entry);
	}

	BT_DBG("setup irq handler");
	for (i = 0; i < data->alloc_vecs; i++) {
		struct msix_entry *msix_entry;

		msix_entry = &data->msix_entries[i];

		err = devm_request_threaded_irq(&data->pdev->dev,
						msix_entry->vector,
						btintel_pcie_msix_isr,
						btintel_pcie_irq_msix_handler,
						IRQF_SHARED,
						KBUILD_MODNAME,
						msix_entry);
		if (err) {
			BT_ERR("Failed to allocate irq handler (%d)", err);
			return err;
		}
	}

	return 0;
}

struct btintel_pcie_causes_list {
	u32 cause;
	u32 mask_reg;
	u8 cause_num;
};

struct btintel_pcie_causes_list causes_list[] = {
	{ MSIX_FH_INT_CAUSES_0,		CSR_MSIX_FH_INT_MASK,	0x00 },
	{ MSIX_FH_INT_CAUSES_1,		CSR_MSIX_FH_INT_MASK,	0x01 },
	{ MSIX_HW_INT_CAUSES_GP0,	CSR_MSIX_HW_INT_MASK,	0x20 },
};

/* This function configures the interrupt masks for both HW_INT_CAUSES and
 * FH_INT_CAUSES which are meaningful to us.
 *
 * After resetting BT function via PCIE FLR or FUNC_CTRL reset, the driver
 * need to call this function again to configure it again since the masks
 * are reset to 0xFFFFFFFF after reset.
 */
static void btintel_pcie_config_msix(struct btintel_pcie_data *data)
{
	int i;
	int val = data->def_irq | MSIX_NON_AUTO_CLEAR_CAUSE;

	/* Set Non Auto Clear Cause */
	for (i = 0; i < ARRAY_SIZE(causes_list); i++) {
		btintel_pcie_wr_reg8(data,
				     CSR_MSIX_IVAR(causes_list[i].cause_num),
				     val);
		btintel_pcie_clr_reg_bits(data,
					  causes_list[i].mask_reg,
					  causes_list[i].cause);
	}

	/* Save the initial interrupt mask */
	data->fh_init_mask = ~btintel_pcie_rd_reg32(data, CSR_MSIX_FH_INT_MASK);
	data->hw_init_mask = ~btintel_pcie_rd_reg32(data, CSR_MSIX_HW_INT_MASK);
	BT_DBG("init_mask: fh=0x%x hw=0x%x", data->fh_init_mask,
	       data->hw_init_mask);
}

static int btintel_pcie_config_pcie(struct pci_dev *pdev,
				    struct btintel_pcie_data *data)
{
	int err;

	err = pcim_enable_device(pdev);
	if (err) {
		BT_ERR("Failed to enable pci device (%d)", err);
		return err;
	}
	pci_set_master(pdev);

	/* Setup DMA mask */
	BT_DBG("Set DMA_MASK(64)");
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		BT_DBG("Set DMA_MASK(32)");
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		/* Both attempt failed */
		if (err) {
			BT_ERR("No suitable DMA available");
			return err;
		}
	}

	/* Get BAR to access CSR */
	err = pcim_iomap_regions(pdev, BIT(0), KBUILD_MODNAME);
	if (err) {
		BT_ERR("Failed to get iomap regions (%d)", err);
		return err;
	}

	data->base_addr = pcim_iomap_table(pdev)[0];
	if (!data->base_addr) {
		BT_ERR("Failed to get base address");
		return -ENODEV;
	}

	err = btintel_pcie_setup_irq(data);
	if (err) {
		BT_ERR("Failed to setup irq for msix");
		return err;
	}

	/* Configure MSI-X with causes list */
	btintel_pcie_config_msix(data);

	return 0;
}

static void btintel_pcie_init_ci(struct btintel_pcie_data *data,
				 struct ctx_info *ci)
{
	ci->version = 0x1;
	ci->size = sizeof(*ci);
	ci->config = 0x0000;
	ci->addr_cr_hia = data->ia.cr_hia_p_addr;
	ci->addr_tr_tia = data->ia.tr_tia_p_addr;
	ci->addr_cr_tia = data->ia.cr_tia_p_addr;
	ci->addr_tr_hia = data->ia.tr_hia_p_addr;
	ci->num_cr_ia = NUM_QUEUES;
	ci->num_tr_ia = NUM_QUEUES;
	ci->addr_urbdq0 = data->txq.urbd0s_p_addr;
	ci->addr_tfdq = data->txq.tfds_p_addr;
	ci->num_tfdq = data->txq.count;
	ci->num_urbdq0 = data->txq.count;
	ci->tfdq_db_vec = TXQ_NUM;
	ci->urbdq0_db_vec = TXQ_NUM;
	ci->rbd_size = RBD_SIZE_4K;
	ci->addr_frbdq = data->rxq.frbds_p_addr;
	ci->num_frbdq = data->rxq.count;
	ci->frbdq_db_vec = RXQ_NUM;
	ci->addr_urbdq1 = data->rxq.urbd1s_p_addr;
	ci->num_urbdq1 = data->rxq.count;
	ci->urbdq_db_vec = RXQ_NUM;
}

static void btintel_pcie_free_txq_bufs(struct btintel_pcie_data *data,
				       struct txq *txq)
{
	/* Free data buffers first */
	dma_free_coherent(&data->pdev->dev, txq->count * BUFFER_SIZE,
			  txq->buf_v_addr, txq->buf_p_addr);
	kfree(txq->bufs);
	BT_DBG("txq buffers are freed");
}

static int btintel_pcie_setup_txq_bufs(struct btintel_pcie_data *data,
				       struct txq *txq)
{
	int err = 0, i;
	struct data_buf *buf;

	if (txq->count == 0) {
		BT_ERR("invalid parameter: txq->count");
		err = -EINVAL;
		goto exit_error;
	}

	/* Allocate the same number of buffers as the descriptor */
	txq->bufs = kmalloc_array(txq->count, sizeof(*buf), GFP_KERNEL);
	if (!txq->bufs) {
		err = -ENOMEM;
		goto exit_error;
	}

	/* Allocate full chunk of data buffer for DMA first and do indexing and
	 * initialization next, so it can be freed easily
	 */
	txq->buf_v_addr = dma_alloc_coherent(&data->pdev->dev,
					     txq->count * BUFFER_SIZE,
					     &txq->buf_p_addr,
					     GFP_KERNEL | __GFP_NOWARN);
	if (!txq->buf_v_addr) {
		BT_ERR("Failed to allocate DMA buf");
		err = -ENOMEM;
		kfree(txq->bufs);
		goto exit_error;
	}
	memset(txq->buf_v_addr, 0, txq->count * BUFFER_SIZE);

	BT_DBG("alloc bufs: p=0x%llx v=0x%p", txq->buf_p_addr, txq->buf_v_addr);

	/* Setup the allocated DMA buffer to bufs. Each data_buf should
	 * have virtual address and physical address
	 */
	for (i = 0; i < txq->count; i++) {
		buf = &txq->bufs[i];
		buf->data_p_addr = txq->buf_p_addr + (i * BUFFER_SIZE);
		buf->data = txq->buf_v_addr + (i * BUFFER_SIZE);
	}

exit_error:
	return err;
}

static void btintel_pcie_free_rxq_bufs(struct btintel_pcie_data *data,
				       struct rxq *rxq)
{
	/* Free data buffers first */
	dma_free_coherent(&data->pdev->dev, rxq->count * BUFFER_SIZE,
			  rxq->buf_v_addr, rxq->buf_p_addr);
	kfree(rxq->bufs);
	BT_DBG("rxq buffers are freed");
}

static int btintel_pcie_setup_rxq_bufs(struct btintel_pcie_data *data,
				       struct rxq *rxq)
{
	int err = 0, i;
	struct data_buf *buf;

	if (rxq->count == 0) {
		BT_ERR("invalid parameter: rxq->count");
		err = -EINVAL;
		goto exit_error;
	}

	/* Allocate the same number of buffers as the descriptor */
	rxq->bufs = kmalloc_array(rxq->count, sizeof(*buf), GFP_KERNEL);
	if (!rxq->bufs) {
		err = -ENOMEM;
		goto exit_error;
	}

	/* Allocate full chunk of data buffer for DMA first and do indexing and
	 * initialization next, so it can be freed easily
	 */
	rxq->buf_v_addr = dma_alloc_coherent(&data->pdev->dev,
					     rxq->count * BUFFER_SIZE,
					     &rxq->buf_p_addr,
					     GFP_KERNEL | __GFP_NOWARN);
	if (!rxq->buf_v_addr) {
		BT_ERR("Failed to allocate DMA buf");
		err = -ENOMEM;
		kfree(rxq->bufs);
		goto exit_error;
	}
	memset(rxq->buf_v_addr, 0, rxq->count * BUFFER_SIZE);

	BT_DBG("alloc bufs: p=0x%llx v=0x%p", rxq->buf_p_addr, rxq->buf_v_addr);

	/* Setup the allocated DMA buffer to bufs. Each data_buf should
	 * have virtual address and physical address
	 */
	for (i = 0; i < rxq->count; i++) {
		buf = &rxq->bufs[i];
		buf->data_p_addr = rxq->buf_p_addr + (i * BUFFER_SIZE);
		buf->data = rxq->buf_v_addr + (i * BUFFER_SIZE);
	}

exit_error:

	return err;
}

static void btintel_pcie_setup_ia(struct btintel_pcie_data *data,
				  dma_addr_t p_addr, void *v_addr,
				  struct ia *ia)
{
	/* TR Head Index Array */
	ia->tr_hia_p_addr = p_addr;
	ia->tr_hia = v_addr;

	/* TR Tail Index Array */
	ia->tr_tia_p_addr = p_addr + sizeof(u16) * NUM_QUEUES;
	ia->tr_tia = v_addr + sizeof(u16) * NUM_QUEUES;

	/* CR Head index Array */
	ia->cr_hia_p_addr = p_addr + (sizeof(u16) * NUM_QUEUES * 2);
	ia->cr_hia = v_addr + (sizeof(u16) * NUM_QUEUES * 2);

	/* CR Tail Index Array */
	ia->cr_tia_p_addr = p_addr + (sizeof(u16) * NUM_QUEUES * 3);
	ia->cr_tia = v_addr + (sizeof(u16) * NUM_QUEUES * 3);
}

static void btintel_pcie_free(struct btintel_pcie_data *data)
{
	btintel_pcie_free_rxq_bufs(data, &data->rxq);
	btintel_pcie_free_txq_bufs(data, &data->txq);

	dma_pool_free(data->dma_pool, data->dma_v_addr, data->dma_p_addr);
	dma_pool_destroy(data->dma_pool);
	BT_DBG("DMA memory is freed");
}

/* Allocate tx and rx queues, any related data structures and buffers.
 */
static int btintel_pcie_alloc(struct btintel_pcie_data *data)
{
	int err = 0;
	size_t total;
	dma_addr_t p_addr;
	void *v_addr;

	/* Allocate the chunk of DMA memory for descriptors, index array, and
	 * context information, instead of allocating individually.
	 * The DMA memory for data buffer is allocated while setting up the
	 * each queue.
	 *
	 * Total size is sum of the following
	 *  + size of TFD * Number of descriptors in queue
	 *  + size of URBD0 * Number of descriptors in queue
	 *  + size of FRBD * Number of descriptors in queue
	 *  + size of URBD1 * Number of descriptors in queue
	 *  + size of index * Number of queues(2) * type of index array(4)
	 *  + size of context information
	 */
	total = (sizeof(struct tfd) + sizeof(struct urbd0) + sizeof(struct frbd)
		+ sizeof(struct urbd1)) * DESCS_COUNT;

	/* Add the sum of size of index array and size of ci struct */
	total += (sizeof(u16) * NUM_QUEUES * 4) + sizeof(struct ctx_info);

	/* Allocate DMA Pool */
	data->dma_pool = dma_pool_create(KBUILD_MODNAME, &data->pdev->dev,
					 total, DMA_POOL_ALIGNMENT, 0);
	if (!data->dma_pool) {
		BT_ERR("Failed to allocate dma pool for queues");
		err = -ENOMEM;
		goto exit_error;
	}

	v_addr = dma_pool_zalloc(data->dma_pool, GFP_KERNEL | __GFP_NOWARN,
				 &p_addr);
	if (!v_addr) {
		BT_ERR("Failed to alloc dma memory for queues");
		dma_pool_destroy(data->dma_pool);
		err = -ENOMEM;
		goto exit_error;
	}

	data->dma_p_addr = p_addr;
	data->dma_v_addr = v_addr;

	BT_DBG("dma pool: p_addr=0x%llx v_addr=0x%p", p_addr, v_addr);

	/* Setup descriptor count */
	data->txq.count = DESCS_COUNT;
	data->rxq.count = DESCS_COUNT;

	/* Setup tfds */
	data->txq.tfds_p_addr = p_addr;
	data->txq.tfds = v_addr;

	p_addr += (sizeof(struct tfd) * DESCS_COUNT);
	v_addr += (sizeof(struct tfd) * DESCS_COUNT);

	/* Setup urbd0 */
	data->txq.urbd0s_p_addr = p_addr;
	data->txq.urbd0s = v_addr;

	p_addr += (sizeof(struct urbd0) * DESCS_COUNT);
	v_addr += (sizeof(struct urbd0) * DESCS_COUNT);

	/* Setup frbd */
	data->rxq.frbds_p_addr = p_addr;
	data->rxq.frbds = v_addr;

	p_addr += (sizeof(struct frbd) * DESCS_COUNT);
	v_addr += (sizeof(struct frbd) * DESCS_COUNT);

	/* Setup urbd1 */
	data->rxq.urbd1s_p_addr = p_addr;
	data->rxq.urbd1s = v_addr;

	p_addr += (sizeof(struct urbd1) * DESCS_COUNT);
	v_addr += (sizeof(struct urbd1) * DESCS_COUNT);

	/* Setup data buffers for txq */
	err = btintel_pcie_setup_txq_bufs(data, &data->txq);
	if (err) {
		BT_ERR("Failed to setup txq buffers: %d", err);
		goto exit_error_pool;
	}

	/* Setup data buffers for rxq */
	err = btintel_pcie_setup_rxq_bufs(data, &data->rxq);
	if (err) {
		BT_ERR("Failed to allocate rxq buffers: %d", err);
		goto exit_error_txq;
	}

	/* Setup Index Array */
	btintel_pcie_setup_ia(data, p_addr, v_addr, &data->ia);

	/* Setup Context Information */
	p_addr += sizeof(u16) * NUM_QUEUES * 4;
	v_addr += sizeof(u16) * NUM_QUEUES * 4;

	data->ci = v_addr;
	data->ci_p_addr = p_addr;

	/* Initialize the CI */
	btintel_pcie_init_ci(data, data->ci);

	return 0;

exit_error_txq:
	btintel_pcie_free_txq_bufs(data, &data->txq);
exit_error_pool:
	dma_pool_free(data->dma_pool, data->dma_v_addr, data->dma_p_addr);
	dma_pool_destroy(data->dma_pool);
exit_error:
	return err;
}

static void btintel_pcie_release_hdev(struct btintel_pcie_data *data)
{
	struct hci_dev *hdev;

	hdev = data->hdev;
	if (hdev) {
		hci_unregister_dev(hdev);
		hci_free_dev(hdev);
	}
	data->hdev = NULL;
}

static int btintel_pcie_setup_hdev(struct btintel_pcie_data *data)
{
	/* TODO: initialize hdev and assign the callbacks to hdev */
	return -ENODEV;
}

static int btintel_pcie_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	int err;
	struct btintel_pcie_data *data;

	if (!pdev)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* initialize the btintel_pcie data struct */
	data->pdev = pdev;

	spin_lock_init(&data->irq_lock);
	spin_lock_init(&data->hci_rx_lock);

	init_waitqueue_head(&data->gp0_wait_q);
	data->gp0_received = false;

	init_waitqueue_head(&data->tx_wait_q);
	data->tx_wait_done = false;

	data->workqueue = alloc_ordered_workqueue(KBUILD_MODNAME, WQ_HIGHPRI);
	if (!data->workqueue) {
		BT_ERR("Failed to create workqueue");
		return -ENOMEM;
	}
	skb_queue_head_init(&data->rx_skb_q);
	INIT_WORK(&data->rx_work, btintel_pcie_rx_work);

	data->boot_stage_cache = 0x00;
	data->img_resp_cache = 0x00;

	/* PCIe specific all to configure it for this device includes
	 * enabling pice device, setting master, reading BAR[0], configuring
	 * MSIx, setting DMA mask, and save the driver data.
	 */
	err = btintel_pcie_config_pcie(pdev, data);
	if (err) {
		BT_ERR("Failed to config pcie (%d)", err);
		goto exit_error;
	}

	/* Set driver data for this PCI device */
	pci_set_drvdata(pdev, data);

	/* allocate the IPC struct */
	err = btintel_pcie_alloc(data);
	if (err) {
		BT_ERR("Failed to allocate queues(%d)", err);
		goto exit_error;
	}

	/* Enable BT function */
	err = btintel_pcie_enable_bt(data);
	if (err) {
		BT_ERR("Failed to start bluetooth device(%d)", err);
		goto exit_error;
	}

	/* CNV information (CNVi and CNVr) is in CSR */
	data->cnvi = btintel_pcie_rd_reg32(data, CSR_HW_REV_REG);
	BT_DBG("cnvi:   0x%08x", data->cnvi);

	data->cnvr = btintel_pcie_rd_reg32(data, CSR_RF_ID_REG);
	BT_DBG("cnvr:   0x%08x", data->cnvr);

	err = btintel_pcie_start_rx(data);
	if (err) {
		BT_ERR("Failed to start rx (%d)", err);
		goto exit_error;
	}

	err = btintel_pcie_setup_hdev(data);
	if (err) {
		BT_ERR("Failed to setup HCI module");
		goto exit_error;
	}

	return 0;

exit_error:
	/* reset device before leave */
	btintel_pcie_reset_bt(data);

	/* clear bus mastering */
	pci_clear_master(pdev);

	/* Unset driver data for PCI device */
	pci_set_drvdata(pdev, NULL);

	return err;
}

static void btintel_pcie_remove(struct pci_dev *pdev)
{
	struct btintel_pcie_data *data;

	if (!pdev) {
		BT_ERR("Invalid parameter: pdev");
		return;
	}

	data = pci_get_drvdata(pdev);
	if (!data) {
		BT_ERR("data is empty");
		return;
	}

	btintel_pcie_release_hdev(data);

	flush_work(&data->rx_work);

	destroy_workqueue(data->workqueue);

	btintel_pcie_free(data);

	/* reset device before leave */
	btintel_pcie_reset_bt(data);

	/* clear bus mastering */
	pci_clear_master(pdev);

	/* Unset driver data for PCI device */
	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
static int btintel_pcie_suspend(struct device *dev)
{
	/* TODO: Add support suspend */
	return 0;
}

static int btintel_pcie_resume(struct device *dev)
{
	/* TODO: Add support resume */
	return 0;
}

static SIMPLE_DEV_PM_OPS(btintel_pcie_pm_ops, btintel_pcie_suspend,
							btintel_pcie_resume);
#endif /* CONFIG_PM */

static struct pci_driver btintel_pcie_driver = {
	.name = KBUILD_MODNAME,
	.id_table = btintel_pcie_table,
	.probe = btintel_pcie_probe,
	.remove = btintel_pcie_remove,
#ifdef CONFIG_PM
	.driver.pm = &btintel_pcie_pm_ops,
#endif /* CONFIG_PM */
};
module_pci_driver(btintel_pcie_driver);

MODULE_AUTHOR("Tedd Ho-Jeong An <tedd.an@intel.com>");
MODULE_DESCRIPTION("Intel Bluetooth PCIe transport driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
