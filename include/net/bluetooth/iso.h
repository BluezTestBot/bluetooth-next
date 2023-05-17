/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2022 Intel Corporation
 * Copyright 2023 NXP
 */

#ifndef __ISO_H
#define __ISO_H

/* ISO defaults */
#define ISO_DEFAULT_MTU		251
#define ISO_MAX_NUM_BIS		0x1f

/* ISO socket broadcast address */
struct sockaddr_iso_bc {
	bdaddr_t	bc_bdaddr;
	__u8		bc_bdaddr_type;
	__u8		bc_sid;
	__u8		bc_num_bis;
	__u8		bc_bis[ISO_MAX_NUM_BIS];
};

/* ISO socket address */
struct sockaddr_iso {
	sa_family_t	iso_family;
	bdaddr_t	iso_bdaddr;
	__u8		iso_bdaddr_type;
	struct sockaddr_iso_bc iso_bc[];
};

struct iso_bis {
	__u16	handle;
	bool	assigned;
};

/* hdev BIG list entry */
struct iso_big {
	struct list_head	list;
	__u8			handle;
	__u8			num_bis;
	struct iso_bis		bis[ISO_MAX_NUM_BIS];
};

#endif /* __ISO_H */
