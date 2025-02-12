// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Bluetooth HCI UART driver for WILC devices
 *
 */
#include "linux/bitops.h"
#include "linux/byteorder/generic.h"
#include "linux/err.h"
#include "linux/gfp_types.h"
#include "net/bluetooth/bluetooth.h"
#include "net/bluetooth/hci.h"
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/wilc.h>

#include "hci_uart.h"

#define WILC_BT_UART_MANUFACTURER	205
#define WILC_UART_DEFAULT_BAUDRATE	115200
#define WILC_UART_BAUDRATE		460800

#define HCI_VERSION_BOOTROM	0xFF
#define HCI_VERSION_FIRMWARE	0x06

#define HCI_VENDOR_CMD_WRITE_MEM	0xFC52
#define HCI_VENDOR_CMD_UPDATE_UART	0xFC53
#define HCI_VENDOR_CMD_UPDATE_ADDR	0xFC54
#define HCI_VENDOR_CMD_RESET		0xFC55
#define HCI_VENDOR_CMD_READ_REG		0xFC01

struct wilc_adapter {
	struct hci_uart hu;
	struct device *dev;
	void *wlan_priv;
	bool flow_control;
};

struct wilc_data {
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
};

struct hci_update_uart_param {
	__le32 baudrate;
	__u8 flow_control;
} __packed;

static int wilc_open(struct hci_uart *hu)
{
	struct wilc_data *wdata;

	BT_DBG("hci_wilc: open");
	wdata = kzalloc(sizeof(*wdata), GFP_KERNEL);
	if (!wdata)
		return -ENOMEM;
	skb_queue_head_init(&wdata->txq);
	hu->priv = wdata;

	return 0;
}

static int wilc_close(struct hci_uart *hu)
{
	struct wilc_data *wdata = hu->priv;

	BT_DBG("hci_wilc: close");
	skb_queue_purge(&wdata->txq);
	kfree_skb(wdata->rx_skb);
	kfree(wdata);
	hu->priv = NULL;
	return 0;
}

static int wilc_flush(struct hci_uart *hu)
{
	struct wilc_data *wdata = hu->priv;

	BT_DBG("hci_wilc: flush");
	skb_queue_purge(&wdata->txq);
	return 0;
}

static const struct h4_recv_pkt wilc_bt_recv_pkts[] = {
	{ H4_RECV_ACL, .recv = hci_recv_frame },
	{ H4_RECV_SCO, .recv = hci_recv_frame },
	{ H4_RECV_EVENT, .recv = hci_recv_frame },
};

static int wilc_recv(struct hci_uart *hu, const void *data, int len)
{
	struct wilc_data *wdata = hu->priv;
	int err;

	if (!test_bit(HCI_UART_REGISTERED, &hu->flags))
		return -EUNATCH;
	wdata->rx_skb = h4_recv_buf(hu->hdev, wdata->rx_skb, data, len,
				    wilc_bt_recv_pkts,
				    ARRAY_SIZE(wilc_bt_recv_pkts));
	if (IS_ERR(wdata->rx_skb)) {
		err = PTR_ERR(wdata->rx_skb);
		bt_dev_err(hu->hdev, "Frame reassembly failed (%d)", err);
		wdata->rx_skb = NULL;
	}

	return len;
}

static int wilc_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct wilc_data *wdata = hu->priv;

	BT_DBG("hci_wilc: enqueue skb %pK", skb);
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);
	skb_queue_tail(&wdata->txq, skb);
	return 0;
}

static struct sk_buff *wilc_dequeue(struct hci_uart *hu)
{
	struct wilc_data *wdata = hu->priv;

	BT_DBG("hci_wilc: dequeue skb");
	return skb_dequeue(&wdata->txq);
}

static int _set_uart_settings(struct hci_uart *hu, unsigned int speed,
			      bool flow_control)
{
	struct hci_update_uart_param param;
	int ret;

	param.baudrate = cpu_to_le32(speed);
	param.flow_control = flow_control ? 1 : 0;
	ret = __hci_cmd_sync_status(hu->hdev, HCI_VENDOR_CMD_UPDATE_UART,
				    sizeof(param), &param, HCI_CMD_TIMEOUT);
	if (ret) {
		BT_ERR("Failed to update UART settings");
		return ret;
	}

	serdev_device_set_baudrate(hu->serdev, speed);
	serdev_device_set_flow_control(hu->serdev, flow_control);

	return 0;
}

static int wilc_set_baudrate(struct hci_uart *hu, unsigned int speed)
{
	struct wilc_adapter *wilc_adapter;

	BT_INFO("WILC uart settings update request: speed=%d", speed);
	wilc_adapter = serdev_device_get_drvdata(hu->serdev);

	return _set_uart_settings(hu, speed, wilc_adapter->flow_control);
}

static int check_firmware_running(struct hci_uart *hu)
{
	struct hci_rp_read_local_version *version;
	struct sk_buff *skb;
	int ret = 0;

	BT_DBG("Resetting bluetooth chip");
	ret = __hci_cmd_sync_status(hu->hdev, HCI_OP_RESET, 0, NULL,
				    HCI_CMD_TIMEOUT);
	if (ret) {
		BT_ERR("Can not reset wilc");
		return ret;
	}

	BT_DBG("Checking chip state");
	skb = __hci_cmd_sync(hu->hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_CMD_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("Error while checking bootrom");
		return PTR_ERR(skb);
	}

	if (skb->len != sizeof(struct hci_rp_read_local_version)) {
		BT_ERR("Can not read local version");
		return -1;
	}
	version = (struct hci_rp_read_local_version *)skb->data;
	BT_DBG("Status: 0x%1X, HCI version: 0x%1X", version->status,
	       version->hci_ver);
	kfree_skb(skb);
	if (version->hci_ver != HCI_VERSION_FIRMWARE) {
		BT_ERR("Bluetooth firmware is not running !");
		if (version->hci_ver == HCI_VERSION_BOOTROM)
			BT_WARN("Bootrom is running");
		return 1;
	}
	BT_DBG("Firmware is running");
	return 0;
}

static int wilc_setup(struct hci_uart *hu)
{
	struct wilc_adapter *wilc_adapter;
	int ret;

	BT_DBG("hci_wilc: setup");
	serdev_device_set_baudrate(hu->serdev, WILC_UART_DEFAULT_BAUDRATE);
	serdev_device_set_flow_control(hu->serdev, false);
	ret = check_firmware_running(hu);
	if (ret)
		return ret;

	BT_DBG("Updating firmware uart settings");

	wilc_adapter = serdev_device_get_drvdata(hu->serdev);
	ret = _set_uart_settings(&wilc_adapter->hu, WILC_UART_BAUDRATE, true);
	if (ret) {
		BT_ERR("Failed to reconfigure firmware uart settings");
		return ret;
	}
	wilc_adapter->flow_control = true;

	BT_INFO("Wilc successfully initialized");
	return ret;
}

static const struct hci_uart_proto wilc_bt_proto = {
	.id = HCI_UART_WILC,
	.name = "Microchip",
	.manufacturer = WILC_BT_UART_MANUFACTURER,
	.init_speed = WILC_UART_DEFAULT_BAUDRATE,
	.open = wilc_open,
	.close = wilc_close,
	.flush = wilc_flush,
	.recv = wilc_recv,
	.enqueue = wilc_enqueue,
	.dequeue = wilc_dequeue,
	.setup = wilc_setup,
	.set_baudrate = wilc_set_baudrate,
};

static int wilc_bt_serdev_probe(struct serdev_device *serdev)
{
	struct wilc_adapter *wilc_adapter;
	struct device_node *wlan_node;
	void *wlan = NULL;
	int ret;

	wilc_adapter = kzalloc(sizeof(*wilc_adapter), GFP_KERNEL);
	if (!wilc_adapter)
		return -ENOMEM;

	wlan_node = of_parse_phandle(serdev->dev.of_node, "wlan", 0);
	if (!wlan_node) {
		BT_ERR("Can not run wilc bluetooth without wlan node");
		ret = -EINVAL;
		goto exit_free_adapter;
	}

#if IS_ENABLED(CONFIG_WILC1000_SDIO)
	wlan = wilc_sdio_get_byphandle(wlan_node);
#endif
#if IS_ENABLED(CONFIG_WILC1000_SPI)
	if (!wlan || wlan == ERR_PTR(-EPROBE_DEFER))
		wlan = wilc_spi_get_byphandle(wlan_node);
#endif
	if (IS_ERR(wlan)) {
		pr_warn("Can not initialize bluetooth: %pe\n", wlan);
		ret = PTR_ERR(wlan);
		goto exit_put_wlan_node;
	}

	of_node_put(wlan_node);
	wilc_adapter->wlan_priv = wlan;
	ret = wilc_bt_init(wlan);
	if (ret) {
		pr_err("Failed to initialize bluetooth firmware (%d)\n", ret);
		goto exit_put_wlan;
	}

	wilc_adapter->dev = &serdev->dev;
	wilc_adapter->hu.serdev = serdev;
	wilc_adapter->flow_control = false;
	serdev_device_set_drvdata(serdev, wilc_adapter);
	ret = hci_uart_register_device(&wilc_adapter->hu, &wilc_bt_proto);
	if (ret) {
		dev_err(&serdev->dev, "Failed to register hci device");
		goto exit_deinit_bt;
	}

	dev_info(&serdev->dev, "WILC hci interface registered");
	return 0;

exit_deinit_bt:
	wilc_bt_shutdown(wlan);
exit_put_wlan:
	wilc_put(wlan);
exit_put_wlan_node:
	of_node_put(wlan_node);
exit_free_adapter:
	kfree(wilc_adapter);
	return ret;
}

static void wilc_bt_serdev_remove(struct serdev_device *serdev)
{
	struct wilc_adapter *wilc_adapter = serdev_device_get_drvdata(serdev);

	hci_uart_unregister_device(&wilc_adapter->hu);
	wilc_bt_shutdown(wilc_adapter->wlan_priv);
	wilc_put(wilc_adapter->wlan_priv);
	kfree(wilc_adapter);
}

static const struct of_device_id wilc_bt_of_match[] = {
	{ .compatible = "microchip,wilc3000-bt" },
	{},
};
MODULE_DEVICE_TABLE(of, wilc_bt_of_match);

static struct serdev_device_driver wilc_bt_serdev_driver = {
	.probe = wilc_bt_serdev_probe,
	.remove = wilc_bt_serdev_remove,
	.driver = {
		.name = "hci_uart_wilc",
		.of_match_table = of_match_ptr(wilc_bt_of_match),
	},
};

module_serdev_device_driver(wilc_bt_serdev_driver)
MODULE_AUTHOR("Alexis Lothoré <alexis.lothore@bootlin.com>");
MODULE_DESCRIPTION("Bluetooth HCI Uart for WILC devices");
MODULE_LICENSE("GPL");
