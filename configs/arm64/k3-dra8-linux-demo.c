/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Configuration for Linux inmate on DRA8 based platforms
 * 1 CPU, 512MB RAM, 1 serial port
 *
 * Copyright (c) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Authors:
 *  Lokesh Vutla <lokeshvutla@ti.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <jailhouse/types.h>
#include <jailhouse/cell-config.h>
#include "k3-cpus.h"

#define ARRAY_SIZE(a) sizeof(a) / sizeof(a[0])

#ifndef CONFIG_INMATE_BASE
#define CONFIG_INMATE_BASE 0x0000000
#endif

struct {
	struct jailhouse_cell_desc cell;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[3];
	struct jailhouse_irqchip irqchips[1];
} __attribute__((packed)) config = {
	.cell = {
		.signature = JAILHOUSE_CELL_DESC_SIGNATURE,
		.revision = JAILHOUSE_CONFIG_REVISION,
		.name = "linux-inmate-demo",
		.flags = JAILHOUSE_CELL_PASSIVE_COMMREG|JAILHOUSE_CELL_DEBUG_CONSOLE,

		.cpu_set_size = sizeof(config.cpus),
		.num_memory_regions = ARRAY_SIZE(config.mem_regions),
		.num_irqchips = ARRAY_SIZE(config.irqchips),
		.pio_bitmap_size = 0,
		.num_pci_devices = 0,
		.cpu_reset_address = 0x0,
	},

	.cpus = {
		K3_CORE1,
	},

	.mem_regions = {
		/* UART 3 */ {
			.phys_start = 0x02810000,
			.virt_start = 0x02810000,
			.size = 0x10000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_IO,
		},
		/* RAM load */ {
			.phys_start = 0xFFFF0000,
			.virt_start = 0x0,
			.size = 0x10000,	/* 64KB */
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_DMA |
				JAILHOUSE_MEM_LOADABLE,
		},
		/* RAM load */ {
			.phys_start = 0xE0000000,
			.virt_start = 0x80000000,
			.size = 0x1fff0000,	/* (512MB - 64KB) */
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
				JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_DMA |
				JAILHOUSE_MEM_LOADABLE,
		},
	},

	.irqchips = {
		{
			.address = 0x01800000,
			.pin_base = 160,
			.pin_bitmap = {
			/*
			 * offset = (SPI_NR + 32 - base) / 32
			 * bit = (SPI_NR + 32 - base) % 32
			 */
			0, 0, 0x2, 0,
			},
		},
	}
};
