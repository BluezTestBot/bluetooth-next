// SPDX-License-Identifier: GPL-2.0-or-later
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

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"
#include "hci_nxp.h"

/** NXP Power Save Feature **/
struct BT_CMD {
	u16	ocf_ogf;
	u8	length;
	u8	data[4];
} __packed;

/* Power Save feature will be enabled and wakeup method will be break signal by default */
int wakeupmode = WAKEUP_METHOD_BREAK;
int ps_mode = PS_MODE_ENABLE;

static int is_device_ready(struct hci_uart *hu)
{
	struct hci_dev *hdev = NULL;

	if (!hu) {
		BT_ERR("hu is NULL");
		return -ENODEV;
	}
	if (!hu->proto || !hu->hdev || !hu->tty) {
		BT_ERR("Device not ready! proto=%p, hdev=%p, tty=%p", hu->proto, hu->hdev, hu->tty);
		return -ENODEV;
	}
	hdev = hu->hdev;
	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		BT_ERR("HCI_RUNNING is not set");
		return -EBUSY;
	}
	return 0;
}

/* Builds and sends an PS command packet */
static int send_ps_cmd(u8 cmd, struct hci_uart *hu)
{
	int err = 0;
	struct sk_buff *skb = NULL;
	struct BT_CMD *pcmd;

	/* allocate packet */
	skb = bt_skb_alloc(sizeof(struct BT_CMD), GFP_ATOMIC);
	if (!skb) {
		BT_ERR("cannot allocate memory for HCILL packet");
		err = -ENOMEM;
		goto out;
	}

	pcmd = (struct BT_CMD *)skb->data;
	pcmd->ocf_ogf =  (OGF << 10) | BT_CMD_AUTO_SLEEP_MODE;
	pcmd->length = 1;
	if (cmd ==  PS_MODE_ENABLE)
		pcmd->data[0] = BT_PS_ENABLE;
	else
		pcmd->data[0] = BT_PS_DISABLE;

	bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;
	skb_put(skb, sizeof(struct BT_CMD) - 4 + pcmd->length);
	skb->dev = (void *)hu->hdev;

	/* send packet */
	hu->proto->enqueue(hu, skb);
	hci_uart_tx_wakeup(hu);

out:
	return err;
}

/* Builds and sends an wake up method command packet */
static int send_wakeup_method_cmd(u8 cmd, struct hci_uart *hu)
{
	int err = 0;
	struct sk_buff *skb = NULL;
	struct BT_CMD *pcmd;

	/* allocate packet */
	skb = bt_skb_alloc(sizeof(struct BT_CMD), GFP_ATOMIC);
	if (!skb) {
		BT_ERR("cannot allocate memory for HCILL packet");
		err = -ENOMEM;
		goto out;
	}

	pcmd = (struct BT_CMD *)skb->data;
	pcmd->ocf_ogf =  (OGF << 10) | BT_CMD_WAKEUP_METHOD;
	pcmd->length = 4;
	pcmd->data[0] = BT_HOST_WAKEUP_METHOD_NONE;
	pcmd->data[1] = BT_HOST_WAKEUP_DEFAULT_GPIO;
	switch (cmd) {
	case WAKEUP_METHOD_DTR:
		pcmd->data[2] = BT_CTRL_WAKEUP_METHOD_DSR;
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		pcmd->data[2] = BT_CTRL_WAKEUP_METHOD_BREAK;
		break;
	}
	pcmd->data[3] = BT_CTRL_WAKEUP_DEFAULT_GPIO;

	bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;

	skb_put(skb, sizeof(struct BT_CMD) - 4 + pcmd->length);
	skb->dev = (void *)hu->hdev;

	/* send packet */
	hu->proto->enqueue(hu, skb);
	hci_uart_tx_wakeup(hu);

out:
	return err;
}

void ps_timeout_func(struct timer_list *t)
{
	struct ps_data *data = from_timer(data, t, ps_timer);
	struct tty_struct *tty = data->tty;
	struct hci_uart *hu = NULL;

	data->timer_on = 0;
	if (!data->tty)
		return;

	hu = (struct hci_uart *)tty->disc_data;

	if (!hu)
		return;

	if (test_bit(HCI_UART_SENDING, &hu->tx_state)) {
		ps_start_timer(hu);
	} else {
		data->ps_cmd = PS_CMD_ENTER_PS;
		schedule_work(&data->work);
	}
}

static void set_dtr(struct tty_struct *tty, int on_off)
{
	u32 old_state = 0;
	u32 new_state = 0;

	if (TTY_FUNC->tiocmget) {
		old_state = TTY_FUNC->tiocmget(tty);
		if (on_off)
			new_state = old_state | TIOCM_DTR;
		else
			new_state = old_state & ~TIOCM_DTR;
		if (new_state == old_state)
			return;
		if (TTY_FUNC->tiocmset) {
			if (on_off)
				TTY_FUNC->tiocmset(tty, TIOCM_DTR, 0);  /* DTR ON */
			else
				TTY_FUNC->tiocmset(tty, 0, TIOCM_DTR);  /* DTR OFF */
		}
	}
}

static void set_break(struct tty_struct *tty, int on_off)
{
	if (TTY_FUNC->break_ctl) {
		if (on_off)
			TTY_FUNC->break_ctl(tty, -1); /* turn on break */
		else
			TTY_FUNC->break_ctl(tty, 0);  /* turn off break */
	}
}

static int get_cts(struct tty_struct *tty)
{
	u32 state = 0;
	int ret = -1;

	if (TTY_FUNC->tiocmget) {
		state = TTY_FUNC->tiocmget(tty);
		if (state & TIOCM_CTS)
			ret = 1;   /* CTS LOW */
		else
			ret = 0;   /* CTS HIGH */
	}
	return ret;
}

static void set_rts(struct tty_struct *tty, int on_off)
{
	u32 old_state = 0;
	u32 new_state = 0;

	if (TTY_FUNC->tiocmget) {
		old_state = TTY_FUNC->tiocmget(tty);
		if (on_off)
			new_state = old_state | TIOCM_RTS;
		else
			new_state = old_state & ~TIOCM_RTS;
		if (new_state == old_state)
			return;
		if (TTY_FUNC->tiocmset) {
			if (on_off)
				TTY_FUNC->tiocmset(tty, TIOCM_RTS, 0);    /* RTS ON */
			else
				TTY_FUNC->tiocmset(tty, 0, TIOCM_RTS);    /* RTS OFF */
		}
	}
}

static void ps_control(struct ps_data *data, u8 ps_state)
{
	struct hci_uart *hu = NULL;

	if (data->ps_state == ps_state)
		return;
	if (data->tty) {
		switch (data->cur_wakeupmode) {
		case WAKEUP_METHOD_DTR:
			if (ps_state == PS_STATE_AWAKE)
				set_dtr(data->tty, 1);  /* DTR ON */
			else
				set_dtr(data->tty, 0);  /* DTR OFF */
			data->ps_state = ps_state;
			break;
		case WAKEUP_METHOD_BREAK:
			if (ps_state == PS_STATE_AWAKE)
				set_break(data->tty, 0); /* break OFF */
			else
				set_break(data->tty, 1); /* break ON */
			data->ps_state = ps_state;
			break;
		default:
			break;
		}
		if (ps_state == PS_STATE_AWAKE) {
			hu = (struct hci_uart *)data->tty->disc_data;
			if (hu)
				hci_uart_tx_wakeup(hu);
		}
	}
}

static void ps_work_func(struct work_struct *work)
{
	struct ps_data *data = container_of(work, struct ps_data, work);

	if (data->tty) {
		if (data->ps_cmd == PS_CMD_ENTER_PS && data->cur_psmode == PS_MODE_ENABLE)
			ps_control(data, PS_STATE_SLEEP);
		else  if (data->ps_cmd == PS_CMD_EXIT_PS)
			ps_control(data, PS_STATE_AWAKE);
	}
}

int ps_init_work(struct hci_uart *hu)
{
	struct ps_data *psdata = kzalloc(sizeof(*psdata), GFP_KERNEL);
	struct nxp_struct *nxp = hu->priv;

	if (!psdata) {
		BT_ERR("Can't allocate control structure");
		return -ENFILE;
	}
	nxp->psdata = psdata;

	memset(psdata, 0, sizeof(*psdata));
	psdata->interval = DEFAULT_TIME_PERIOD;
	psdata->timer_on    = 0;
	psdata->tty = NULL;
	psdata->ps_state = PS_STATE_AWAKE;
	psdata->ps_mode = ps_mode;
	psdata->ps_cmd = 0;
	psdata->send_cmd = 0;
	switch (wakeupmode) {
	case WAKEUP_METHOD_DTR:
		psdata->wakeupmode =  WAKEUP_METHOD_DTR;
		break;
	case  WAKEUP_METHOD_BREAK:
	default:
		psdata->wakeupmode =  WAKEUP_METHOD_BREAK;
		break;
	}

	psdata->cur_psmode = PS_MODE_DISABLE;
	psdata->cur_wakeupmode = WAKEUP_METHOD_INVALID;

	INIT_WORK(&psdata->work, ps_work_func);
	return 0;
}

void ps_init_timer(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;
	struct ps_data *psdata = nxp->psdata;

	psdata->timer_on = 0;
	psdata->tty = hu->tty;
	timer_setup(&psdata->ps_timer, ps_timeout_func, 0);
}

void ps_start_timer(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;
	struct ps_data *psdata = nxp->psdata;

	if (psdata->cur_psmode ==  PS_MODE_ENABLE) {
		psdata->timer_on = 1;
		mod_timer(&psdata->ps_timer, jiffies + (psdata->interval * HZ) / 1000);
	}
}

void ps_cancel_timer(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;
	struct ps_data *psdata = nxp->psdata;

	if (psdata) {
		flush_scheduled_work();
	if (psdata->timer_on)
		del_timer(&psdata->ps_timer);
	if (psdata->cur_psmode ==  PS_MODE_ENABLE &&
	    psdata->cur_wakeupmode ==  WAKEUP_METHOD_BREAK) {
		/* set_break off */
		set_break(psdata->tty, 0);
	}
	psdata->tty = NULL;
	kfree(psdata);
	}
}

int ps_wakeup(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;
	struct ps_data *psdata = nxp->psdata;
	int ret = 1;

	if (psdata->ps_state == PS_STATE_AWAKE)
		ret = 0;
	psdata->ps_cmd = PS_CMD_EXIT_PS;
	schedule_work(&psdata->work);
	return ret;
}

void ps_init(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;
	struct ps_data *psdata = nxp->psdata;
	int mode = 0;
	struct ktermios old_termios;

	if (!psdata || !psdata->tty)
		return;
	if (get_cts(psdata->tty) != 1) {
		/* firmware is sleeping */
		mode = psdata->cur_wakeupmode;
		if (mode == WAKEUP_METHOD_INVALID)
			mode =  wakeupmode;
		switch (mode) {
		case WAKEUP_METHOD_BREAK:
			/* set RTS */
			set_rts(psdata->tty, 1);
			/* break on */
			set_break(psdata->tty, 1);
			/* break off */
			set_break(psdata->tty, 0);
			mdelay(5);
			break;
		case WAKEUP_METHOD_DTR:
			/* set RTS */
			set_rts(psdata->tty, 1);
			set_dtr(psdata->tty, 0);
			set_dtr(psdata->tty, 1);
			mdelay(5);
			break;
		default:
			break;
		}
		old_termios = psdata->tty->termios;
		psdata->tty->termios.c_cflag &= ~CRTSCTS;  /* Clear the flow control */
		psdata->TTY_FUNC->set_termios(psdata->tty, &old_termios);
		old_termios = psdata->tty->termios;
		psdata->tty->termios.c_cflag |= CRTSCTS;  /* Enable the flow control */
		psdata->TTY_FUNC->set_termios(psdata->tty, &old_termios);
	}

	psdata->send_cmd = 0;
	if (!is_device_ready(hu)) {
		if (psdata->cur_wakeupmode != psdata->wakeupmode) {
			psdata->send_cmd |= SEND_WAKEUP_METHOD_CMD;
			send_wakeup_method_cmd(psdata->wakeupmode, hu);
		}
		if (psdata->cur_psmode != psdata->ps_mode) {
			psdata->send_cmd |= SEND_AUTO_SLEEP_MODE_CMD;
			send_ps_cmd(psdata->ps_mode, hu);
		}
	}
}

void ps_check_event_packet(struct hci_uart *hu, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *)skb->data;
	struct hci_ev_cmd_complete *ev = NULL;
	u8 event = hdr->evt;
	u16 opcode;
	u8 status = 0;
	struct nxp_struct *nxp = hu->priv;
	struct ps_data *psdata = nxp->psdata;

	if (event == HCI_EV_CMD_COMPLETE) {
		ev = (void *)(skb->data + sizeof(struct hci_event_hdr));
		opcode = __le16_to_cpu(ev->opcode);
		switch (opcode) {
		case HCI_OP_AUTO_SLEEP_MODE:
			status = *((u8 *)ev + sizeof(struct hci_ev_cmd_complete));
			/* if hci_nxp has sent the command internally */
			if (psdata->send_cmd) {
				if (!status)
					psdata->cur_psmode = psdata->ps_mode;
				else
					psdata->ps_mode = psdata->cur_psmode;
				psdata->send_cmd &= ~SEND_AUTO_SLEEP_MODE_CMD;
			} else {
				if (!status)
					psdata->cur_psmode = psdata->ps_mode;
			}
			if (psdata->cur_psmode == PS_MODE_ENABLE)
				ps_start_timer(hu);
			else
				ps_wakeup(hu);
			BT_INFO("Power Save mode response: status=%d, ps_mode=%d",
				status, psdata->cur_psmode);
			break;
		case HCI_OP_WAKEUP_METHOD:
			status = *((u8 *)ev + sizeof(struct hci_ev_cmd_complete));
			/* if hci_nxp has sent the command internally */
			if (psdata->send_cmd) {
				psdata->send_cmd &= ~SEND_WAKEUP_METHOD_CMD;
				if (!status)
					psdata->cur_wakeupmode = psdata->wakeupmode;
				else
					psdata->wakeupmode = psdata->cur_wakeupmode;
			} else {
				psdata->cur_wakeupmode = ev->ncmd;
			}
			BT_INFO("Set Wakeup Method response: status=%d, wakeupmode=%d",
				status, psdata->cur_wakeupmode);
			break;
		default:
			break;
		}
	}
}

/** NXP proto **/

/* Initialize protocol */
static int nxp_open(struct hci_uart *hu)
{
	struct nxp_struct *nxp;

	BT_DBG("hu %p", hu);

	nxp = kzalloc(sizeof(*nxp), GFP_KERNEL);
	if (!nxp)
		return -ENOMEM;

	skb_queue_head_init(&nxp->txq);

	hu->priv = nxp;

	if (ps_init_work(hu) == 0)
		ps_init_timer(hu);

	return 0;
}

/* Flush protocol data */
static int nxp_flush(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&nxp->txq);

	return 0;
}

/* Close protocol */
static int nxp_close(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;

	BT_DBG("hu %p", hu);

	ps_cancel_timer(hu);

	skb_queue_purge(&nxp->txq);

	kfree_skb(nxp->rx_skb);

	hu->priv = NULL;
	kfree(nxp);

	return 0;
}

/* Enqueue frame for transmission (padding, crc, etc) */
static int nxp_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct nxp_struct *nxp = hu->priv;
	struct ps_data *psdata = nxp->psdata;
	struct BT_CMD *pcmd = (struct BT_CMD *)skb->data;

	BT_DBG("hu %p skb %p", hu, skb);

	if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT) {
		if (pcmd->ocf_ogf == ((OGF << 10) | BT_CMD_AUTO_SLEEP_MODE)) {
			if (pcmd->data[0] == BT_PS_ENABLE)
				psdata->ps_mode = PS_MODE_ENABLE;
			else if (pcmd->data[0] == BT_PS_DISABLE)
				psdata->ps_mode = PS_MODE_DISABLE;
		}
	}

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	skb_queue_tail(&nxp->txq, skb);

	return 0;
}

static const struct h4_recv_pkt nxp_recv_pkts[] = {
	{ H4_RECV_ACL,   .recv = hci_recv_frame },
	{ H4_RECV_SCO,   .recv = hci_recv_frame },
	{ H4_RECV_EVENT, .recv = hci_recv_frame },
	{ H4_RECV_ISO,   .recv = hci_recv_frame },
};

static struct sk_buff *nxp_recv_buf(struct hci_dev *hdev, struct sk_buff *skb,
				    const unsigned char *buffer, int count,
						const struct h4_recv_pkt *pkts,
						int pkts_count)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	u8 alignment = hu->alignment ? hu->alignment : 1;

	/* Check for error from previous call */
	if (IS_ERR(skb))
		skb = NULL;

	while (count) {
		int i, len;

		/* remove padding bytes from buffer */
		for (; hu->padding && count > 0; hu->padding--) {
			count--;
			buffer++;
		}
		if (!count)
			break;

		if (!skb) {
			for (i = 0; i < pkts_count; i++) {
				if (buffer[0] != (&pkts[i])->type)
					continue;

				skb = bt_skb_alloc((&pkts[i])->maxlen,
						   GFP_ATOMIC);
				if (!skb)
					return ERR_PTR(-ENOMEM);

				hci_skb_pkt_type(skb) = (&pkts[i])->type;
				hci_skb_expect(skb) = (&pkts[i])->hlen;
				break;
			}

			/* Check for invalid packet type */
			if (!skb)
				return ERR_PTR(-EILSEQ);

			count -= 1;
			buffer += 1;
		}

		len = min_t(uint, hci_skb_expect(skb) - skb->len, count);
		skb_put_data(skb, buffer, len);

		count -= len;
		buffer += len;

		/* Check for partial packet */
		if (skb->len < hci_skb_expect(skb))
			continue;

		for (i = 0; i < pkts_count; i++) {
			if (hci_skb_pkt_type(skb) == (&pkts[i])->type)
				break;
		}

		if (i >= pkts_count) {
			kfree_skb(skb);
			return ERR_PTR(-EILSEQ);
		}

		if (skb->len == (&pkts[i])->hlen) {
			u16 dlen;

			switch ((&pkts[i])->lsize) {
			case 0:
				/* No variable data length */
				dlen = 0;
				break;
			case 1:
				/* Single octet variable length */
				dlen = skb->data[(&pkts[i])->loff];
				hci_skb_expect(skb) += dlen;

				if (skb_tailroom(skb) < dlen) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
				break;
			case 2:
				/* Double octet variable length */
				dlen = get_unaligned_le16(skb->data +
							  (&pkts[i])->loff);
				hci_skb_expect(skb) += dlen;

				if (skb_tailroom(skb) < dlen) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
				break;
			default:
				/* Unsupported variable length */
				kfree_skb(skb);
				return ERR_PTR(-EILSEQ);
			}

			if (!dlen) {
				hu->padding = (skb->len - 1) % alignment;
				hu->padding = (alignment - hu->padding) % alignment;

				/* No more data, complete frame */
				if (hci_skb_pkt_type(skb) == HCI_EVENT_PKT)
					ps_check_event_packet(hu, skb);
				(&pkts[i])->recv(hdev, skb);
				skb = NULL;
			}
		} else {
			hu->padding = (skb->len - 1) % alignment;
			hu->padding = (alignment - hu->padding) % alignment;

			/* Complete frame */
			if (hci_skb_pkt_type(skb) == HCI_EVENT_PKT)
				ps_check_event_packet(hu, skb);
			(&pkts[i])->recv(hdev, skb);
			skb = NULL;
		}
	}

	return skb;
}

static int nxp_recv(struct hci_uart *hu, const void *data, int count)
{
	struct nxp_struct *nxp = hu->priv;

	if (!test_bit(HCI_UART_REGISTERED, &hu->flags))
		return -EUNATCH;

	ps_start_timer(hu);

	nxp->rx_skb = nxp_recv_buf(hu->hdev, nxp->rx_skb, data, count,
				   nxp_recv_pkts, ARRAY_SIZE(nxp_recv_pkts));
	if (IS_ERR(nxp->rx_skb)) {
		int err = PTR_ERR(nxp->rx_skb);

		bt_dev_err(hu->hdev, "Frame reassembly failed (%d)", err);
		nxp->rx_skb = NULL;
		return err;
	}

	return count;
}

static struct sk_buff *nxp_dequeue(struct hci_uart *hu)
{
	struct nxp_struct *nxp = hu->priv;

	if (ps_wakeup(hu)) {
		clear_bit(HCI_UART_SENDING, &hu->tx_state);
		return 0;
	}
	ps_start_timer(hu);
	return skb_dequeue(&nxp->txq);
}

static int nxp_setup(struct hci_uart *hu)
{
	ps_init(hu);
	return 0;
}

static const struct hci_uart_proto nxpp = {
	.id		= HCI_UART_NXP,
	.name		= "NXP",
	.open		= nxp_open,
	.close		= nxp_close,
	.recv		= nxp_recv,
	.enqueue	= nxp_enqueue,
	.dequeue	= nxp_dequeue,
	.flush		= nxp_flush,
	.setup		= nxp_setup,
};

int __init nxp_init(void)
{
	return hci_uart_register_proto(&nxpp);
}

int __exit nxp_deinit(void)
{
	return hci_uart_unregister_proto(&nxpp);
}
