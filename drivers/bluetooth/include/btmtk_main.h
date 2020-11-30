/* SPDX-License-Identifier: GPL-2.0-or-later */
/**
 *  Copyright (c) 2018 MediaTek Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
#ifndef __BTMTK_MAIN_H__
#define __BTMTK_MAIN_H__
#include "btmtk_define.h"
#include "btmtk_chip_if.h"

#define DEFAULT_COUNTRY_TABLE_NAME "btPowerTable.dat"


//static inline struct sk_buff *mtk_add_stp(struct btmtk_dev *bdev, struct sk_buff *skb);

#define hci_dev_test_and_clear_flag(hdev, nr)  test_and_clear_bit((nr), (hdev)->dev_flags)

/* h4_recv */
#define hci_skb_pkt_type(skb) bt_cb((skb))->pkt_type
#define hci_skb_expect(skb) bt_cb((skb))->expect
#define hci_skb_opcode(skb) bt_cb((skb))->hci.opcode

/* HCI bus types */
#define HCI_VIRTUAL	0
#define HCI_USB		1
#define HCI_PCCARD	2
#define HCI_UART	3
#define HCI_RS232	4
#define HCI_PCI		5
#define HCI_SDIO	6
#define HCI_SPI		7
#define HCI_I2C		8
#define HCI_SMD		9

#define HCI_TYPE_SIZE	1

/* this for 7663 need download patch staus
 * 0:
 * patch download is not complete/BT get patch semaphore fail (WiFi get semaphore success)
 * 1:
 * patch download is complete
 * 2:
 * patch download is not complete/BT get patch semaphore success
 */
#define MT766X_PATCH_IS_DOWNLOAD_BY_OTHER 0
#define MT766X_PATCH_READY 1
#define MT766X_PATCH_NEED_DOWNLOAD 2

/* this for 79XX need download patch staus
 * 0:
 * patch download is not complete, BT driver need to download patch
 * 1:
 * patch is downloading by Wifi,BT driver need to retry until status = PATCH_READY
 * 2:
 * patch download is complete, BT driver no need to download patch
 */
#define PATCH_ERR -1
#define PATCH_NEED_DOWNLOAD 0
#define PATCH_IS_DOWNLOAD_BY_OTHER 1
#define PATCH_READY 2

/* 0:
 * using legacy wmt cmd/evt to download fw patch, usb/sdio just support 0 now
 * 1:
 * using DMA to download fw patch
 */
#define PATCH_DOWNLOAD_USING_WMT 0
#define PATCH_DOWNLOAD_USING_DMA 1

#define PATCH_DOWNLOAD_PHASE1_2_DELAY_TIME 1
#define PATCH_DOWNLOAD_PHASE1_2_RETRY 5
#define PATCH_DOWNLOAD_PHASE3_DELAY_TIME 20
#define PATCH_DOWNLOAD_PHASE3_RETRY 20

/* * delay and retrey for main_send_cmd */
#define WMT_DELAY_TIMES 100
#define DELAY_TIMES 20
#define RETRY_TIMES 20

/* Expected minimum supported interface */
#define BT_MCU_MINIMUM_INTERFACE_NUM	4

/* Bus event */
#define HIF_EVENT_PROBE		0
#define HIF_EVENT_DISCONNECT	1
#define HIF_EVENT_SUSPEND	2
#define HIF_EVENT_RESUME	3
#define HIF_EVENT_STANDBY	4
#define HIF_EVENT_SUBSYS_RESET	5
#define HIF_EVENT_WHOLE_CHIP_RESET	6
#define HIF_EVENT_FW_DUMP	7


#define CHAR2HEX_SIZE	4

/**
 * For chip reset pin
 */
#define RESET_PIN_SET_LOW_TIME		100

/* stpbtfwlog setting */
#define FWLOG_QUEUE_COUNT			(400 * BT_MCU_MINIMUM_INTERFACE_NUM)
#define FWLOG_ASSERT_QUEUE_COUNT		45000
#define FWLOG_BLUETOOTH_KPI_QUEUE_COUNT		400
#define HCI_MAX_COMMAND_SIZE			255
#define HCI_MAX_COMMAND_BUF_SIZE		(HCI_MAX_COMMAND_SIZE * 3)
#define HCI_MAX_ISO_SIZE	340

/* fwlog information define */
#define FWLOG_TYPE		0xF0
#define FWLOG_LEN_SIZE		2
#define FWLOG_TL_SIZE		(HCI_TYPE_SIZE + FWLOG_LEN_SIZE)
#define FWLOG_ATTR_TYPE_LEN	1
#define FWLOG_ATTR_LEN_LEN	1
#define FWLOG_ATTR_RX_LEN_LEN	2
#define FWLOG_ATTR_TL_SIZE	(FWLOG_ATTR_TYPE_LEN + FWLOG_ATTR_LEN_LEN)

#define FWLOG_HCI_IDX		0x00
#define FWLOG_DONGLE_IDX	0x01
#define FWLOG_TX		0x10
#define FWLOG_RX		0x11

/* total fwlog info len */
#define FWLOG_PRSV_LEN		32

/* bluetooth kpi */
#define KPI_WITHOUT_TYPE	0
#define COUNTRY_CODE_LEN	2


#define EDR_MIN		-32
#define EDR_MAX		20
#define EDR_MIN_LV9	13
#define BLE_MIN		-29
#define BLE_MAX		20
#define EDR_MIN_R1	-64
#define EDR_MAX_R1	40
#define EDR_MIN_LV9_R1	26
#define BLE_MIN_R1	-58
#define BLE_MAX_R1	40
#define EDR_MIN_R2	-128
#define EDR_MAX_R2	80
#define EDR_MIN_LV9_R2	52
#define BLE_MIN_R2	-116
#define BLE_MAX_R2	80

#define ERR_PWR		-9999

enum {
	RES_1 = 0,
	RES_DOT_5,
	RES_DOT_25
};

enum {
	CHECK_SINGLE_SKU_PWR_MODE	= 0,
	CHECK_SINGLE_SKU_EDR_MAX,
	CHECK_SINGLE_SKU_BLE,
	CHECK_SINGLE_SKU_BLE_2M,
	CHECK_SINGLE_SKU_BLE_LR_S2,
	CHECK_SINGLE_SKU_BLE_LR_S8,
	CHECK_SINGLE_SKU_ALL
};

enum {
	DISABLE_LV9 = 0,
	ENABLE_LV9
};

enum {
	DIFF_MODE_3DB = 0,
	DIFF_MODE_0DB
};

struct btmtk_cif_state {
	unsigned char ops_enter;
	unsigned char ops_end;
	unsigned char ops_error;
};

enum TX_TYPE {
	BTMTK_TX_CMD_FROM_DRV = 0,	/* send hci cmd and wmt cmd by driver */
	BTMTK_TX_ACL_FROM_DRV,	/* send acl pkt with load rompatch by driver */
	BTMTK_TX_PKT_FROM_HOST,	/* send pkt from host, include acl and hci */
};

/* Device node */
#if CFG_SUPPORT_MULTI_DEV_NODE
	#define BT_CHR_DEV	"BT_multi_chrdevfwlog"
	#define BT_DEV_NODE	"stpbt_multi_fwlog"
#else
	#define BT_CHR_DEV	"BT_chrdevfwlog"
	#define BT_DEV_NODE	"stpbtfwlog"
#endif

struct bt_power_setting {
	int8_t EDR_Max;
	int8_t LV9;
	int8_t DM;
	int8_t IR;
	int8_t BLE_1M;
	int8_t BLE_2M;
	int8_t BLE_LR_S2;
	int8_t BLE_LR_S8;
	char country_code[3];
};

struct btmtk_fops_fwlog {
	dev_t g_devIDfwlog;
	struct cdev BT_cdevfwlog;
	wait_queue_head_t fw_log_inq;
	struct sk_buff_head fwlog_queue;
	struct class *pBTClass;
	struct device *pBTDevfwlog;
	spinlock_t fwlog_lock;
	u8 btmtk_bluetooth_kpi;
};

enum {
	BTMTK_DONGLE_STATE_UNKNOWN,
	BTMTK_DONGLE_STATE_POWER_ON,
	BTMTK_DONGLE_STATE_POWER_OFF,
	BTMTK_DONGLE_STATE_ERROR,
};

enum {
	HW_ERR_NONE = 0x00,
	HW_ERR_CODE_CHIP_RESET = 0xF0,
	HW_ERR_CODE_USB_DISC = 0xF1,
	HW_ERR_CODE_CORE_DUMP = 0xF2,
	HW_ERR_CODE_POWER_ON = 0xF3,
	HW_ERR_CODE_POWER_OFF = 0xF4,
	HW_ERR_CODE_SET_SLEEP_CMD = 0xF5,
	HW_ERR_CODE_RESET_STACK_AFTER_WOBLE = 0xF6,
};

/* Please keep sync with btmtk_set_state function */
enum {
	/* BTMTK_STATE_UNKNOWN = 0, */
	BTMTK_STATE_INIT = 1,
	BTMTK_STATE_DISCONNECT,
	BTMTK_STATE_PROBE,
	BTMTK_STATE_WORKING,
	BTMTK_STATE_SUSPEND,
	BTMTK_STATE_RESUME,
	BTMTK_STATE_FW_DUMP,
	BTMTK_STATE_STANDBY,
	BTMTK_STATE_SUBSYS_RESET,
};

/* Please keep sync with btmtk_fops_set_state function */
enum {
	/* BTMTK_FOPS_STATE_UNKNOWN = 0, */
	BTMTK_FOPS_STATE_INIT = 1,
	BTMTK_FOPS_STATE_OPENING,	/* during opening */
	BTMTK_FOPS_STATE_OPENED,	/* open in fops_open */
	BTMTK_FOPS_STATE_CLOSING,	/* during closing */
	BTMTK_FOPS_STATE_CLOSED,	/* closed */
};

enum {
	BTMTK_EVENT_COMPARE_STATE_UNKNOWN,
	BTMTK_EVENT_COMPARE_STATE_NOTHING_NEED_COMPARE,
	BTMTK_EVENT_COMPARE_STATE_NEED_COMPARE,
	BTMTK_EVENT_COMPARE_STATE_COMPARE_SUCCESS,
};

struct h4_recv_pkt {
	u8  type;	/* Packet type */
	u8  hlen;	/* Header length */
	u8  loff;	/* Data length offset in header */
	u8  lsize;	/* Data length field size */
	u16 maxlen;	/* Max overall packet length */
	int (*recv)(struct hci_dev *hdev, struct sk_buff *skb);
};

#pragma pack(1)
struct _PATCH_HEADER {
	u8 ucDateTime[16];
	u8 ucPlatform[4];
	u16 u2HwVer;
	u16 u2SwVer;
	u32 u4MagicNum;
};

struct _Global_Descr {
	u32 u4PatchVer;
	u32 u4SubSys;
	u32 u4FeatureOpt;
	u32 u4SectionNum;
};

struct _Section_Map {
	u32 u4SecType;
	u32 u4SecOffset;
	u32 u4SecSize;
	union {
		u32 u4SecSpec[SECTION_SPEC_NUM];
		struct {
			u32 u4DLAddr;
			u32 u4DLSize;
			u32 u4SecKeyIdx;
			u32 u4AlignLen;
			u32 u4SecType;
			u32 u4DLModeCrcType;
			u32 u4Crc;
			u32 reserved[6];
		} bin_info_spec;
	};
};
#pragma pack()

#define H4_RECV_ACL \
	.type = HCI_ACLDATA_PKT, \
	.hlen = HCI_ACL_HDR_SIZE, \
	.loff = 2, \
	.lsize = 2, \
	.maxlen = HCI_MAX_FRAME_SIZE \

#define H4_RECV_SCO \
	.type = HCI_SCODATA_PKT, \
	.hlen = HCI_SCO_HDR_SIZE, \
	.loff = 2, \
	.lsize = 1, \
	.maxlen = HCI_MAX_SCO_SIZE

#define H4_RECV_EVENT \
	.type = HCI_EVENT_PKT, \
	.hlen = HCI_EVENT_HDR_SIZE, \
	.loff = 1, \
	.lsize = 1, \
	.maxlen = HCI_MAX_EVENT_SIZE


struct btmtk_dev {
	struct hci_dev	*hdev;
	unsigned long	hdev_flags;
	unsigned long	flags;
	void *intf_dev;
	void *cif_dev;

	struct work_struct	work;
	struct work_struct	waker;
	struct work_struct	reset_waker;

	int	recv_evt_len;
	int	tx_in_flight;
	spinlock_t	txlock;
	spinlock_t	rxlock;

	struct sk_buff	*evt_skb;
	struct sk_buff	*sco_skb;

	/* For ble iso packet size */
	int iso_threshold;

	unsigned int	sco_num;
	int	isoc_altsetting;

	int	suspend_count;

	/* For tx queue */
	unsigned long	tx_state;

	/* For rx queue */
	struct workqueue_struct	*workqueue;
	struct sk_buff_head	rx_q;
	struct work_struct	rx_work;
	struct sk_buff		*rx_skb;

	wait_queue_head_t	p_wait_event_q;

	unsigned int	subsys_reset;
	unsigned int	chip_reset;
	unsigned char	*rom_patch_bin_file_name;
	unsigned int	chip_id;
	unsigned int	flavor;
	unsigned int	fw_version;
	unsigned char	dongle_index;
	unsigned char	power_state;
	unsigned char	fops_state;
	unsigned char	interface_state;
	struct btmtk_cif_state *cif_state;

	/* io buffer for usb control transfer */
	unsigned char	*io_buf;

	unsigned char	*setting_file;
	unsigned char	*woble_setting_file_name;
	unsigned int	woble_setting_len;

	struct fw_cfg_struct	woble_setting_apcf[WOBLE_SETTING_COUNT];
	struct fw_cfg_struct	woble_setting_apcf_fill_mac[WOBLE_SETTING_COUNT];
	struct fw_cfg_struct	woble_setting_apcf_fill_mac_location[WOBLE_SETTING_COUNT];

	struct fw_cfg_struct	woble_setting_radio_off;
	struct fw_cfg_struct	woble_setting_wakeup_type;
	struct fw_cfg_struct	woble_setting_radio_off_status_event;
	/* complete event */
	struct fw_cfg_struct	woble_setting_radio_off_comp_event;

	struct fw_cfg_struct	woble_setting_radio_on;
	struct fw_cfg_struct	woble_setting_radio_on_status_event;
	struct fw_cfg_struct	woble_setting_radio_on_comp_event;

	/* set apcf after resume(radio on) */
	struct fw_cfg_struct	woble_setting_apcf_resume[WOBLE_SETTING_COUNT];
	unsigned char	bdaddr[BD_ADDRESS_SIZE];
	unsigned int	woble_need_trigger_coredump;
	unsigned int	woble_need_set_radio_off_in_probe;

	unsigned char		*bt_cfg_file_name;
	struct bt_cfg_struct	bt_cfg;

	/* Foe Woble eint */
	unsigned int wobt_irq;
	int wobt_irqlevel;
	atomic_t irq_enable_count;
	struct input_dev *WoBLEInputDev;

	u8 opcode_usr[2];
};

typedef int (*cif_open_ptr)(struct hci_dev *hdev);
typedef int (*cif_close_ptr)(struct hci_dev *hdev);
typedef int (*cif_reg_read_ptr)(struct btmtk_dev *bdev, u32 reg, u32 *val);
typedef int (*cif_reg_write_ptr)(struct btmtk_dev *bdev, u32 reg, u32 val);
typedef int (*cif_send_cmd_ptr)(struct btmtk_dev *bdev, struct sk_buff *skb,
		int delay, int retry, int pkt_type);
typedef int (*cif_send_and_recv_ptr)(struct btmtk_dev *bdev,
		struct sk_buff *skb,
		const uint8_t *event, const int event_len,
		int delay, int retry, int pkt_type);
typedef int (*cif_event_filter_ptr)(struct btmtk_dev *bdev, struct sk_buff *skb);
typedef int (*cif_subsys_reset_ptr)(struct btmtk_dev *bdev);
typedef int (*cif_whole_reset_ptr)(struct btmtk_dev *bdev);
typedef void (*cif_chip_reset_notify_ptr)(struct btmtk_dev *bdev);
typedef void (*cif_mutex_lock_ptr)(struct btmtk_dev *bdev);
typedef void (*cif_mutex_unlock_ptr)(struct btmtk_dev *bdev);
typedef void (*cif_open_done_ptr)(struct btmtk_dev *bdev);
typedef int (*cif_dl_dma_ptr)(struct btmtk_dev *bdev, u8 *image,
		u8 *fwbuf, int section_dl_size, int section_offset);

struct hif_hook_ptr {
	cif_open_ptr			open;
	cif_close_ptr			close;
	cif_reg_read_ptr		reg_read;
	cif_reg_write_ptr		reg_write;
	cif_send_cmd_ptr		send_cmd;
	cif_send_and_recv_ptr		send_and_recv;
	cif_event_filter_ptr		event_filter;
	cif_subsys_reset_ptr		subsys_reset;
	cif_whole_reset_ptr		whole_reset;
	cif_chip_reset_notify_ptr	chip_reset_notify;
	cif_mutex_lock_ptr		cif_mutex_lock;
	cif_mutex_unlock_ptr		cif_mutex_unlock;
	cif_open_done_ptr		open_done;
	cif_dl_dma_ptr			dl_dma;
};

struct btmtk_main_info {
	u8 reset_stack_flag;
	struct wakeup_source *fwdump_ws;
	struct wakeup_source *woble_ws;
	struct wakeup_source *eint_ws;
	struct hif_hook_ptr hif_hook;
	struct bt_power_setting PWS;
};

static inline int is_mt7922(u32 chip_id)
{
	chip_id &= 0xFFFF;
	if (chip_id == 0x7922)
		return 1;
	return 0;
}

static inline int is_mt7961(u32 chip_id)
{
	chip_id &= 0xFFFF;
	if (chip_id == 0x7961)
		return 1;
	return 0;
}

static inline int is_mt7663(u32 chip_id)
{
	chip_id &= 0xFFFF;
	if (chip_id == 0x7663)
		return 1;
	return 0;
}

static inline int is_support_unify_woble(struct btmtk_dev *bdev)
{
	if (bdev->bt_cfg.support_unify_woble) {
		if (is_mt7922(bdev->chip_id) ||
		is_mt7961(bdev->chip_id) || is_mt7663(bdev->chip_id))
			return 1;
		else
			return 0;
	} else {
		return 0;
	}
}

int btmtk_get_chip_state(struct btmtk_dev *bdev);
void btmtk_set_chip_state(struct btmtk_dev *bdev, int new_state);
int btmtk_allocate_hci_device(struct btmtk_dev *bdev, int hci_bus_type);
void btmtk_free_hci_device(struct btmtk_dev *bdev, int hci_bus_type);
int btmtk_register_hci_device(struct btmtk_dev *bdev);
int btmtk_deregister_hci_device(struct btmtk_dev *bdev);
int btmtk_recv(struct hci_dev *hdev, const u8 *data, size_t count);
int btmtk_recv_event(struct hci_dev *hdev, struct sk_buff *skb);
int btmtk_recv_acl(struct hci_dev *hdev, struct sk_buff *skb);
int btmtk_send_init_cmds(struct btmtk_dev *hdev);
int btmtk_send_deinit_cmds(struct btmtk_dev *hdev);
int btmtk_main_send_cmd(struct btmtk_dev *bdev, const uint8_t *cmd,
		const int cmd_len, const uint8_t *event, const int event_len, int delay,
		int retry, int pkt_type);
int btmtk_send_wmt_reset(struct btmtk_dev *hdev);
int btmtk_send_wmt_power_on_cmd(struct btmtk_dev *hdev);
int btmtk_send_wmt_power_off_cmd(struct btmtk_dev *hdev);
int btmtk_woble_suspend(struct btmtk_dev *bdev);
int btmtk_woble_resume(struct btmtk_dev *bdev);
int btmtk_handle_leaving_WoBLE_state(struct btmtk_dev *bdev);
int btmtk_handle_entering_WoBLE_state(struct btmtk_dev *bdev);
int btmtk_load_code_from_setting_files(char *setting_file_name,
		struct device *dev, u32 *code_len, struct btmtk_dev *bdev);
int btmtk_load_woble_setting(char *bin_name,
		struct device *dev, u32 *code_len, struct btmtk_dev *bdev);
int btmtk_load_rom_patch_766x(struct btmtk_dev *hdev);
int btmtk_uart_send_wakeup_cmd(struct hci_dev *hdev);
int btmtk_uart_send_set_uart_cmd(struct hci_dev *hdev);
int btmtk_load_rom_patch(struct btmtk_dev *bdev);
struct btmtk_dev *btmtk_get_dev(void);
void btmtk_release_dev(struct btmtk_dev *bdev);
struct btmtk_dev *btmtk_allocate_dev_memory(struct device *dev);
void btmtk_free_dev_memory(struct device *dev, struct btmtk_dev *bdev);
void btmtk_reset_waker(struct work_struct *work);
void btmtk_initialize_cfg_items(struct btmtk_dev *bdev);
bool btmtk_load_bt_cfg(char *cfg_name, struct device *dev, struct btmtk_dev *bdev);
struct btmtk_main_info *btmtk_get_main_info(void);
int btmtk_reset_power_on(struct btmtk_dev *bdev);
void btmtk_send_hw_err_to_host(struct btmtk_dev *bdev);
void btmtk_free_setting_file(struct btmtk_dev *bdev);
/** file_operations: stpbtfwlog */
int btmtk_fops_openfwlog(struct inode *inode, struct file *file);
int btmtk_fops_closefwlog(struct inode *inode, struct file *file);
ssize_t btmtk_fops_readfwlog(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t btmtk_fops_writefwlog(struct file *filp, const char __user *buf,
			size_t count, loff_t *f_pos);
unsigned int btmtk_fops_pollfwlog(struct file *filp, poll_table *wait);
long btmtk_fops_unlocked_ioctlfwlog(struct file *filp, unsigned int cmd, unsigned long arg);

/* Auto enable picus */
int btmtk_picus_enable(struct btmtk_dev *bdev);
int btmtk_picus_disable(struct btmtk_dev *bdev);

void btmtk_hci_snoop_save_cmd(u32 len, u8 *buf);
void btmtk_hci_snoop_save_event(u32 len, u8 *buf);
void btmtk_hci_snoop_save_adv_event(u32 len, u8 *buf);
void btmtk_hci_snoop_save_acl(u32 len, u8 *buf);
void btmtk_hci_snoop_print(u32 len, const u8 *buf);
unsigned long btmtk_kallsyms_lookup_name(const char *name);
void btmtk_woble_wake_lock(struct btmtk_dev *bdev);
void btmtk_woble_wake_unlock(struct btmtk_dev *bdev);
void btmtk_reg_hif_hook(struct hif_hook_ptr *hook);
int btmtk_main_cif_initialize(struct btmtk_dev *bdev, int hci_bus);
void btmtk_main_cif_uninitialize(struct btmtk_dev *bdev, int hci_bus);
int btmtk_main_woble_initialize(struct btmtk_dev *bdev);
int btmtk_main_cif_disconnect_notify(struct btmtk_dev *bdev, int hci_bus);
int btmtk_cif_send_calibration(struct btmtk_dev *bdev);
int btmtk_send_assert_cmd(struct btmtk_dev *bdev);
int btmtk_efuse_read(struct btmtk_dev *bdev, u16 addr, u8 *value);

void btmtk_set_country_code_from_wifi(char *code);

#endif /* __BTMTK_MAIN_H__ */
