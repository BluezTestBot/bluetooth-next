// SPDX-License-Identifier: GPL-2.0

#include <linux/dev_printk.h>
#include <linux/mutex.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <net/wilc.h>
#include "netdev.h"
#include "wlan_if.h"
#include "wlan.h"

#define	FW_WILC3000_BLE		"mchp/wilc3000_ble_firmware.bin"

static int wilc_bt_power_down(struct wilc *wilc)
{
	int ret;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	ret = wilc->hif_func->hif_rmw_reg(wilc, GLOBAL_MODE_CONTROL, BIT(1), 0);
	if (ret) {
		dev_err(wilc->dev, "Failed to disable BT mode\n");
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
		return ret;
	}

	ret = wilc->hif_func->hif_rmw_reg(wilc, COE_AUTO_PS_ON_NULL_PKT,
					  BIT(30), 0);
	if (ret) {
		dev_err(wilc->dev, "Failed to disable awake coexistence null frames\n");
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
		return ret;
	}

	ret = wilc->hif_func->hif_rmw_reg(wilc, COE_AUTO_PS_OFF_NULL_PKT,
					  BIT(30), 0);
	if (ret) {
		dev_err(wilc->dev, "Failed to disable doze coexistence null frames\n");
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
		return ret;
	}

	ret = wilc->hif_func->hif_rmw_reg(wilc, PWR_SEQ_MISC_CTRL, BIT(29), 0);
	if (ret) {
		dev_err(wilc->dev, "Failed to disable bluetooth wake-up\n");
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
		return ret;
	}
	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

	if (!wilc->initialized) {
		acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
		ret = wilc->hif_func->hif_deinit(wilc);
		release_bus(wilc, WILC_BUS_RELEASE_ONLY);
	}

	return 0;
}

static int wilc_bt_power_up(struct wilc *wilc)
{
	int ret;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);
	if (!wilc->initialized) {
		ret = wilc->hif_func->hif_rmw_reg(wilc, COE_AUTO_PS_ON_NULL_PKT,
						  BIT(30), 0);
		if (ret) {
			dev_err(wilc->dev, "Failed to disable awake coexistence null frames\n");
			goto fail;
		}

		ret = wilc->hif_func->hif_rmw_reg(wilc,
				COE_AUTO_PS_OFF_NULL_PKT, BIT(30), 0);
		if (ret) {
			dev_err(wilc->dev, "Failed to disable awake coexistence null frames\n");
			goto fail;
		}
	}

	ret = wilc->hif_func->hif_rmw_reg(wilc, PWR_SEQ_MISC_CTRL, BIT(29),
					  BIT(29));
	if (ret) {
		dev_err(wilc->dev, "Failed to enable bluetooth wake-up\n");
		goto fail;
	}
	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

	return 0;

fail:
	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
	wilc_bt_power_down(wilc);
	return ret;
}

static int wilc_bt_firmware_download(struct wilc *wilc)
{
	const struct firmware *wilc_bt_firmware;
	u32 addr, size, size2, blksz;
	size_t buffer_size;
	const u8 *buffer;
	u8 *dma_buffer;
	u32 offset;
	int ret = 0;

	dev_info(wilc->dev, "Bluetooth firmware: %s\n", FW_WILC3000_BLE);
	ret = request_firmware(&wilc_bt_firmware, FW_WILC3000_BLE, wilc->dev);
	if (ret) {
		dev_err(wilc->dev, "%s - firmware not available. Skip!\n",
			FW_WILC3000_BLE);
		return ret;
	}

	buffer = wilc_bt_firmware->data;
	buffer_size = (size_t)wilc_bt_firmware->size;
	if (buffer_size <= 0) {
		dev_err(wilc->dev, "Firmware size = 0!\n");
		ret = -EINVAL;
		goto out_release_firmware;
	}

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	ret = wilc->hif_func->hif_write_reg(wilc, WILC_BT_BOOTROM_CONFIGURATION,
					    WILC_BT_BOOTROM_DISABLE);
	if (ret) {
		dev_err(wilc->dev, "Failed to disable BT bootrom\n");
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
		goto out_release_firmware;
	}

	ret = wilc->hif_func->hif_rmw_reg(wilc, WILC_BT_RESET_MUX,
					  WILC_BT_ENABLE_GLOBAL_RESET,
					  WILC_BT_ENABLE_GLOBAL_RESET);
	if (ret) {
		dev_err(wilc->dev, "Failed to configure reset for BT CPU\n");
		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);
		goto out_release_firmware;
	}

	ret = wilc->hif_func->hif_rmw_reg(wilc, WILC_BT_CPU_CONFIGURATION,
					  WILC_BT_CPU_ENABLE,
					  WILC_BT_CPU_ENABLE);
	if (!ret)
		ret = wilc->hif_func->hif_rmw_reg(wilc,
				WILC_BT_CPU_CONFIGURATION,
				WILC_BT_CPU_ENABLE, 0);
	if (ret) {
		dev_err(wilc->dev, "Failed to disable BT CPU\n");
		goto out_release_firmware;
	}

	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

	/* blocks of sizes > 512 causes the wifi to hang! */
	blksz = (1ul << 9);
	/* Allocate a DMA coherent buffer. */
	dma_buffer = kmalloc(blksz, GFP_KERNEL);
	if (!dma_buffer) {
		ret = -ENOMEM;
		dev_err(wilc->dev,
			"Can't allocate buffer for BT firmware download\n");
		goto out_free_buffer;
	}
	dev_info(wilc->dev, "Downloading BT firmware size = %zu ...\n",
		 buffer_size);

	offset = 0;
	addr = WILC_BT_IRAM;
	size = buffer_size;
	offset = 0;

	while (((int)size) && (offset < buffer_size)) {
		if (size <= blksz)
			size2 = size;
		else
			size2 = blksz;

		/* Copy firmware into a DMA coherent buffer */
		memcpy(dma_buffer, &buffer[offset], size2);

		acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

		ret = wilc->hif_func->hif_block_tx(wilc, addr, dma_buffer,
						   size2);

		release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

		if (ret)
			break;

		addr += size2;
		offset += size2;
		size -= size2;
	}

	if (ret) {
		dev_err(wilc->dev, "Failed to download BT firmware\n");
		goto out_free_buffer;
	}
	dev_info(wilc->dev, "Finished downloading firmware\n");

out_free_buffer:
	kfree(dma_buffer);
out_release_firmware:
	release_firmware(wilc_bt_firmware);
return ret;
}

static int wilc_bt_start(struct wilc *wilc)
{
	int ret;

	acquire_bus(wilc, WILC_BUS_ACQUIRE_AND_WAKEUP);

	dev_info(wilc->dev, "Starting BT firmware\n");
	/*
	 * Write the firmware download complete magic at
	 * location 0xFFFF000C (Cortus map) or C000C (AHB map).
	 * This will let the boot-rom code execute from RAM.
	 */
	ret = wilc->hif_func->hif_write_reg(wilc, WILC_BT_FW_MAGIC_LOC,
					    WILC_BT_FW_MAGIC);
	if (ret) {
		dev_err(wilc->dev, "Failed to write BT firmware magic\n");
		return ret;
	}

	ret = wilc->hif_func->hif_rmw_reg(wilc, WILC_BT_CPU_CONFIGURATION,
					  WILC_BT_CPU_BOOT, 0);
	if (ret) {
		dev_err(wilc->dev, "Failed to disable BT CPU");
		return ret;
	}

	msleep(100);

	ret = wilc->hif_func->hif_rmw_reg(wilc, WILC_BT_CPU_CONFIGURATION,
					  WILC_BT_CPU_BOOT, WILC_BT_CPU_BOOT);
	if (ret) {
		dev_err(wilc->dev, "Failed to enable BT CPU");
		return ret;
	}
	/* An additional wait to give BT firmware time to do
	 * CPLL update as the time measured since the start of
	 * BT FW till the end of function "rf_nmi_init_tuner"
	 * was 71.2 ms
	 */
	msleep(100);

	dev_info(wilc->dev, "BT Start Succeeded\n");

	release_bus(wilc, WILC_BUS_RELEASE_ALLOW_SLEEP);

	return 0;
}

int wilc_bt_init(void *wilc_wl_priv)
{
	struct wilc *wilc = (struct wilc *)wilc_wl_priv;
	int ret;

	if (!wilc->hif_func->hif_is_init(wilc)) {
		dev_info(wilc->dev, "Initializing bus before starting BT");
		acquire_bus(wilc, WILC_BUS_ACQUIRE_ONLY);
		ret = wilc->hif_func->hif_init(wilc, false);
		release_bus(wilc, WILC_BUS_RELEASE_ONLY);
		if (ret)
			return ret;
	}

	mutex_lock(&wilc->radio_fw_start);
	ret = wilc_bt_power_up(wilc);
	if (ret) {
		dev_err(wilc->dev, "Error powering up bluetooth chip\n");
		goto hif_deinit;
	}
	ret = wilc_bt_firmware_download(wilc);
	if (ret) {
		dev_err(wilc->dev, "Error downloading firmware\n");
		goto power_down;
	}
	ret = wilc_bt_start(wilc);
	if (ret) {
		dev_err(wilc->dev, "Error starting bluetooth firmware\n");
		goto power_down;
	}
	mutex_unlock(&wilc->radio_fw_start);
	return 0;

power_down:
	wilc_bt_power_down(wilc);
hif_deinit:
	mutex_unlock(&wilc->radio_fw_start);
	if (!wilc->initialized)
		wilc->hif_func->hif_deinit(wilc);
	return ret;
}
EXPORT_SYMBOL(wilc_bt_init);

int wilc_bt_shutdown(void *wilc_wl_priv)
{
	struct wilc *wilc = (struct wilc *)wilc_wl_priv;
	int ret;

	mutex_lock(&wilc->radio_fw_start);
	ret = wilc->hif_func->hif_rmw_reg(wilc, WILC_BT_CPU_CONFIGURATION,
					  WILC_BT_CPU_ENABLE, 0);
	if (ret)
		dev_warn(wilc->dev, "Failed to disable BT CPU\n");
	if (wilc_bt_power_down(wilc))
		dev_warn(wilc->dev, "Failed to power down BT CPU\n");
	if (!wilc->initialized)
		wilc->hif_func->hif_deinit(wilc);
	mutex_unlock(&wilc->radio_fw_start);

	return 0;
}
EXPORT_SYMBOL(wilc_bt_shutdown);
