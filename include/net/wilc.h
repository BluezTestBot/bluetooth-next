/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __WILC_HEADER_H
#define __WILC_HEADER_H

#include <linux/of.h>

#if defined(CONFIG_WILC1000_SDIO) || defined(CONFIG_WILC1000_SDIO_MODULE)
void *wilc_sdio_get_byphandle(struct device_node *wlan_node);
#endif
#if defined(CONFIG_WILC1000_SPI) || defined(CONFIG_WILC1000_SPI_MODULE)
void *wilc_spi_get_byphandle(struct device_node *wlan_node);
#endif
void wilc_put(void *wilc_wl_priv);

int wilc_bt_init(void *wilc_wl_priv);
int wilc_bt_shutdown(void *wilc_wl_priv);

#endif
