// SPDX-License-Identifier: GPL-2.0-or-later
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

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/skbuff.h>
#include <asm/unaligned.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/crc8.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btnxpuart.h"
#include "h4_recv.h"

#define BTNXPUART_TX_STATE_ACTIVE	1
#define BTNXPUART_TX_STATE_WAKEUP	2
#define BTNXPUART_FW_DOWNLOADING	3

static u8 crc8_table[CRC8_TABLE_SIZE];
static unsigned long crc32_table[256];

static struct sk_buff *nxp_drv_send_cmd(struct hci_dev *hdev, u16 opcode,
										u32 plen, void *param)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = nxpdev->psdata;
	struct sk_buff *skb;

	psdata->driver_sent_cmd = true;	/* set flag to prevent re-sending command in nxp_enqueue */
	skb = __hci_cmd_sync(hdev, opcode, plen, param, HCI_CMD_TIMEOUT);
	psdata->driver_sent_cmd = false;

	return skb;
}

/* NXP Power Save Feature */
int wakeupmode = WAKEUP_METHOD_BREAK;
int ps_mode = PS_MODE_ENABLE;

static void ps_start_timer(struct btnxpuart_dev *nxpdev)
{
	struct ps_data *psdata = nxpdev->psdata;

	if (!psdata)
		return;

	if (psdata->cur_psmode == PS_MODE_ENABLE) {
		psdata->timer_on = 1;
		mod_timer(&psdata->ps_timer, jiffies + (psdata->interval * HZ) / 1000);
	}
}

static void ps_cancel_timer(struct btnxpuart_dev *nxpdev)
{
	struct ps_data *psdata = nxpdev->psdata;

	if (!psdata)
		return;

	flush_work(&psdata->work);
	if (psdata->timer_on)
		del_timer_sync(&psdata->ps_timer);
	kfree(psdata);
}

static void ps_control(struct hci_dev *hdev, u8 ps_state)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = nxpdev->psdata;

	if (psdata->ps_state == ps_state)
		return;

	switch (psdata->cur_wakeupmode) {
	case WAKEUP_METHOD_DTR:
		if (ps_state == PS_STATE_AWAKE)
			serdev_device_set_tiocm(nxpdev->serdev, TIOCM_DTR, 0);
		else
			serdev_device_set_tiocm(nxpdev->serdev, 0, TIOCM_DTR);
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		BT_INFO("Set UART break: %s", ps_state == PS_STATE_AWAKE ? "off" : "on");
		if (ps_state == PS_STATE_AWAKE)
			serdev_device_break_ctl(nxpdev->serdev, 0);
		else
			serdev_device_break_ctl(nxpdev->serdev, -1);
		break;
	}
	psdata->ps_state = ps_state;

	if (ps_state == PS_STATE_AWAKE)
		btnxpuart_tx_wakeup(nxpdev);
}

static void ps_work_func(struct work_struct *work)
{
	struct ps_data *data = container_of(work, struct ps_data, work);

	if (data->ps_cmd == PS_CMD_ENTER_PS && data->cur_psmode == PS_MODE_ENABLE)
		ps_control(data->hdev, PS_STATE_SLEEP);
	else if (data->ps_cmd == PS_CMD_EXIT_PS)
		ps_control(data->hdev, PS_STATE_AWAKE);
}

static void ps_timeout_func(struct timer_list *t)
{
	struct ps_data *data = from_timer(data, t, ps_timer);
	struct hci_dev *hdev = data->hdev;
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	data->timer_on = 0;
	if (test_bit(BTNXPUART_TX_STATE_ACTIVE, &nxpdev->tx_state)) {
		ps_start_timer(nxpdev);
	} else {
		data->ps_cmd = PS_CMD_ENTER_PS;
		schedule_work(&data->work);
	}
}

static int ps_init_work(struct hci_dev *hdev)
{
	struct ps_data *psdata = kzalloc(sizeof(*psdata), GFP_KERNEL);
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	if (!psdata) {
		BT_ERR("Can't allocate control structure for Power Save feature");
		return -ENOMEM;
	}
	nxpdev->psdata = psdata;

	psdata->interval = PS_DEFAULT_TIMEOUT_PERIOD;
	psdata->ps_state = PS_STATE_AWAKE;
	psdata->ps_mode = ps_mode;
	psdata->hdev = hdev;

	switch (wakeupmode) {
	case WAKEUP_METHOD_DTR:
		psdata->wakeupmode = WAKEUP_METHOD_DTR;
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		psdata->wakeupmode = WAKEUP_METHOD_BREAK;
		break;
	}

	psdata->cur_psmode = PS_MODE_DISABLE;
	psdata->cur_wakeupmode = WAKEUP_METHOD_INVALID;
	INIT_WORK(&psdata->work, ps_work_func);

	return 0;
}

static void ps_init_timer(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = nxpdev->psdata;

	psdata->timer_on = 0;
	timer_setup(&psdata->ps_timer, ps_timeout_func, 0);
}

static int ps_wakeup(struct btnxpuart_dev *nxpdev)
{
	struct ps_data *psdata = nxpdev->psdata;
	int ret = 1;

	if (psdata->ps_state == PS_STATE_AWAKE)
		ret = 0;
	psdata->ps_cmd = PS_CMD_EXIT_PS;
	schedule_work(&psdata->work);

	return ret;
}

static int send_ps_cmd(struct hci_dev *hdev, void *data)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = nxpdev->psdata;
	u8 pcmd;
	struct sk_buff *skb;
	u8 *status;

	if (psdata->ps_mode == PS_MODE_ENABLE)
		pcmd = BT_PS_ENABLE;
	else
		pcmd = BT_PS_DISABLE;

	skb = nxp_drv_send_cmd(hdev, HCI_NXP_AUTO_SLEEP_MODE, 1, &pcmd);

	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting Power Save mode failed (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	status = skb_pull_data(skb, 1);

	if (status) {
		if (!*status)
			psdata->cur_psmode = psdata->ps_mode;
		else
			psdata->ps_mode = psdata->cur_psmode;
		if (psdata->cur_psmode == PS_MODE_ENABLE)
			ps_start_timer(nxpdev);
		else
			ps_wakeup(nxpdev);
		BT_INFO("Power Save mode response: status=%d, ps_mode=%d",
			*status, psdata->cur_psmode);
	}
	kfree_skb(skb);

	return 0;
}

static int send_wakeup_method_cmd(struct hci_dev *hdev, void *data)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = nxpdev->psdata;
	u8 pcmd[4];
	struct sk_buff *skb;
	u8 *status;

	pcmd[0] = BT_HOST_WAKEUP_METHOD_NONE;
	pcmd[1] = BT_HOST_WAKEUP_DEFAULT_GPIO;
	switch (psdata->wakeupmode) {
	case WAKEUP_METHOD_DTR:
		pcmd[2] = BT_CTRL_WAKEUP_METHOD_DSR;
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		pcmd[2] = BT_CTRL_WAKEUP_METHOD_BREAK;
		break;
	}
	pcmd[3] = 0xFF;

	skb = nxp_drv_send_cmd(hdev, HCI_NXP_WAKEUP_METHOD, 4, pcmd);

	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting wake-up method failed (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	status = skb_pull_data(skb, 1);
	if (status) {
		if (*status == 0)
			psdata->cur_wakeupmode = psdata->wakeupmode;
		else
			psdata->wakeupmode = psdata->cur_wakeupmode;
		BT_INFO("Set Wakeup Method response: status=%d, wakeupmode=%d",
			*status, psdata->cur_wakeupmode);
	}
	kfree_skb(skb);

	return 0;
}

static int ps_init(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = nxpdev->psdata;

	serdev_device_set_tiocm(nxpdev->serdev, TIOCM_RTS, 0);

	switch (psdata->wakeupmode) {
	case WAKEUP_METHOD_DTR:
		serdev_device_set_tiocm(nxpdev->serdev, 0, TIOCM_DTR);
		serdev_device_set_tiocm(nxpdev->serdev, TIOCM_DTR, 0);
		break;
	case WAKEUP_METHOD_BREAK:
	default:
		serdev_device_break_ctl(nxpdev->serdev, -1);
		serdev_device_break_ctl(nxpdev->serdev, 0);
		break;
	}
	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		BT_ERR("HCI_RUNNING is not set");
		return -EBUSY;
	}
	if (psdata->cur_wakeupmode != psdata->wakeupmode)
		hci_cmd_sync_queue(hdev, send_wakeup_method_cmd, NULL, NULL);
	if (psdata->cur_psmode != psdata->ps_mode)
		hci_cmd_sync_queue(hdev, send_ps_cmd, NULL, NULL);

	return 0;
}

/* NXP Firmware Download Feature */
static void nxp_fw_dnld_gen_crc_table(void)
{
	int i, j;
	unsigned long crc_accum;

	for (i = 0; i < 256; i++) {
		crc_accum = ((unsigned long)i << 24);
		for (j = 0; j < 8; j++) {
			if (crc_accum & 0x80000000L)
				crc_accum = (crc_accum << 1) ^ POLYNOMIAL32;
			else
				crc_accum = (crc_accum << 1);
		}
		crc32_table[i] = crc_accum;
	}
}

static unsigned long nxp_fw_dnld_update_crc(unsigned long crc_accum,
										char *data_blk_ptr,
										int data_blk_size)
{
	unsigned long i, j;

	for (j = 0; j < data_blk_size; j++) {
		i = ((unsigned long)(crc_accum >> 24) ^ *data_blk_ptr++) & 0xff;
		crc_accum = (crc_accum << 8) ^ crc32_table[i];
	}
	return crc_accum;
}

static int nxp_download_firmware(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	const struct btnxpuart_data *nxp_data = nxpdev->nxp_data;
	const char *fw_name_dt;
	int err = 0;

	nxpdev->fw_dnld_offset = 0;
	nxpdev->fw_sent_bytes = 0;

	set_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
	crc8_populate_msb(crc8_table, POLYNOMIAL8);
	nxp_fw_dnld_gen_crc_table();

	if (!device_property_read_string(&nxpdev->serdev->dev, "firmware-name",
										&fw_name_dt))
		snprintf(nxpdev->fw_name, MAX_FW_FILE_NAME_LEN, "nxp/%s",
					fw_name_dt);
	else
		snprintf(nxpdev->fw_name, MAX_FW_FILE_NAME_LEN, "%s",
					nxp_data->fw_name);

	BT_INFO("Request Firmware: %s", nxpdev->fw_name);
	err = request_firmware(&nxpdev->fw, nxpdev->fw_name, &hdev->dev);
	if (err < 0) {
		BT_ERR("Firmware file %s not found", nxpdev->fw_name);
		clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
	}

	serdev_device_set_baudrate(nxpdev->serdev, nxp_data->fw_dnld_pri_baudrate);
	serdev_device_set_flow_control(nxpdev->serdev, 0);
	nxpdev->current_baudrate = nxp_data->fw_dnld_pri_baudrate;
	nxpdev->fw_v3_offset_correction = 0;

	/* Wait till FW is downloaded and CTS becomes low */
	init_waitqueue_head(&nxpdev->suspend_wait_q);
	err = wait_event_interruptible_timeout(nxpdev->suspend_wait_q,
			!test_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state),
			msecs_to_jiffies(60000));
	if (err == 0) {
		BT_ERR("FW Download Timeout.");
		return -ETIMEDOUT;
	}

	err = serdev_device_wait_for_cts(nxpdev->serdev, 1, 60000);
	if (err < 0) {
		BT_ERR("CTS is still high. FW Download failed.");
		return err;
	}
	BT_INFO("CTS is low");
	release_firmware(nxpdev->fw);

	/* Allow the downloaded FW to initialize */
	usleep_range(20000, 22000);

	return 0;
}

static int nxp_send_ack(u8 ack, struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	u8 ack_nak[2];

	if (ack == NXP_ACK_V1 || ack == NXP_NAK_V1) {
		ack_nak[0] = ack;
		serdev_device_write_buf(nxpdev->serdev, ack_nak, 1);
	} else if (ack == NXP_ACK_V3) {
		ack_nak[0] = ack;
		ack_nak[1] = crc8(crc8_table, ack_nak, 1, 0xFF);
		serdev_device_write_buf(nxpdev->serdev, ack_nak, 2);
	}
	return 0;
}

static bool nxp_fw_change_baudrate(struct hci_dev *hdev, u16 req_len)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct nxp_bootloader_cmd nxp_cmd5;
	struct uart_config uart_config;

	if (req_len == sizeof(nxp_cmd5)) {
		nxp_cmd5.header = __cpu_to_le32(5);
		nxp_cmd5.arg = 0;
		nxp_cmd5.payload_len = __cpu_to_le32(sizeof(uart_config));
		nxp_cmd5.crc = swab32(nxp_fw_dnld_update_crc(0UL,
									 (char *)&nxp_cmd5,
									 sizeof(nxp_cmd5) - 4));

		serdev_device_write_buf(nxpdev->serdev, (u8 *)&nxp_cmd5, req_len);
		nxpdev->fw_v3_offset_correction += req_len;
	} else if (req_len == sizeof(uart_config)) {
		uart_config.clkdiv.address = __cpu_to_le32(CLKDIVADDR);
		uart_config.clkdiv.value = __cpu_to_le32(0x00C00000);
		uart_config.uartdiv.address = __cpu_to_le32(UARTDIVADDR);
		uart_config.uartdiv.value = __cpu_to_le32(1);
		uart_config.mcr.address = __cpu_to_le32(UARTMCRADDR);
		uart_config.mcr.value = __cpu_to_le32(MCR);
		uart_config.re_init.address = __cpu_to_le32(UARTREINITADDR);
		uart_config.re_init.value = __cpu_to_le32(INIT);
		uart_config.icr.address = __cpu_to_le32(UARTICRADDR);
		uart_config.icr.value = __cpu_to_le32(ICR);
		uart_config.fcr.address = __cpu_to_le32(UARTFCRADDR);
		uart_config.fcr.value = __cpu_to_le32(FCR);
		uart_config.crc = swab32(nxp_fw_dnld_update_crc(0UL,
										(char *)&uart_config,
										sizeof(uart_config) - 4));
		serdev_device_write_buf(nxpdev->serdev, (u8 *)&uart_config, req_len);
		serdev_device_wait_until_sent(nxpdev->serdev, 0);
		nxpdev->fw_v3_offset_correction += req_len;
		return true;
	}
	return false;
}

static bool nxp_fw_change_timeout(struct hci_dev *hdev, u16 req_len)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct nxp_bootloader_cmd nxp_cmd7;

	if (req_len == sizeof(nxp_cmd7)) {
		nxp_cmd7.header = __cpu_to_le32(7);
		nxp_cmd7.arg = __cpu_to_le32(0x70);
		nxp_cmd7.payload_len = 0;
		nxp_cmd7.crc = swab32(nxp_fw_dnld_update_crc(0UL,
										(char *)&nxp_cmd7,
										sizeof(nxp_cmd7) - 4));

		serdev_device_write_buf(nxpdev->serdev, (u8 *)&nxp_cmd7, req_len);
		serdev_device_wait_until_sent(nxpdev->serdev, 0);
		nxpdev->fw_v3_offset_correction += req_len;
		return true;
	}
	return false;
}


static u32 nxp_get_data_len(const u8 *buf)
{
	struct nxp_bootloader_cmd *hdr = (struct nxp_bootloader_cmd *)buf;
	return __le32_to_cpu(hdr->payload_len);
}

/* for legacy chipsets with V1 bootloader */
static int nxp_recv_fw_req_v1(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct v1_data_req *req = skb_pull_data(skb, sizeof(struct v1_data_req));
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	const struct btnxpuart_data *nxp_data = nxpdev->nxp_data;
	static bool timeout_changed;
	static bool baudrate_changed;
	u32 requested_len;
	static u32 expected_len = HDR_LEN;

	if (!test_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state))
		goto ret;

	if (!nxpdev->fw)
		goto ret;

	if (req && (req->len ^ req->len_comp) != 0xffff) {
		BT_INFO("ERR: Send NAK");
		nxp_send_ack(NXP_NAK_V1, hdev);
		goto ret;
	}

	if (nxp_data->fw_dnld_sec_baudrate != nxpdev->current_baudrate) {
		if (!timeout_changed) {
			nxp_send_ack(NXP_ACK_V1, hdev);
			timeout_changed = nxp_fw_change_timeout(hdev, req->len);
			goto ret;
		}
		if (!baudrate_changed) {
			nxp_send_ack(NXP_ACK_V1, hdev);
			baudrate_changed = nxp_fw_change_baudrate(hdev, req->len);
			if (baudrate_changed) {
				serdev_device_set_baudrate(nxpdev->serdev,
								nxp_data->fw_dnld_sec_baudrate);
				nxpdev->current_baudrate = nxp_data->fw_dnld_sec_baudrate;
			}
			goto ret;
		}
	}

	nxp_send_ack(NXP_ACK_V1, hdev);
	requested_len = req->len;
	if (requested_len == 0) {
		BT_INFO("FW Downloaded Successfully: %zu bytes", nxpdev->fw->size);
		clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
		wake_up_interruptible(&nxpdev->suspend_wait_q);
		goto ret;
	}
	if (requested_len & 0x01) {
		/* The CRC did not match at the other end.
		* That's why the request to re-send.
		* Simply send the same bytes again.
		*/
		requested_len = nxpdev->fw_sent_bytes;
		BT_ERR("CRC error. Resend %d bytes of FW.", requested_len);
	} else {
		/* The FW bin file is made up of many blocks of
		* 16 byte header and payload data chunks. If the
		* FW has requested a header, read the payload length
		* info from the header, before sending the header.
		* In the next iteration, the FW should request the
		* payload data chunk, which should be equal to the
		* payload length read from header. If there is a
		* mismatch, clearly the driver and FW are out of sync,
		* and we need to re-send the previous chunk again.
		*/
		if (requested_len == expected_len) {
			/* All OK here. Increment offset by number
			* of previous successfully sent bytes.
			*/
			nxpdev->fw_dnld_offset += nxpdev->fw_sent_bytes;

			if (requested_len == HDR_LEN)
				expected_len = nxp_get_data_len(nxpdev->fw->data +
									nxpdev->fw_dnld_offset);
			else
				expected_len = HDR_LEN;
		}
	}

	if (nxpdev->fw_dnld_offset + requested_len <= nxpdev->fw->size)
		serdev_device_write_buf(nxpdev->serdev,
				nxpdev->fw->data + nxpdev->fw_dnld_offset,
				requested_len);
	nxpdev->fw_sent_bytes = requested_len;

ret:
	kfree_skb(skb);
	return 0;
}

static int nxp_recv_chip_ver_v3(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct v3_start_ind *req = skb_pull_data(skb, sizeof(struct v3_start_ind));
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	const struct btnxpuart_data *nxp_data = nxpdev->nxp_data;

	if (!test_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state))
		goto ret;

	if (!req)
		goto ret;

	if (req->chip_id != nxp_data->chip_signature) {
		BT_ERR("Invalid chip signature received");
		clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
	} else {
		nxp_send_ack(NXP_ACK_V3, hdev);
	}

ret:
	kfree_skb(skb);
	return 0;
}

static int nxp_recv_fw_req_v3(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct v3_data_req *req = skb_pull_data(skb, sizeof(struct v3_data_req));
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	const struct btnxpuart_data *nxp_data = nxpdev->nxp_data;
	static bool timeout_changed;
	static bool baudrate_changed;

	if (!req || !nxpdev || !nxpdev->fw->data)
		goto ret;

	if (!test_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state))
		goto ret;

	nxp_send_ack(NXP_ACK_V3, hdev);

	if (nxpdev->current_baudrate != nxp_data->fw_dnld_sec_baudrate) {
		if (!timeout_changed) {
			timeout_changed = nxp_fw_change_timeout(hdev, req->len);
			goto ret;
		}

		if (!baudrate_changed) {
			baudrate_changed = nxp_fw_change_baudrate(hdev, req->len);
			if (baudrate_changed) {
				serdev_device_set_baudrate(nxpdev->serdev,
								nxp_data->fw_dnld_sec_baudrate);
				nxpdev->current_baudrate = nxp_data->fw_dnld_sec_baudrate;
			}
			goto ret;
		}
	}

	if (req->len == 0) {
		BT_INFO("FW Downloaded Successfully: %zu bytes", nxpdev->fw->size);
		clear_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state);
		wake_up_interruptible(&nxpdev->suspend_wait_q);
		goto ret;
	}
	if (req->error)
		BT_ERR("FW Download received err 0x%02X from chip. Resending FW chunk.",
			   req->error);

	if (req->offset < nxpdev->fw_v3_offset_correction) {
		/* This scenario should ideally never occur.
		 * But if it ever does, FW is out of sync and
		 * needs a power cycle.
		 */
		BT_ERR("Something went wrong during FW download. Please power cycle and try again");
		goto ret;
	}

	serdev_device_write_buf(nxpdev->serdev,
				nxpdev->fw->data + req->offset - nxpdev->fw_v3_offset_correction,
				req->len);

ret:
	kfree_skb(skb);
	return 0;
}

static int nxp_set_baudrate_cmd(struct hci_dev *hdev, void *data)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	u32 new_baudrate = __cpu_to_le32(nxpdev->new_baudrate);
	struct ps_data *psdata = nxpdev->psdata;
	u8 *pcmd = (u8 *)&new_baudrate;
	struct sk_buff *skb;
	u8 *status;

	if (!psdata)
		return -EFAULT;

	skb = nxp_drv_send_cmd(hdev, HCI_NXP_SET_OPER_SPEED, 4, pcmd);

	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "Setting baudrate failed (%ld)", PTR_ERR(skb));
		return PTR_ERR(skb);
	}

	status = skb_pull_data(skb, 1);
	if (status) {
		if (*status == 0) {
			serdev_device_set_baudrate(nxpdev->serdev, nxpdev->new_baudrate);
			nxpdev->current_baudrate = nxpdev->new_baudrate;
		}
		BT_INFO("Set baudrate response: status=%d, baudrate=%d",
			*status, nxpdev->new_baudrate);
	}
	kfree_skb(skb);

	return 0;
}

/* NXP protocol */
static int nxp_setup(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	const struct btnxpuart_data *nxp_data = nxpdev->nxp_data;
	int ret = 0;

	if (!serdev_device_get_cts(nxpdev->serdev)) {
		BT_INFO("CTS high. Need FW Download");
		ret = nxp_download_firmware(hdev);
		if (ret < 0)
			goto err;
	} else {
		BT_INFO("CTS low. FW already running.");
	}

	serdev_device_set_flow_control(nxpdev->serdev, 1);
	serdev_device_set_baudrate(nxpdev->serdev, nxp_data->fw_init_baudrate);
	nxpdev->current_baudrate = nxp_data->fw_init_baudrate;

	if (nxpdev->current_baudrate != nxp_data->oper_speed) {
		nxpdev->new_baudrate = nxp_data->oper_speed;
		hci_cmd_sync_queue(hdev, nxp_set_baudrate_cmd, NULL, NULL);
	}

	if (!ps_init_work(hdev))
		ps_init_timer(hdev);
	ps_init(hdev);
err:
	return ret;
}

static int nxp_enqueue(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	struct ps_data *psdata = nxpdev->psdata;
	struct hci_command_hdr *hdr;
	u8 *param;

	if (!psdata) {
		kfree_skb(skb);
		goto ret;
	}

	/* if commands are received from user space (e.g. hcitool), update
	 * driver flags accordingly and ask driver to re-send the command
	 */
	if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT && !psdata->driver_sent_cmd) {
		hdr = (struct hci_command_hdr *)skb->data;
		param = skb->data + HCI_COMMAND_HDR_SIZE;
		switch (__le16_to_cpu(hdr->opcode)) {
		case HCI_NXP_AUTO_SLEEP_MODE:
			if (hdr->plen >= 1) {
				if (param[0] == BT_PS_ENABLE)
					psdata->ps_mode = PS_MODE_ENABLE;
				else if (param[0] == BT_PS_DISABLE)
					psdata->ps_mode = PS_MODE_DISABLE;
				hci_cmd_sync_queue(hdev, send_ps_cmd, NULL, NULL);
				kfree_skb(skb);
				goto ret;
			}
			break;
		case HCI_NXP_WAKEUP_METHOD:
			if (hdr->plen >= 4) {
				switch (param[2]) {
				case BT_CTRL_WAKEUP_METHOD_DSR:
					psdata->wakeupmode = WAKEUP_METHOD_DTR;
					break;
				case BT_CTRL_WAKEUP_METHOD_BREAK:
				default:
					psdata->wakeupmode = WAKEUP_METHOD_BREAK;
					break;
				}
				hci_cmd_sync_queue(hdev, send_wakeup_method_cmd, NULL, NULL);
				kfree_skb(skb);
				goto ret;
			}
			break;
		case HCI_NXP_SET_OPER_SPEED:
			if (hdr->plen == 4) {
				nxpdev->new_baudrate = *((u32 *)param);
				hci_cmd_sync_queue(hdev, nxp_set_baudrate_cmd, NULL, NULL);
				kfree_skb(skb);
				goto ret;
			}
		}
	}

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	skb_queue_tail(&nxpdev->txq, skb);

	btnxpuart_tx_wakeup(nxpdev);
ret:
	return 0;
}

static struct sk_buff *nxp_dequeue(void *data)
{
	struct btnxpuart_dev *nxpdev = (struct btnxpuart_dev *)data;

	ps_wakeup(nxpdev);
	ps_start_timer(nxpdev);
	return skb_dequeue(&nxpdev->txq);
}

/* btnxpuart based on serdev */
static void btnxpuart_tx_work(struct work_struct *work)
{
	struct btnxpuart_dev *nxpdev = container_of(work, struct btnxpuart_dev,
						   tx_work);
	struct serdev_device *serdev = nxpdev->serdev;
	struct hci_dev *hdev = nxpdev->hdev;

	if (!nxpdev->nxp_data->dequeue)
		return;

	while (1) {
		clear_bit(BTNXPUART_TX_STATE_WAKEUP, &nxpdev->tx_state);

		while (1) {
			struct sk_buff *skb = nxpdev->nxp_data->dequeue(nxpdev);
			int len;

			if (!skb)
				break;

			len = serdev_device_write_buf(serdev, skb->data, skb->len);
			hdev->stat.byte_tx += len;

			skb_pull(skb, len);
			if (skb->len > 0) {
				skb_queue_head(&nxpdev->txq, skb);
				break;
			}

			switch (hci_skb_pkt_type(skb)) {
			case HCI_COMMAND_PKT:
				hdev->stat.cmd_tx++;
				break;
			case HCI_ACLDATA_PKT:
				hdev->stat.acl_tx++;
				break;
			case HCI_SCODATA_PKT:
				hdev->stat.sco_tx++;
				break;
			}

			kfree_skb(skb);
		}

		if (!test_bit(BTNXPUART_TX_STATE_WAKEUP, &nxpdev->tx_state))
			break;
	}
	clear_bit(BTNXPUART_TX_STATE_ACTIVE, &nxpdev->tx_state);
}

static void btnxpuart_tx_wakeup(struct btnxpuart_dev *nxpdev)
{
	if (test_and_set_bit(BTNXPUART_TX_STATE_ACTIVE, &nxpdev->tx_state)) {
		set_bit(BTNXPUART_TX_STATE_WAKEUP, &nxpdev->tx_state);
		return;
	}
	schedule_work(&nxpdev->tx_work);
}

static int btnxpuart_open(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	int err;

	err = serdev_device_open(nxpdev->serdev);
	if (err) {
		bt_dev_err(hdev, "Unable to open UART device %s",
			   dev_name(&nxpdev->serdev->dev));
		return err;
	}

	if (nxpdev->nxp_data->open) {
		err = nxpdev->nxp_data->open(hdev);
		if (err) {
			serdev_device_close(nxpdev->serdev);
			return err;
		}
	}

	return 0;
}

static int btnxpuart_close(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);
	int err;

	if (nxpdev->nxp_data->close) {
		err = nxpdev->nxp_data->close(hdev);
		if (err)
			return err;
	}

	serdev_device_close(nxpdev->serdev);

	return 0;
}

static int btnxpuart_flush(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	/* Flush any pending characters */
	serdev_device_write_flush(nxpdev->serdev);
	skb_queue_purge(&nxpdev->txq);

	cancel_work_sync(&nxpdev->tx_work);

	kfree_skb(nxpdev->rx_skb);
	nxpdev->rx_skb = NULL;

	return 0;
}

static int btnxpuart_setup(struct hci_dev *hdev)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	if (nxpdev->nxp_data->setup)
		return nxpdev->nxp_data->setup(hdev);

	return 0;
}

static int btnxpuart_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btnxpuart_dev *nxpdev = hci_get_drvdata(hdev);

	if (nxpdev->nxp_data->enqueue)
		nxpdev->nxp_data->enqueue(hdev, skb);

	return 0;
}

static int btnxpuart_receive_buf(struct serdev_device *serdev, const u8 *data,
								 size_t count)
{
	struct btnxpuart_dev *nxpdev = serdev_device_get_drvdata(serdev);
	const struct btnxpuart_data *nxp_data = nxpdev->nxp_data;

	if (test_bit(BTNXPUART_FW_DOWNLOADING, &nxpdev->tx_state)) {
		if (*data != NXP_V1_FW_REQ_PKT && *data != NXP_V1_CHIP_VER_PKT &&
		   *data != NXP_V3_FW_REQ_PKT && *data != NXP_V3_CHIP_VER_PKT) {
			/* Unknown bootloader signature, skip without returning error */
			return count;
		}
	}

	ps_start_timer(nxpdev);

	nxpdev->rx_skb = h4_recv_buf(nxpdev->hdev, nxpdev->rx_skb, data, count,
						nxp_data->recv_pkts, nxp_data->recv_pkts_cnt);
	if (IS_ERR(nxpdev->rx_skb)) {
		int err = PTR_ERR(nxpdev->rx_skb);

		bt_dev_err(nxpdev->hdev, "Frame reassembly failed (%d)", err);
		nxpdev->rx_skb = NULL;
		return err;
	}
	nxpdev->hdev->stat.byte_rx += count;
	return count;
}

static void btnxpuart_write_wakeup(struct serdev_device *serdev)
{
	serdev_device_write_wakeup(serdev);
}

static const struct serdev_device_ops btnxpuart_client_ops = {
	.receive_buf = btnxpuart_receive_buf,
	.write_wakeup = btnxpuart_write_wakeup,
};

static int nxp_serdev_probe(struct serdev_device *serdev)
{
	struct hci_dev *hdev;
	struct btnxpuart_dev *nxpdev;

	nxpdev = devm_kzalloc(&serdev->dev, sizeof(*nxpdev), GFP_KERNEL);
	if (!nxpdev)
		return -ENOMEM;

	nxpdev->nxp_data = device_get_match_data(&serdev->dev);

	nxpdev->serdev = serdev;
	serdev_device_set_drvdata(serdev, nxpdev);

	serdev_device_set_client_ops(serdev, &btnxpuart_client_ops);

	INIT_WORK(&nxpdev->tx_work, btnxpuart_tx_work);
	skb_queue_head_init(&nxpdev->txq);

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(&serdev->dev, "Can't allocate HCI device\n");
		return -ENOMEM;
	}

	nxpdev->hdev = hdev;

	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, nxpdev);

	hdev->manufacturer = 37;
	hdev->open  = btnxpuart_open;
	hdev->close = btnxpuart_close;
	hdev->flush = btnxpuart_flush;
	hdev->setup = btnxpuart_setup;
	hdev->send  = btnxpuart_send_frame;
	SET_HCIDEV_DEV(hdev, &serdev->dev);

	if (hci_register_dev(hdev) < 0) {
		dev_err(&serdev->dev, "Can't register HCI device\n");
		hci_free_dev(hdev);
		return -ENODEV;
	}

	return 0;
}

static void nxp_serdev_remove(struct serdev_device *serdev)
{
	struct btnxpuart_dev *nxpdev = serdev_device_get_drvdata(serdev);
	const struct btnxpuart_data *nxp_data = nxpdev->nxp_data;
	struct hci_dev *hdev = nxpdev->hdev;

	/* Restore FW baudrate to fw_init_baudrate if changed.
	 * This will ensure FW baudrate is in sync with
	 * driver baudrate in case this driver is re-inserted.
	 */
	if (nxp_data->fw_init_baudrate != nxpdev->current_baudrate) {
		nxpdev->new_baudrate = nxp_data->fw_init_baudrate;
		nxp_set_baudrate_cmd(hdev, NULL);
	}

	ps_cancel_timer(nxpdev);
	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
}

static const struct h4_recv_pkt nxp_recv_pkts[] = {
	{ H4_RECV_ACL,          .recv = hci_recv_frame },
	{ H4_RECV_SCO,          .recv = hci_recv_frame },
	{ H4_RECV_EVENT,        .recv = hci_recv_frame },
	{ NXP_RECV_FW_REQ_V1,   .recv = nxp_recv_fw_req_v1 },
	{ NXP_RECV_CHIP_VER_V3, .recv = nxp_recv_chip_ver_v3 },
	{ NXP_RECV_FW_REQ_V3,   .recv = nxp_recv_fw_req_v3 },
};

static const struct btnxpuart_data w8987_data = {
	.recv_pkts	= nxp_recv_pkts,
	.recv_pkts_cnt	= ARRAY_SIZE(nxp_recv_pkts),
	.fw_dnld_pri_baudrate = 115200,
	.fw_dnld_sec_baudrate = 3000000,
	.fw_init_baudrate = 115200,
	.oper_speed		= 3000000,
	.setup		= nxp_setup,
	.enqueue    = nxp_enqueue,
	.dequeue    = nxp_dequeue,
	.chip_signature = 0xffff,
	.fw_name = FIRMWARE_W8987,
};

static const struct btnxpuart_data w8997_data = {
	.recv_pkts	= nxp_recv_pkts,
	.recv_pkts_cnt	= ARRAY_SIZE(nxp_recv_pkts),
	.fw_dnld_pri_baudrate = 115200,
	.fw_dnld_sec_baudrate = 3000000,
	.fw_init_baudrate = 115200,
	.oper_speed		= 3000000,
	.setup		= nxp_setup,
	.enqueue    = nxp_enqueue,
	.dequeue    = nxp_dequeue,
	.chip_signature = 0xffff,
	.fw_name = FIRMWARE_W8997,
};

static const struct btnxpuart_data w9098_data = {
	.recv_pkts	= nxp_recv_pkts,
	.recv_pkts_cnt	= ARRAY_SIZE(nxp_recv_pkts),
	.fw_dnld_pri_baudrate = 115200,
	.fw_dnld_sec_baudrate = 3000000,
	.fw_init_baudrate = 3000000,
	.oper_speed		= 3000000,
	.setup		= nxp_setup,
	.enqueue    = nxp_enqueue,
	.dequeue    = nxp_dequeue,
	.chip_signature = 0x5c03,
	.fw_name = FIRMWARE_W9098,
};

static const struct btnxpuart_data iw416_data = {
	.recv_pkts	= nxp_recv_pkts,
	.recv_pkts_cnt	= ARRAY_SIZE(nxp_recv_pkts),
	.fw_dnld_pri_baudrate = 115200,
	.fw_dnld_sec_baudrate = 3000000,
	.fw_init_baudrate = 3000000,
	.oper_speed		= 3000000,
	.setup		= nxp_setup,
	.enqueue    = nxp_enqueue,
	.dequeue    = nxp_dequeue,
	.chip_signature = 0x7201,
	.fw_name = FIRMWARE_IW416,
};

static const struct btnxpuart_data iw612_data = {
	.recv_pkts	= nxp_recv_pkts,
	.recv_pkts_cnt	= ARRAY_SIZE(nxp_recv_pkts),
	.fw_dnld_pri_baudrate = 115200,
	.fw_dnld_sec_baudrate = 3000000,
	.fw_init_baudrate = 3000000,
	.oper_speed		= 3000000,
	.setup		= nxp_setup,
	.enqueue    = nxp_enqueue,
	.dequeue    = nxp_dequeue,
	.chip_signature = 0x7601,
	.fw_name = FIRMWARE_IW612,
};

#ifdef CONFIG_OF
static const struct of_device_id nxpuart_of_match_table[] = {
	{ .compatible = "nxp,w8987-bt", .data = &w8987_data },
	{ .compatible = "nxp,w8997-bt", .data = &w8997_data },
	{ .compatible = "nxp,w9098-bt", .data = &w9098_data },
	{ .compatible = "nxp,iw416-bt", .data = &iw416_data },
	{ .compatible = "nxp,iw612-bt", .data = &iw612_data },
	{ }
};
MODULE_DEVICE_TABLE(of, nxpuart_of_match_table);
#endif

static struct serdev_device_driver nxp_serdev_driver = {
	.probe = nxp_serdev_probe,
	.remove = nxp_serdev_remove,
	.driver = {
		.name = "btnxpuart",
		.of_match_table = of_match_ptr(nxpuart_of_match_table),
	},
};

module_serdev_device_driver(nxp_serdev_driver);

MODULE_AUTHOR("Neeraj Sanjay Kale <neeraj.sanjaykale@nxp.com>");
MODULE_DESCRIPTION("NXP Bluetooth Serial driver v1.0 ");
MODULE_VERSION("v1.0");
MODULE_LICENSE("GPL");
