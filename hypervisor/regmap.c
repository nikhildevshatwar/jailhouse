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

#include <jailhouse/cell.h>
#include <jailhouse/control.h>
#include <jailhouse/paging.h>
#include <jailhouse/printk.h>
#include <jailhouse/unit.h>
#include <jailhouse/percpu.h>
#include <jailhouse/regmap.h>

static inline bool regmap_is_enabled(struct reg_map_data *regmap, int reg)
{
	u32 idx, mask;

	idx = reg / 32;
	mask = 1 << (reg % 32);

	return regmap->reg_bitmap[idx] & mask ? 1 : 0;
}

static inline void regmap_enable(struct reg_map_data *regmap, int reg)
{
	u32 idx, mask;

	idx = reg / 32;
	mask = 1 << (reg % 32);

	regmap->reg_bitmap[idx] |= mask;
}

static inline void regmap_disable(struct reg_map_data *regmap, int reg)
{
	u32 idx, mask;

	idx = reg / 32;
	mask = 1 << (reg % 32);

	regmap->reg_bitmap[idx] &= ~mask;
}

/**
 * Find the regmap which degines the ownership bitmap for
 * the register address provided.
 *
 * @param cell		Cell in which to search.
 * @param addr		Register address to match
 * @param idx		Pointer to index, populated with index of register in
 *			the matching regmap
 *
 * @return Valid reg_map_data or NULL when not found.
 */
static struct reg_map_data *cell_get_regmap(struct cell *cell,
	unsigned long addr, unsigned int *idx)
{
	const struct jailhouse_regmap *info;
	struct reg_map_data *regmap;
	unsigned long start, end;
	u32 i;

	for (i = 0; i < cell->config->num_regmaps; i++) {
		regmap = &cell->regmap[i];
		info = regmap->info;
		start = (unsigned long)info->reg_base;
		end = (unsigned long)start + info->reg_size * info->reg_count;

		if (addr < start || addr >= end)
			continue;

		*idx = (addr - info->reg_base) / info->reg_size;
		return regmap;
	}
	return NULL;
}

/**
 * Handle emulation of regmap access as per permission bitmap
 * Check regmap access permissions and ownership
 * Based on that, allow or forbid the MMIOs access to register
 *
 * @param arg		Private argument, reg_map_data.
 * @param mmio		describes the mmio access which caused the fault
 *
 * @return		MMIO_HANDLED if the access is as per regmap description,
 *			MMIO_ERROR if it violates some of the permissions,
 */
static enum mmio_result regmap_handler(void *arg, struct mmio_access *mmio)
{
	struct reg_map_data *regmap = (struct reg_map_data *)arg;
	const struct jailhouse_regmap *info;
	unsigned int idx;

	info = regmap->info;
	idx = mmio->address / info->reg_size;

	if (mmio->is_write) {
		if ((info->flags & JAILHOUSE_MEM_WRITE) == 0)
			return MMIO_ERROR;
	} else {
		if ((info->flags & JAILHOUSE_MEM_READ) == 0)
			return MMIO_ERROR;
	}

	if (regmap_is_enabled(regmap, idx)) {
		mmio_perform_access(regmap->map_base, mmio);
		return MMIO_HANDLED;
	}  else {
		printk("MMIO access disabled\n");
		return MMIO_ERROR;
	}
}

/**
 * Modify root_cell's bitmap to (un)mask the registers defined in inmate cell.
 * Ignore if the root cell does not describe the regmap used by inmate
 * Handles the case where root cell describes the registers using
 * different address range
 *
 * @param cell		inmate cell handle.
 * @param regmap	register (un)map to be removed from root_cell.
 * @param map		true to map the regmap, false to unmap.
 *
 * @return 0 on successfully (un)mapping the regmap.
 */
static int regmap_modify_root(struct cell *cell, struct reg_map_data *regmap,
		bool map)
{
	const struct jailhouse_regmap *info = regmap->info;
	struct reg_map_data *root_regmap = NULL;
	unsigned long long addr;
	u32 reg, idx;

	if (cell == &root_cell)
		return 0;
	if (info->flags & JAILHOUSE_MEM_ROOTSHARED)
		return 0;

	for (reg = 0; reg < info->reg_count; reg++) {

		addr = info->reg_base + reg * info->reg_size;
		if (!root_regmap) {
			root_regmap = cell_get_regmap(&root_cell, addr, &idx);
			if (!root_regmap)
				continue;
		}

		if (regmap_is_enabled(regmap, reg)) {
			if (map) {
				regmap_enable(root_regmap, idx);

			/* For unmapping, ensure that its mapped in root cell regmap */
			} else if (regmap_is_enabled(root_regmap, idx)) {

				regmap_disable(root_regmap, idx);
			} else {
				printk("ERROR: Root cell does not own bitmap for reg %llx\n",
						addr);
				return -EINVAL;
			}
		}

		/* reuse the same root_regmap for next register if idx is within limit */
		idx++;
		if (idx >= root_regmap->info->reg_count)
			root_regmap = NULL;
	}
	return 0;
}

static int regmap_cell_init(struct cell *cell)
{
	const struct jailhouse_regmap *info;
	struct reg_map_data *regmap;
	u32 i, num_pages, size;
	int ret;

	if (cell->config->num_regmaps == 0)
		return 0;

	num_pages = PAGES(cell->config->num_regmaps * sizeof(struct reg_map_data));
	cell->regmap = page_alloc(&mem_pool, num_pages);
	if (!cell->regmap)
		return -ENOMEM;

	info = jailhouse_cell_regmaps(cell->config);
	for (i = 0; i < cell->config->num_regmaps; i++, info++) {

		regmap = &cell->regmap[i];
		regmap->info = info;
		regmap->cell = cell;
		size = info->reg_size * info->reg_count;

		if (info->reg_count > JAILHOUSE_REGMAP_BITS ||
		    (info->flags & (JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE)) == 0)
			goto invalid;

		regmap->map_base = paging_map_device(info->reg_base, size);
		if (!regmap->map_base)
			return -ENOMEM;

		memcpy(regmap->reg_bitmap, info->reg_bitmap,
			sizeof(regmap->reg_bitmap));

		mmio_region_register(cell, info->reg_base, size,
			regmap_handler, regmap);

		/* Unmap the memory so that handler can be triggered */
		ret = paging_destroy(&cell->arch.mm, info->reg_base, size,
				PAGING_COHERENT);
		if (ret)
			goto invalid;

		ret = regmap_modify_root(cell, regmap, false);
		if (ret)
			goto invalid;
	}

	return 0;
invalid:
	page_free(&mem_pool, cell->regmap, 1);
	return -EINVAL;
}

static void regmap_cell_exit(struct cell *cell)
{
	struct reg_map_data *regmap;
	u32 i, num_pages;

	for (i = 0; i < cell->config->num_regmaps; i++) {
		regmap = &cell->regmap[i];
		regmap_modify_root(cell, regmap, true);
	}

	num_pages = PAGES(cell->config->num_regmaps);
	page_free(&mem_pool, cell->regmap, num_pages);
}

static int regmap_init(void)
{
	return regmap_cell_init(&root_cell);
}

static unsigned int regmap_mmio_count_regions(struct cell *cell)
{
	return cell->config->num_regmaps;
}

DEFINE_UNIT_SHUTDOWN_STUB(regmap);
DEFINE_UNIT(regmap, "regmap");
