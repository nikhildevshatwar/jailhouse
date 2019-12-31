/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (c) 2019 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors:
 *  Nikhil Devshatwar <nikhil.nd@ti.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef _JAILHOUSE_REGMAP_H
#define _JAILHOUSE_REGMAP_H

#include <jailhouse/types.h>
#include <asm/mmio.h>
#include <jailhouse/cell-config.h>

struct cell;

/**
 * @defgroup REGMAP Regmap subsystem
 *
 * This subsystem provides interpretation and handling of intercepted
 * register accesses performed by cells.
 *
 * @{
 */

#define JAILHOUSE_REGMAP_WORDS		8
#define JAILHOUSE_REGMAP_BITS		(JAILHOUSE_REGMAP_WORDS * 32)

/** Register map description */
struct reg_map_data {
	/** Reference to regmap defined in config */
	const struct jailhouse_regmap *info;
	/** Owning cell */
	struct cell *cell;
	/** virt address where this regmap is mapped */
	void *map_base;
	/** Ownership details for each register */
	u32 reg_bitmap[8];
};

/** @} REGMAP */
#endif /* !_JAILHOUSE_REGMAP_H */
